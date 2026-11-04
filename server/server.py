"""
server.py
Server Web Flask + Flask-SocketIO per il Pannello di Controllo Zulu Game System.

Espone:
- Le viste tattiche per gli arbitri/admin (/, /mobile, /tv).
- Il portale personale per i giocatori PRO (/portal/login, /portal/setup, /portal/dashboard).
- Gli endpoint WebSocket (Socket.IO) per la gestione in tempo reale di roster, database, OTA e missioni.
"""

from functools import wraps
from threading import Lock
import logging
import json
import time
import os
import sys
import urllib.request

from flask import Flask, render_template, request, jsonify, redirect, url_for, flash, abort
from flask_socketio import SocketIO, emit
from flask_login import LoginManager, UserMixin, login_user, login_required, logout_user, current_user
from werkzeug.utils import secure_filename
from werkzeug.security import generate_password_hash, check_password_hash

import db_manager
import game_engine

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# =============================================================================
# CONFIGURAZIONE FLASK, UPLOAD E AUTENTICAZIONE
# =============================================================================

app = Flask(__name__)
app.config['SECRET_KEY'] = 'philanthropy_secret_key_change_in_prod'

UPLOAD_FOLDER = os.path.join(app.root_path, 'static', 'uploads')
os.makedirs(UPLOAD_FOLDER, exist_ok=True)
app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER

socketio = SocketIO(app, cors_allowed_origins="*", async_mode='threading')

login_manager = LoginManager()
login_manager.init_app(app)

_init_lock = Lock()

USERS = {
    "admin": {
        "password_hash": "scrypt:32768:8:1$UudpiqxEpDnMIMyI$d1c51523f034965a99cb569f943943399dc8722d382fec63de356a4f4b74fdebe269ff61c287b4784886553adb74086a2dac9cc570a13caa7434f3218dd2ca13",
        "name": "Admin"
    },
    "topgun": {
        "password_hash": "scrypt:32768:8:1$7OpLxyv7XJkDXsrf$f1b769dd4f3496651e6c3a17cc56b4aa0469985e8a1ae0655eec9a93456e04cb501259345d00607e534a7822dbac4e4394a0b6f09315b5deeae29930b43eee67",
        "name": "TopGun"
    }
}


class User(UserMixin):
    """Modello utente unificato per Amministratori e Operatori PRO."""
    def __init__(self, id, role="admin", name=None):
        self.id = id
        self.role = role
        self.name = name or id

    @staticmethod
    def get(user_id):
        if user_id in USERS:
            return User(user_id, role="admin", name=USERS[user_id]['name'])

        player = db_manager.get_player_profile(user_id)
        if player and player['is_pro'] == 1:
            role = "player" if player.get('username') else "player_setup"
            return User(user_id, role=role, name=player['alias'])

        return None


@login_manager.user_loader
def load_user(user_id):
    return User.get(user_id)


@login_manager.unauthorized_handler
def unauthorized():
    if request.path.startswith('/portal'):
        flash("ACCESSO NEGATO: Sessione scaduta o non valida.", "error")
        return redirect(url_for('player_login'))
    flash("ACCESSO NEGATO", "error")
    return redirect(url_for('login'))


def admin_required(f):
    """Decoratore per limitare l'accesso alle sole rotte amministrative."""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if not current_user.is_authenticated:
            return redirect(url_for('login'))
        if current_user.role != 'admin':
            return redirect(url_for('player_portal'))
        return f(*args, **kwargs)
    return decorated_function


# =============================================================================
# SERVIZIO METEO, MANIFESTO OTA E INIZIALIZZAZIONE SOTTOSISTEMI
# =============================================================================

last_weather_update = 0
cached_weather = "--°C // SCANNING..."


def fetch_weather():
    """Recupera e mette in cache per 15 minuti le condizioni meteo locali da Open-Meteo."""
    global last_weather_update, cached_weather
    now = time.time()
    if now - last_weather_update > 900:
        try:
            url = 'https://api.open-meteo.com/v1/forecast?latitude=45.615&longitude=9.005&current_weather=true'
            req = urllib.request.urlopen(url, timeout=5)
            data = json.loads(req.read().decode('utf-8'))
            cw = data.get('current_weather')
            if cw:
                temp = round(cw.get('temperature', 0))
                code = cw.get('weathercode', 0)
                is_day = cw.get('is_day', 1)
                
                condition = "CLEAR"
                if 1 <= code <= 3:
                    condition = "CLOUDS"
                elif 45 <= code <= 48:
                    condition = "FOG"
                elif 51 <= code <= 67:
                    condition = "RAIN"
                elif 71 <= code <= 77:
                    condition = "SNOW"
                elif 80 <= code <= 82:
                    condition = "SHOWERS"
                elif code >= 95:
                    condition = "STORM"
                
                if is_day == 0 and condition in ["CLEAR", "CLOUDS"]:
                    condition += "_NIGHT"
                
                cached_weather = f"{temp}°C // {condition}"
                last_weather_update = now
        except Exception:
            pass
    return cached_weather


def get_ota_manifest():
    """Legge il manifesto delle versioni firmware disponibili in static/firmware/manifest.json."""
    manifest_path = os.path.join(app.root_path, 'static', 'firmware', 'manifest.json')
    if os.path.exists(manifest_path):
        try:
            with open(manifest_path, 'r', encoding='utf-8') as f:
                return json.load(f)
        except Exception:
            pass
    return {}


@app.before_request
def setup_database():
    """Inizializza in modo thread-safe DB, client MQTT e background task al primo avvio."""
    if not getattr(app, 'db_initialized', False):
        with _init_lock:
            if not getattr(app, 'db_initialized', False):
                db_manager.init_db()
                game_engine.start_mqtt(socketio)
                if not game_engine.background_thread_started:
                    socketio.start_background_task(game_engine.background_cleanup, fetch_weather)
                    game_engine.background_thread_started = True
                app.db_initialized = True


# =============================================================================
# ROTTE HTTP: PANNELLO ADMIN, TV, MOBILE E PORTALE PRO
# =============================================================================

@app.route('/')
@admin_required
def index():
    return render_template('index.html', username=current_user.name)


@app.route('/tv')
def tv_dashboard():
    return render_template('tv.html')


@app.route('/mobile')
@admin_required
def mobile_dashboard():
    return render_template('mobile.html', username=current_user.name)


@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')
        next_page = request.args.get('next')
        
        if username in USERS and check_password_hash(USERS[username]['password_hash'], password):
            login_user(User.get(username))
            return redirect(next_page or url_for('index'))
            
        flash('ACCESSO NEGATO', 'error')
    return render_template('login.html')


@app.route('/logout')
@login_required
def logout():
    logout_user()
    return redirect(url_for('login'))


# --- Area Giocatori PRO ---

@app.route('/portal/login', methods=['GET', 'POST'])
def player_login():
    if request.method == 'POST':
        login_id = request.form.get('login_id', '').strip()
        password = request.form.get('password', '').strip()
        
        player = db_manager.get_player_profile(login_id.upper())
        if not player:
            player = db_manager.get_player_by_username(login_id)
            
        if player and player['is_pro'] == 1:
            if player.get('username') and player.get('password_hash'):
                if check_password_hash(player['password_hash'], password):
                    login_user(User(player['uid'], role="player", name=player['alias']))
                    return redirect(url_for('player_portal'))
                else:
                    flash('Credenziali non valide o password errata.', 'error')
                    return render_template('player_login.html')
            else:
                if login_id.upper() == player['uid']:
                    login_user(User(player['uid'], role="player_setup", name=player['alias']))
                    return redirect(url_for('player_setup'))
                else:
                    flash('Account non configurato. Usa l\'UID della tessera.', 'error')
        else:
            flash('Utente non trovato o account non abilitato.', 'error')
            
    return render_template('player_login.html')


@app.route('/portal/setup', methods=['GET', 'POST'])
@login_required
def player_setup():
    if current_user.role == 'admin':
        return redirect(url_for('index'))
    if current_user.role != 'player_setup':
        return redirect(url_for('player_portal'))
        
    if request.method == 'POST':
        username = request.form.get('username', '').strip()
        password = request.form.get('password', '').strip()
        
        if not username or not password:
            flash('Tutti i campi sono obbligatori.', 'error')
            return render_template('player_setup.html')
            
        existing = db_manager.get_player_by_username(username)
        if existing:
            flash('Username già in uso, sceglierne un altro.', 'error')
            return render_template('player_setup.html')
            
        db_manager.set_player_credentials(current_user.id, username, generate_password_hash(password))
        
        player = db_manager.get_player_profile(current_user.id)
        login_user(User(current_user.id, role="player", name=player['alias']))
        return redirect(url_for('player_portal'))
        
    return render_template('player_setup.html')


@app.route('/portal/dashboard')
@login_required
def player_portal():
    if current_user.role == 'admin':
        return redirect(url_for('index'))
    if current_user.role == 'player_setup':
        return redirect(url_for('player_setup'))
    if current_user.role != 'player':
        return abort(403)
    
    player_data = db_manager.get_player_profile(current_user.id)
    games = player_data.get('games_played', 0)
    wins = player_data.get('wins', 0)
    player_data['win_rate'] = round((wins / games) * 100) if games > 0 else 0
    player_data.pop('password_hash', None)
    
    history = db_manager.get_player_history(current_user.id)
    return render_template('player_portal.html', player=player_data, history=history)


@app.route('/portal/upload_photo', methods=['POST'])
@login_required
def upload_photo_player():
    if current_user.role != 'player':
        return jsonify({"status": "error"}), 403
    if 'photo' not in request.files:
        return jsonify({"status": "error"}), 400
    file = request.files['photo']
    if file.filename != '':
        filename = secure_filename(f"op_{current_user.id}.jpg")
        file.save(os.path.join(app.config['UPLOAD_FOLDER'], filename))
        db_manager.update_player_photo(current_user.id, filename)
        return jsonify({"status": "ok", "filename": filename})
    return jsonify({"status": "error"}), 400


@app.route('/portal/rename', methods=['POST'])
@login_required
def rename_player_portal():
    if current_user.role != 'player':
        return jsonify({"status": "error"}), 403
    new_name = request.form.get('new_name')
    if new_name and new_name.strip() != '':
        clean_name = new_name.strip().upper()
        db_manager.rename_player(current_user.id, clean_name)
        socketio.emit('database_update', db_manager.get_all_players())
        socketio.emit('roster_update', db_manager.get_full_roster())
        return jsonify({"status": "ok", "new_name": clean_name})
    return jsonify({"status": "error"}), 400


@app.route('/portal/edit_asd', methods=['POST'])
@login_required
def edit_asd_portal():
    if current_user.role != 'player':
        return jsonify({"status": "error"}), 403
    new_asd = request.form.get('new_asd', '').strip().upper()
    if new_asd == "":
        new_asd = "NESSUNA"
    db_manager.update_player_asd(current_user.id, new_asd)
    socketio.emit('database_update', db_manager.get_all_players())
    socketio.emit('roster_update', db_manager.get_full_roster())
    return jsonify({"status": "ok", "new_asd": new_asd})


@app.route('/upload_photo', methods=['POST'])
@admin_required
def upload_photo():
    if 'photo' not in request.files or 'uid' not in request.form:
        return jsonify({"status": "error"}), 400
    file = request.files['photo']
    uid = request.form['uid']
    if file.filename != '':
        filename = secure_filename(f"op_{uid}.jpg")
        file.save(os.path.join(app.config['UPLOAD_FOLDER'], filename))
        db_manager.update_player_photo(uid, filename)
        return jsonify({"status": "ok", "filename": filename})
    return jsonify({"status": "error"}), 400


@app.route('/upload_device_photo', methods=['POST'])
@admin_required
def upload_device_photo():
    if 'photo' not in request.files or 'device_id' not in request.form:
        return jsonify({"status": "error"}), 400
    file = request.files['photo']
    device_id = request.form['device_id'].strip()
    if file.filename != '' and device_id:
        filename = secure_filename(f"dev_{device_id}.jpg")
        file.save(os.path.join(app.config['UPLOAD_FOLDER'], filename))
        game_engine.registry.set_device_photo(device_id, filename)
        active_devs, _ = game_engine.registry.get_active_devices()
        socketio.emit('devices_update', active_devs)
        return jsonify({"status": "ok", "filename": filename})
    return jsonify({"status": "error"}), 400


@socketio.on('delete_device_photo')
@login_required
def handle_delete_device_photo(data):
    device_id = data.get('id')
    if device_id:
        game_engine.registry.set_device_photo(device_id, None)
        active_devs, _ = game_engine.registry.get_active_devices()
        socketio.emit('devices_update', active_devs)


# =============================================================================
# GESTIONE EVENTI WEBSOCKET (SOCKET.IO)
# =============================================================================

@socketio.on('connect')
def handle_connect():
    active_devs, _ = game_engine.registry.get_active_devices()
    emit('devices_update', active_devs)
    emit('weather_update', {'weather': fetch_weather()})
    emit('factions_update', db_manager.get_factions_config())
    socketio.emit('roster_update', db_manager.get_full_roster())
    emit('database_update', db_manager.get_all_players())
    emit('ota_manifest_update', get_ota_manifest())


@socketio.on('update_factions')
@login_required
def handle_update_factions(data):
    if 'alpha_name' in data and 'alpha_color' in data:
        db_manager.set_faction_config('ALPHA', data['alpha_name'], data['alpha_color'])
    if 'bravo_name' in data and 'bravo_color' in data:
        db_manager.set_faction_config('BRAVO', data['bravo_name'], data['bravo_color'])
        
    if 'alpha_wins' in data:
        db_manager.update_faction_wins('ALPHA', data['alpha_wins'])
    if 'bravo_wins' in data:
        db_manager.update_faction_wins('BRAVO', data['bravo_wins'])
    
    new_config = db_manager.get_factions_config()
    socketio.emit('factions_update', new_config)
        
    a_rgb = game_engine.get_pure_rgb(new_config['ALPHA']['color'])
    b_rgb = game_engine.get_pure_rgb(new_config['BRAVO']['color'])
    
    game_engine.mqtt_client.publish("zulu/cmd/broadcast", json.dumps({
        "cmd": "SET_FACTIONS",
        "t1": new_config['ALPHA']['name'], "t1_c": a_rgb,
        "t2": new_config['BRAVO']['name'], "t2_c": b_rgb
    }))


@socketio.on('reset_daily_wins')
def handle_reset_daily_wins():
    db_manager.reset_daily_wins()
    socketio.emit('factions_update', db_manager.get_factions_config())


@socketio.on('request_roster')
def handle_request_roster():
    socketio.emit('roster_update', db_manager.get_full_roster())


@socketio.on('register_operator')
def handle_register_operator(data):
    uid, team = data.get('uid'), data.get('team')
    if uid and team:
        db_manager.register_player(uid, team)
        socketio.emit('roster_update', db_manager.get_full_roster())


@socketio.on('update_operator')
def handle_update_operator(data):
    uid, action, value = data.get('uid'), data.get('action'), data.get('value')
    if action == 'rename':
        db_manager.rename_player(uid, value)
    elif action == 'swap':
        db_manager.swap_player_team(uid)
    elif action == 'status_toggle':
        row = db_manager.get_player_status(uid)
        if row:
            new_status = db_manager.toggle_player_status(uid, row[0])
            if new_status == 'FUORI':
                with game_engine.mission_control.lock:
                    is_active = (game_engine.mission_control.active_mission and game_engine.mission_control.state == "ACTIVE")
                if is_active:
                    db_manager.increment_death(uid)
    elif action == 'delete':
        db_manager.remove_from_roster(uid)
    socketio.emit('roster_update', db_manager.get_full_roster())


@socketio.on('clear_roster')
def handle_clear_roster():
    db_manager.clear_active_roster()
    socketio.emit('roster_update', db_manager.get_full_roster())


@socketio.on('request_database')
def handle_request_database():
    socketio.emit('database_update', db_manager.get_all_players())


@socketio.on('register_player_manual')
def handle_register_manual(data):
    uid = data.get('uid')
    alias = data.get('alias')
    is_pro = data.get('is_pro', 0)
    tag_info = data.get('tag_info', 'NTAG215 / ISO 14443-3A')
    asd_team = data.get('asd_team', 'NESSUNA')
    
    if uid and alias:
        db_manager.register_player_manual(uid, alias.upper(), is_pro, tag_info, asd_team.upper())
        socketio.emit('database_update', db_manager.get_all_players())


@socketio.on('update_db_player')
def handle_update_db_player(data):
    uid, action, value = data.get('uid'), data.get('action'), data.get('value')
    if action == 'rename':
        db_manager.rename_player(uid, value)
    elif action == 'delete':
        db_manager.delete_player_permanently(uid)
    elif action == 'reset_stats':
        db_manager.reset_player_stats(uid)
    elif action == 'toggle_pro':
        db_manager.toggle_pro_status(uid)
    elif action == 'edit_asd':
        db_manager.update_player_asd(uid, value)
    elif action == 'delete_photo':
        db_manager.delete_player_photo(uid)
    
    socketio.emit('roster_update', db_manager.get_full_roster())
    socketio.emit('database_update', db_manager.get_all_players())


@socketio.on('purge_guests')
def handle_purge_guests():
    db_manager.purge_guest_data()
    socketio.emit('database_update', db_manager.get_all_players())
    socketio.emit('roster_update', db_manager.get_full_roster())


@socketio.on('request_profile')
def handle_request_profile(data):
    uid = data.get('uid')
    profile = db_manager.get_player_profile(uid)
    if profile:
        profile.pop('password_hash', None)
        profile['history'] = db_manager.get_player_history(uid)
        emit('profile_data', profile)


@socketio.on('save_notes')
def handle_save_notes(data):
    db_manager.update_player_notes(data.get('uid'), data.get('notes'))


@socketio.on('update_karma')
def handle_update_karma(data):
    uid = data.get('uid')
    db_manager.update_player_karma(uid, data.get('karma'))
    socketio.emit('database_update', db_manager.get_all_players())
    profile = db_manager.get_player_profile(uid)
    if profile:
        profile.pop('password_hash', None)
        profile['history'] = db_manager.get_player_history(uid)
        emit('profile_data', profile)


@socketio.on('request_mission_start')
def handle_request_mission_start(data):
    mode = data.get('mode')
    target_id = data.get('target_id')
    dur = int(data.get('duration', 15))
    force = data.get('force', False)
    settings = data.get('settings')

    if mode == 'sd' and isinstance(settings, dict):
        game_engine.mission_control.sd_settings = settings
    
    players = db_manager.get_active_roster_status()
    players_out = sum(1 for p in players if p[2] != 'IN')
    alpha_in = sum(1 for p in players if p[1] == 'ALPHA' and p[2] == 'IN')
    bravo_in = sum(1 for p in players if p[1] == 'BRAVO' and p[2] == 'IN')
    
    if not force:
        if len(players) == 0:
            emit('mission_start_warning', {'msg': "Il Roster è vuoto. Vuoi avviare la missione comunque?", 'original_request': data})
            return
        if players_out > 0:
            emit('mission_start_error', {'msg': f"Ci sono {players_out} operatori fuori dal campo. Tutti devono risultare 'IN CAMPO'."})
            return
        if alpha_in == 0 or bravo_in == 0:
            emit('mission_start_warning', {'msg': f"Squadre sbilanciate (A:{alpha_in} B:{bravo_in}). Avviare comunque?", 'original_request': data})
            return
            
    game_engine.mission_control.start_mission(mode, target_id, duration=dur)
    socketio.emit('mission_start_success')


@socketio.on('mobile_tag_scanned')
def handle_mobile_scan(data):
    uid = data.get('uid')
    if uid:
        socketio.emit('raw_tag_scanned', {'uid': uid})
        row = db_manager.get_player_status(uid)
        if row:
            new_status = db_manager.toggle_player_status(uid, row[0])
            if new_status == 'FUORI':
                with game_engine.mission_control.lock:
                    is_active = (game_engine.mission_control.active_mission and game_engine.mission_control.state == "ACTIVE")
                if is_active:
                    db_manager.increment_death(uid)
                    
            socketio.emit('roster_update', db_manager.get_full_roster())
            emit('mobile_scan_reply', {'success': True, 'uid': uid, 'status': new_status, 'team': row[1]})
        else:
            emit('mobile_scan_reply', {'success': False, 'uid': uid, 'msg': 'Tag non assegnato al roster.'})


@socketio.on('rename_device')
def handle_rename(data):
    if data.get('id') and data.get('name'):
        game_engine.registry.rename_device(data['id'], data['name'])
        active_devs, _ = game_engine.registry.get_active_devices()
        socketio.emit('devices_update', active_devs)


@socketio.on('request_manual_scan')
def handle_manual_scan():
    active_devs, _ = game_engine.registry.get_active_devices()
    socketio.emit('devices_update', active_devs)
    emit('ota_manifest_update', get_ota_manifest())


@socketio.on('send_command')
def handle_socket_command(data):
    try:
        target_id = data.get('target_id')
        cmd_obj = data.get('command')
        cmd_str = json.dumps(cmd_obj) if isinstance(cmd_obj, dict) else cmd_obj
        
        if isinstance(cmd_obj, dict):
            c = cmd_obj.get("cmd", "")
            if c == "SET_READY":
                game_engine.mission_ready_flag = True
                game_engine.mission_control.mode = cmd_obj.get("mode")
                game_engine.mission_control.target_id = cmd_obj.get("target_id")
                game_engine.mission_control.global_duration = int(cmd_obj.get("duration", 15)) * 60
                return
            elif c == "SET_SD_SETTINGS":
                game_engine.mission_control.sd_settings = cmd_obj
                game_engine.publish_sd_settings_to_target(target_id, cmd_obj)
                return
            elif c == "SET_TDM_SETTINGS":
                return
            elif c == "TRIGGER_OTA":
                manifest = get_ota_manifest()
                fw_key = "rat-v2" if "V2" in str(target_id).upper() else "rat-v1"
                fw_file = manifest.get(fw_key, {}).get("file", f"{fw_key}.bin")
                host_ip = request.host.split(':')[0]
                ota_url = f"http://{host_ip}:5000/static/firmware/{fw_file}"
                game_engine.mqtt_client.publish(f"zulu/cmd/{target_id}", json.dumps({
                    "cmd": "OTA_HTTP",
                    "url": ota_url
                }), qos=1)
                return

        if target_id == "BROADCAST_ENV":
            game_engine.mqtt_client.publish("zulu/cmd/broadcast", cmd_str)
            return

        if target_id == "SERVER-ROUTER":
            rt = game_engine.mission_control.target_id
            if not game_engine.mission_control.active_mission or not rt:
                return
            if "FORCE_END_GAME" in cmd_str:
                game_engine.mission_control.force_stop_mission()
            elif "CLOSE_MISSION" in cmd_str:
                game_engine.mission_control.close_and_dismiss_mission()
            else:
                game_engine.mqtt_client.publish(f"zulu/cmd/{rt}", cmd_str)
            return

        game_engine.mqtt_client.publish(f"zulu/cmd/{target_id}", cmd_str)
             
    except Exception as e:
        logger.error(f"[ERROR SOCKET CMD] {e}")

import hmac
import hashlib
import subprocess

# Imposta una tua chiave segreta (la stessa che inserirai su GitHub)
GITHUB_WEBHOOK_SECRET = os.environ.get('ZULU_WEBHOOK_SECRET', 'philanthropy_webhook_secret_2026')

@app.route('/api/github-webhook', methods=['POST'])
def github_webhook():
    signature = request.headers.get('X-Hub-Signature-256', '')
    if not signature.startswith('sha256='):
        return jsonify({"status": "unauthorized"}), 403

    mac = hmac.new(GITHUB_WEBHOOK_SECRET.encode('utf-8'), msg=request.data, digestmod=hashlib.sha256)
    if not hmac.compare_digest(mac.hexdigest(), signature.split('=')[1]):
        return jsonify({"status": "invalid signature"}), 403

    payload = request.get_json(silent=True) or {}
    # Esegue il deploy solo sui push verso il branch main
    if payload.get('ref') and payload.get('ref') != 'refs/heads/main':
        return jsonify({"status": "ignored branch"}), 200

    repo_root = os.path.abspath(os.path.join(app.root_path, '..'))
    subprocess.run(['git', 'pull', 'origin', 'main'], cwd=repo_root)

    # Controlla se sono stati modificati file del server o solo firmware/asset
    changed_files = []
    for commit in payload.get('commits', []):
        changed_files.extend(commit.get('added', []) + commit.get('modified', []) + commit.get('removed', []))

    server_code_changed = any(
        f.startswith('server/') and not f.startswith('server/static/firmware/')
        for f in changed_files
    )

    # Aggiorna subito il manifesto OTA su tutti i browser connessi
    socketio.emit('ota_manifest_update', get_ota_manifest())

    if server_code_changed or not changed_files:
        logger.info("[WEBHOOK] Modifiche al codice server rilevate: riavvio automatico in corso...")
        def restart_after_response():
            time.sleep(1)
            socketio.emit('server_restarting')
            time.sleep(0.5)
            os.execv(sys.executable, [sys.executable] + sys.argv)
        socketio.start_background_task(restart_after_response)
        return jsonify({"status": "updated_and_restarting"}), 200

    logger.info("[WEBHOOK] Aggiornato solo il pacchetto Firmware OTA (nessun riavvio server necessario).")
    return jsonify({"status": "ota_updated_live"}), 200

@socketio.on('server_reset_request')
@login_required
def handle_server_reset():
    socketio.emit('server_restarting')
    time.sleep(1)
    os.execv(sys.executable, [sys.executable] + sys.argv)


if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=5000, debug=False, allow_unsafe_werkzeug=True)