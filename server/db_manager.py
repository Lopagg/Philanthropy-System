"""
db_manager.py
Gestore centralizzato della persistenza SQLite per Zulu Game System.

Gestisce:
- Anagrafica operatori, statistiche di carriera (partite, vittorie, eliminazioni, karma) e credenziali PRO.
- Roster attivo di giornata (squadre ALPHA/BRAVO e stato IN/FUORI).
- Configurazione delle Fazioni (nomi custom, colori esadecimali, vittorie giornaliere).
- Alias personalizzati dei nodi hardware e storico partite per operatore.
"""

import sqlite3
import datetime
import logging

logger = logging.getLogger(__name__)
DB_PATH = 'philanthropy_arena.db'


# =============================================================================
# CONNESSIONE E INIZIALIZZAZIONE DATABASE
# =============================================================================

def get_current_time():
    """Restituisce il timestamp corrente formattato (YYYY-MM-DD HH:MM:SS)."""
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def get_db_connection(use_row_factory=False):
    """
    Crea e restituisce una connessione SQLite thread-safe con timeout esteso (10s)
    per prevenire conflitti di lock tra thread Flask, MQTT e background task.
    """
    conn = sqlite3.connect(DB_PATH, timeout=10.0)
    if use_row_factory:
        conn.row_factory = sqlite3.Row
    return conn


def init_db():
    """
    Inizializza lo schema del database, applica le migrazioni incrementali sulle colonne
    e configura la modalità WAL (Write-Ahead Logging) per massimizzare la concorrenza.
    """
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("PRAGMA journal_mode=WAL;")
        c.execute("PRAGMA synchronous=NORMAL;")

        # 1. Tabella principale giocatori
        c.execute('''
            CREATE TABLE IF NOT EXISTS players (
                uid TEXT PRIMARY KEY,
                alias TEXT,
                games_played INTEGER DEFAULT 0,
                wins INTEGER DEFAULT 0
            )
        ''')

        # Migrazioni non distruttive per colonne aggiunte nelle versioni successive
        player_columns = [
            "registered_at TEXT",
            "notes TEXT",
            "photo TEXT",
            "games_played INTEGER DEFAULT 0",
            "wins INTEGER DEFAULT 0",
            "deaths INTEGER DEFAULT 0",
            "karma INTEGER DEFAULT 0",
            "is_pro INTEGER DEFAULT 0",
            "tag_info TEXT DEFAULT 'NTAG215'",
            "original_alias TEXT",
            "asd_team TEXT DEFAULT 'NESSUNA'",
            "username TEXT",
            "password_hash TEXT"
        ]
        for col_def in player_columns:
            try:
                c.execute(f"ALTER TABLE players ADD COLUMN {col_def}")
            except sqlite3.OperationalError:
                pass

        # Normalizzazione valori di default su record storici
        c.execute("UPDATE players SET games_played = 0 WHERE games_played IS NULL")
        c.execute("UPDATE players SET wins = 0 WHERE wins IS NULL")
        c.execute("UPDATE players SET deaths = 0 WHERE deaths IS NULL")
        c.execute("UPDATE players SET karma = 0 WHERE karma IS NULL")
        c.execute("UPDATE players SET is_pro = 0 WHERE is_pro IS NULL")
        c.execute("UPDATE players SET asd_team = 'NESSUNA' WHERE asd_team IS NULL")
        c.execute("UPDATE players SET original_alias = alias WHERE original_alias IS NULL")

        # 2. Tabelle operative (Roster attivo, Alias Dispositivi, Configurazione Fazioni, Storico)
        c.execute('''CREATE TABLE IF NOT EXISTS active_roster (uid TEXT PRIMARY KEY, team TEXT, status TEXT)''')
        c.execute('''CREATE TABLE IF NOT EXISTS device_aliases (device_id TEXT PRIMARY KEY, alias TEXT)''')
        try:
            c.execute("ALTER TABLE device_aliases ADD COLUMN photo TEXT")
            
        except sqlite3.OperationalError:
            pass
        try:
            c.execute("ALTER TABLE device_aliases ADD COLUMN volume INTEGER DEFAULT 100")
        except sqlite3.OperationalError:
            pass
        c.execute('''CREATE TABLE IF NOT EXISTS factions_config (team_id TEXT PRIMARY KEY, display_name TEXT)''')
        c.execute('''
            CREATE TABLE IF NOT EXISTS match_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                uid TEXT,
                match_date TEXT,
                game_mode TEXT,
                result TEXT,
                FOREIGN KEY(uid) REFERENCES players(uid)
            )
        ''')

        for col_def in ["hex_color TEXT", "daily_wins INTEGER DEFAULT 0"]:
            try:
                c.execute(f"ALTER TABLE factions_config ADD COLUMN {col_def}")
            except sqlite3.OperationalError:
                pass

        c.execute("INSERT OR IGNORE INTO factions_config (team_id, display_name, hex_color, daily_wins) VALUES ('ALPHA', 'ALPHA', '#ff3333', 0)")
        c.execute("INSERT OR IGNORE INTO factions_config (team_id, display_name, hex_color, daily_wins) VALUES ('BRAVO', 'BRAVO', '#55ff55', 0)")
        c.execute("UPDATE factions_config SET hex_color = '#ff3333' WHERE team_id = 'ALPHA' AND hex_color IS NULL")
        c.execute("UPDATE factions_config SET hex_color = '#55ff55' WHERE team_id = 'BRAVO' AND hex_color IS NULL")
        c.execute("UPDATE factions_config SET daily_wins = 0 WHERE daily_wins IS NULL")

        # 3. Tabella Preset di Configurazione Missione
        c.execute('''
            CREATE TABLE IF NOT EXISTS mission_presets (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT UNIQUE,
                mode TEXT,
                config_json TEXT,
                updated_at TEXT
            )
        ''')

        # Se non esistono ancora preset, inserisce 3 preset tattici di base
        c.execute("SELECT COUNT(*) FROM mission_presets")
        if (c.fetchone()[0] or 0) == 0:
            import json as _json
            default_presets = [
                (
                    "S&D - CLASSICO (SOLO HOLD)",
                    "sd",
                    _json.dumps({
                        "attacker": "ALPHA", "game_duration": 20, "bomb_time": 10,
                        "fx_smoke": False, "fx_relay": False,
                        "arm_fail_mode": "RETRY_STEP", "defuse_fail_mode": "RETRY_STEP",
                        "v2_arm_seq": [{"type": "HOLD", "val": 5, "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""}],
                        "v2_defuse_seq": [{"type": "HOLD", "val": 10, "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""}],
                        "arm_time": 5, "defuse_time": 10, "use_arm_pin": "false", "use_defuse_pin": "false",
                        "arm_pin": "1234", "defuse_pin": "4321"
                    })
                ),
                (
                    "S&D - HARDCORE (CHIAVE + PIN + ESPLOSIONE)",
                    "sd",
                    _json.dumps({
                        "attacker": "ALPHA", "game_duration": 20, "bomb_time": 8,
                        "fx_smoke": True, "fx_relay": True,
                        "arm_fail_mode": "RESTART_SEQ", "defuse_fail_mode": "EXPLODE",
                        "v2_arm_seq": [
                            {"type": "KEY", "val": "K1_ON", "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""},
                            {"type": "PIN", "val": "7355", "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""},
                            {"type": "HOLD", "val": 5, "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""}
                        ],
                        "v2_defuse_seq": [
                            {"type": "RFID", "val": "", "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""},
                            {"type": "KEY", "val": "K1_OFF", "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""},
                            {"type": "PIN", "val": "7355", "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""},
                            {"type": "HOLD", "val": 10, "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""}
                        ],
                        "arm_time": 5, "defuse_time": 10, "use_arm_pin": "true", "use_defuse_pin": "true",
                        "arm_pin": "7355", "defuse_pin": "7355"
                    })
                ),
                (
                    "DOMINIO - CONQUISTA STANDARD (10s)",
                    "dom",
                    _json.dumps({
                        "game_duration": 15, "fx_smoke": False, "fx_relay": False,
                        "v2_dom_alpha_seq": [{"type": "HOLD", "val": 10, "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""}],
                        "v2_dom_bravo_seq": [{"type": "HOLD", "val": 10, "rfidMode": "TEAM", "rfidPlayers": [], "rfidCustomUid": ""}],
                        "capture_time": 10, "countdown": 10
                    })
                )
            ]
            now_str = get_current_time()
            for p_name, p_mode, p_cfg in default_presets:
                c.execute(
                    "INSERT OR IGNORE INTO mission_presets (name, mode, config_json, updated_at) VALUES (?, ?, ?, ?)",
                    (p_name, p_mode, p_cfg, now_str)
                )

        # 4. Tabelle Gestionale Giornate di Gioco (Calendario Eventi e Partecipanti/Bozza Squadre)
        c.execute('''
            CREATE TABLE IF NOT EXISTS game_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT,
                event_date TEXT,
                start_time TEXT DEFAULT '08:30',
                status TEXT DEFAULT 'PLANNED',
                max_players INTEGER DEFAULT 30,
                alpha_name TEXT DEFAULT 'ALPHA',
                alpha_color TEXT DEFAULT '#ff3333',
                bravo_name TEXT DEFAULT 'BRAVO',
                bravo_color TEXT DEFAULT '#55ff55',
                schedule_json TEXT DEFAULT '[]',
                notes TEXT DEFAULT '',
                created_at TEXT
            )
        ''')

        c.execute('''
            CREATE TABLE IF NOT EXISTS event_participants (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                event_id INTEGER,
                uid TEXT,
                guest_name TEXT,
                team TEXT DEFAULT 'UNASSIGNED',
                needs_rental INTEGER DEFAULT 0,
                paid INTEGER DEFAULT 0,
                notes TEXT DEFAULT '',
                FOREIGN KEY(event_id) REFERENCES game_events(id)
            )
        ''')
        try:
            c.execute("ALTER TABLE event_participants ADD COLUMN assigned_uid TEXT")
        except sqlite3.OperationalError:
            pass

        conn.commit()


# =============================================================================
# GESTIONE FAZIONI E ALIAS DISPOSITIVI
# =============================================================================

def get_factions_config():
    """Restituisce il dizionario con nome, colore e vittorie odierne per ALPHA e BRAVO."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT team_id, display_name, hex_color, daily_wins FROM factions_config")
        rows = c.fetchall()
        config = {
            'ALPHA': {'name': 'ALPHA', 'color': '#ff3333', 'daily_wins': 0},
            'BRAVO': {'name': 'BRAVO', 'color': '#55ff55', 'daily_wins': 0}
        }
        for row in rows:
            team_id = row[0]
            if team_id in config:
                config[team_id] = {
                    'name': row[1],
                    'color': row[2] if row[2] else config[team_id]['color'],
                    'daily_wins': row[3] or 0
                }
        return config


def set_faction_config(team_id, new_name, hex_color):
    """Aggiorna il nome visualizzato e il colore esadecimale di una fazione."""
    if team_id not in ['ALPHA', 'BRAVO']:
        return False
    with get_db_connection() as conn:
        conn.execute(
            "UPDATE factions_config SET display_name = ?, hex_color = ? WHERE team_id = ?",
            (new_name, hex_color, team_id)
        )
        conn.commit()
    return True


def update_faction_wins(team_id, wins):
    """Imposta manualmente il contatore delle vittorie giornaliere di una fazione."""
    if team_id not in ['ALPHA', 'BRAVO']:
        return False
    with get_db_connection() as conn:
        conn.execute("UPDATE factions_config SET daily_wins = ? WHERE team_id = ?", (wins, team_id))
        conn.commit()
    return True


def reset_daily_wins():
    """Azzera le vittorie giornaliere di entrambe le fazioni."""
    with get_db_connection() as conn:
        conn.execute("UPDATE factions_config SET daily_wins = 0")
        conn.commit()
        logger.info("[DB] Vittorie di fazione odierne azzerate.")


def get_device_meta(device_id):
    """Restituisce un dizionario {'alias': ..., 'photo': ..., 'volume': ...} per il dispositivo."""
    with get_db_connection(use_row_factory=True) as conn:
        c = conn.cursor()
        c.execute("SELECT alias, photo, volume FROM device_aliases WHERE device_id = ?", (device_id,))
        row = c.fetchone()
        if row:
            vol = row['volume'] if row['volume'] is not None else 100
            return {'alias': row['alias'], 'photo': row['photo'], 'volume': int(vol)}
        return {'alias': None, 'photo': None, 'volume': None}


def get_device_alias(device_id):
    return get_device_meta(device_id)['alias']


def set_device_alias(device_id, alias):
    """Salva o aggiorna l'alias mantenendo eventuale foto e volume già associati."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT device_id FROM device_aliases WHERE device_id = ?", (device_id,))
        if c.fetchone():
            c.execute("UPDATE device_aliases SET alias = ? WHERE device_id = ?", (alias, device_id))
        else:
            c.execute("INSERT INTO device_aliases (device_id, alias, photo, volume) VALUES (?, ?, NULL, 100)", (device_id, alias))
        conn.commit()


def set_device_photo(device_id, filename):
    """Associa o rimuove (filename=None) la foto personalizzata di un nodo hardware."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT device_id FROM device_aliases WHERE device_id = ?", (device_id,))
        if c.fetchone():
            c.execute("UPDATE device_aliases SET photo = ? WHERE device_id = ?", (filename, device_id))
        else:
            c.execute("INSERT INTO device_aliases (device_id, alias, photo, volume) VALUES (?, ?, ?, 100)", (device_id, device_id, filename))
        conn.commit()


def set_device_volume(device_id, volume):
    """Salva nel database il volume (0..100%) associato a un nodo hardware."""
    vol = max(0, min(100, int(volume)))
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT device_id FROM device_aliases WHERE device_id = ?", (device_id,))
        if c.fetchone():
            c.execute("UPDATE device_aliases SET volume = ? WHERE device_id = ?", (vol, device_id))
        else:
            c.execute("INSERT INTO device_aliases (device_id, alias, photo, volume) VALUES (?, ?, NULL, ?)", (device_id, device_id, vol))
        conn.commit()


# =============================================================================
# GESTIONE ROSTER DI GIORNATA (ARENA)
# =============================================================================

def get_full_roster():
    """Restituisce il dizionario completo degli operatori presenti nel roster attivo."""
    with get_db_connection(use_row_factory=True) as conn:
        c = conn.cursor()
        c.execute('''
            SELECT a.uid, p.alias as name, a.team, a.status 
            FROM active_roster a 
            JOIN players p ON a.uid = p.uid
        ''')
        rows = c.fetchall()
        return {
            r['uid']: {'uid': r['uid'], 'name': r['name'], 'team': r['team'], 'status': r['status']}
            for r in rows
        }


def get_active_roster_status():
    """Restituisce la lista di tuple (uid, team, status) del roster attivo."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT uid, team, status FROM active_roster")
        return c.fetchall()


def get_player_status(uid):
    """Restituisce la tupla (status, team) di un operatore nel roster attivo, o None."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT status, team FROM active_roster WHERE uid = ?", (uid,))
        return c.fetchone()


def register_player(uid, team):
    """
    Registra un operatore nel roster attivo (impostandolo su 'FUORI').
    Se il tag non esiste ancora nel database generale, crea un profilo guest di default.
    """
    with get_db_connection() as conn:
        c = conn.cursor()
        default_alias = f"OP-{uid[:4]}"
        c.execute(
            "INSERT OR IGNORE INTO players (uid, alias, original_alias, registered_at) VALUES (?, ?, ?, ?)",
            (uid, default_alias, default_alias, get_current_time())
        )
        c.execute(
            "INSERT OR REPLACE INTO active_roster (uid, team, status) VALUES (?, ?, 'FUORI')",
            (uid, team)
        )
        conn.commit()


def toggle_player_status(uid, current_status):
    """Inverte lo stato IN/FUORI di un operatore nel roster attivo e restituisce il nuovo stato."""
    new_status = 'FUORI' if current_status == 'IN' else 'IN'
    with get_db_connection() as conn:
        conn.execute("UPDATE active_roster SET status = ? WHERE uid = ?", (new_status, uid))
        conn.commit()
    return new_status

def set_all_players_in():
    """Imposta lo stato di tutti gli operatori presenti nel roster attivo su 'IN'."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("UPDATE active_roster SET status = 'IN'")
        conn.commit()


def swap_player_team(uid):
    """Sposta un operatore del roster attivo da ALPHA a BRAVO o viceversa."""
    with get_db_connection() as conn:
        conn.execute(
            "UPDATE active_roster SET team = CASE WHEN team='ALPHA' THEN 'BRAVO' ELSE 'ALPHA' END WHERE uid = ?",
            (uid,)
        )
        conn.commit()


def remove_from_roster(uid):
    """Rimuove un singolo operatore dal roster attivo di giornata."""
    with get_db_connection() as conn:
        conn.execute("DELETE FROM active_roster WHERE uid = ?", (uid,))
        conn.commit()


def clear_active_roster():
    """Svuota completamente il roster attivo di giornata."""
    with get_db_connection() as conn:
        conn.execute("DELETE FROM active_roster")
        conn.commit()


# =============================================================================
# ANAGRAFICA GIOCATORI, PROFILI PRO E STATISTICHE
# =============================================================================

def register_player_manual(uid, alias, is_pro, tag_info, asd_team):
    """Registra o aggiorna manualmente una tessera operatore dal pannello Admin."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT uid FROM players WHERE uid = ?", (uid,))
        if c.fetchone():
            c.execute(
                "UPDATE players SET alias = ?, original_alias = ?, is_pro = ?, tag_info = ?, asd_team = ? WHERE uid = ?",
                (alias, alias, is_pro, tag_info, asd_team, uid)
            )
        else:
            c.execute(
                '''INSERT INTO players 
                   (uid, alias, original_alias, registered_at, games_played, wins, deaths, karma, is_pro, tag_info, asd_team) 
                   VALUES (?, ?, ?, ?, 0, 0, 0, 0, ?, ?, ?)''',
                (uid, alias, alias, get_current_time(), is_pro, tag_info, asd_team)
            )
        conn.commit()


def get_all_players():
    """Restituisce la lista di tutti i giocatori ordinata per alias (senza password_hash)."""
    with get_db_connection(use_row_factory=True) as conn:
        c = conn.cursor()
        c.execute("SELECT * FROM players ORDER BY alias")
        players = [dict(r) for r in c.fetchall()]
        for p in players:
            p.pop('password_hash', None)
        return players


def get_player_profile(uid):
    """Restituisce il dizionario completo del profilo di un giocatore tramite UID."""
    with get_db_connection(use_row_factory=True) as conn:
        c = conn.cursor()
        c.execute("SELECT * FROM players WHERE uid = ?", (uid,))
        row = c.fetchone()
        return dict(row) if row else None


def get_player_by_username(username):
    """Cerca un giocatore tramite il suo username di login (Area PRO)."""
    with get_db_connection(use_row_factory=True) as conn:
        c = conn.cursor()
        c.execute("SELECT * FROM players WHERE username = ?", (username,))
        row = c.fetchone()
        return dict(row) if row else None


def set_player_credentials(uid, username, password_hash):
    """Imposta username e password hash per l'accesso al portale di un giocatore PRO."""
    with get_db_connection() as conn:
        conn.execute(
            "UPDATE players SET username = ?, password_hash = ? WHERE uid = ?",
            (username, password_hash, uid)
        )
        conn.commit()


def rename_player(uid, new_name):
    """Aggiorna l'alias (callsign) di un operatore."""
    with get_db_connection() as conn:
        conn.execute("UPDATE players SET alias = ? WHERE uid = ?", (new_name, uid))
        conn.commit()


def delete_player_permanently(uid):
    """Elimina definitivamente un operatore dal database e dal roster attivo."""
    with get_db_connection() as conn:
        conn.execute("DELETE FROM players WHERE uid = ?", (uid,))
        conn.execute("DELETE FROM active_roster WHERE uid = ?", (uid,))
        conn.commit()


def update_player_notes(uid, notes):
    """Aggiorna le note tattiche associate a un operatore."""
    with get_db_connection() as conn:
        conn.execute("UPDATE players SET notes = ? WHERE uid = ?", (notes, uid))
        conn.commit()


def update_player_photo(uid, filename):
    """Associa il nome del file immagine ritagliato al profilo dell'operatore."""
    with get_db_connection() as conn:
        conn.execute("UPDATE players SET photo = ? WHERE uid = ?", (filename, uid))
        conn.commit()


def delete_player_photo(uid):
    """Rimuove il riferimento alla foto profilo dell'operatore."""
    with get_db_connection() as conn:
        conn.execute("UPDATE players SET photo = NULL WHERE uid = ?", (uid,))
        conn.commit()


def reset_player_stats(uid):
    """Azzera partite, vittorie, morti, karma e note di uno specifico operatore."""
    with get_db_connection() as conn:
        conn.execute(
            "UPDATE players SET games_played = 0, wins = 0, deaths = 0, karma = 0, notes = '' WHERE uid = ?",
            (uid,)
        )
        conn.commit()


def update_player_karma(uid, new_karma):
    """Imposta il valore di karma di un operatore."""
    with get_db_connection() as conn:
        conn.execute("UPDATE players SET karma = ? WHERE uid = ?", (new_karma, uid))
        conn.commit()


def update_player_asd(uid, asd_team):
    """Aggiorna il club/ASD di appartenenza di un operatore."""
    with get_db_connection() as conn:
        conn.execute("UPDATE players SET asd_team = ? WHERE uid = ?", (asd_team, uid))
        conn.commit()


def toggle_pro_status(uid):
    """Commuta lo stato dell'account tra GUEST (0) e PRO (1)."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT is_pro FROM players WHERE uid = ?", (uid,))
        row = c.fetchone()
        if not row:
            return
        if row[0] == 1:
            conn.execute(
                "UPDATE players SET is_pro = 0, username = NULL, password_hash = NULL WHERE uid = ?",
                (uid,)
            )
        else:
            conn.execute("UPDATE players SET is_pro = 1 WHERE uid = ?", (uid,))
        conn.commit()


def purge_guest_data():
    """Ripristina tutti gli account GUEST allo stato iniziale per un nuovo evento."""
    with get_db_connection() as conn:
        conn.execute(
            '''UPDATE players 
               SET alias = original_alias, games_played = 0, wins = 0, deaths = 0, 
                   karma = 0, notes = '', photo = NULL, asd_team = 'NESSUNA' 
               WHERE is_pro = 0'''
        )
        conn.commit()
        logger.info("[DB] Eseguita pulizia massiva (Purge) e ripristino per tutti i GUEST.")


def increment_death(uid):
    """Incrementa di 1 il contatore eliminazioni dell'operatore."""
    with get_db_connection() as conn:
        conn.execute("UPDATE players SET deaths = COALESCE(deaths, 0) + 1 WHERE uid = ?", (uid,))
        conn.commit()


def get_player_history(uid, limit=10):
    """Restituisce le ultime N partite disputate dall'operatore."""
    with get_db_connection(use_row_factory=True) as conn:
        c = conn.cursor()
        c.execute(
            "SELECT match_date, game_mode, result FROM match_history WHERE uid = ? ORDER BY match_date DESC LIMIT ?",
            (uid, limit)
        )
        return [dict(row) for row in c.fetchall()]


def process_match_stats(winner_state, game_mode=None):
    """
    Calcola e consolida nel database le statistiche di fine partita per tutti i
    giocatori presenti nel roster attivo e aggiorna le vittorie giornaliere di fazione.
    """
    raw_state = str(winner_state).strip().upper()
    logger.info(f"[DB] Analisi verdetto di fine partita per statistiche: '{raw_state}'")
    
    if raw_state in ['STOPPED', 'STANDBY', 'PREPARING']:
        logger.info("[DB] Nessuna partita valida salvata (Partita interrotta o in preparazione).")
        return
        
    winning_team = None
    config = get_factions_config()
    alpha_name = config.get('ALPHA', {}).get('name', 'ALPHA').upper()
    bravo_name = config.get('BRAVO', {}).get('name', 'BRAVO').upper()
    
    if raw_state == 'DRAW':
        winning_team = None
    elif "ALPHA" in raw_state or "T WINS" in raw_state or alpha_name in raw_state:
        winning_team = 'ALPHA'
    elif "BRAVO" in raw_state or "CT WINS" in raw_state or bravo_name in raw_state:
        winning_team = 'BRAVO'
        
    if not winning_team and raw_state != 'DRAW':
        logger.warning(f"[DB] Impossibile determinare la squadra vincente da '{raw_state}'.")
        return

    if not game_mode:
        try:
            import game_engine
            game_mode = getattr(game_engine.mission_control, 'mode', None)
        except Exception:
            game_mode = None
    mode_str = str(game_mode).upper() if game_mode else "UNKNOWN"

    try:
        with get_db_connection() as conn:
            c = conn.cursor()
            c.execute("SELECT uid, team FROM active_roster")
            players = c.fetchall()
            p_count = 0
            w_count = 0
            current_timestamp = get_current_time()
            
            for uid, team in players:
                p_count += 1
                c.execute("UPDATE players SET games_played = COALESCE(games_played, 0) + 1 WHERE uid = ?", (uid,))
                
                if winning_team:
                    if team == winning_team:
                        w_count += 1
                        c.execute("UPDATE players SET wins = COALESCE(wins, 0) + 1 WHERE uid = ?", (uid,))
                        player_result = "VITTORIA"
                    else:
                        player_result = "SCONFITTA"
                else:
                    player_result = "PAREGGIO"
                
                c.execute(
                    "INSERT INTO match_history (uid, match_date, game_mode, result) VALUES (?, ?, ?, ?)",
                    (uid, current_timestamp, mode_str, player_result)
                )
            
            if winning_team:
                c.execute(
                    "UPDATE factions_config SET daily_wins = COALESCE(daily_wins, 0) + 1 WHERE team_id = ?",
                    (winning_team,)
                )
            
            conn.commit()
            
            if winning_team:
                logger.info(f"[STATS SALVATE] +1 Partita a {p_count} giocatori | +1 Vittoria a {w_count} giocatori ({winning_team}).")
            else:
                logger.info(f"[STATS SALVATE] PAREGGIO: +1 Partita aggiunta a {p_count} giocatori.")
    except Exception as e:
        logger.error(f"[ERROR STATS] {e}")

# =============================================================================
# GESTIONE PRESET DI CONFIGURAZIONE MISSIONE
# =============================================================================

def get_all_presets():
    """Restituisce la lista di tutti i preset salvati ordinati per modalità e nome."""
    import json as _json
    with get_db_connection(use_row_factory=True) as conn:
        c = conn.cursor()
        c.execute("SELECT id, name, mode, config_json, updated_at FROM mission_presets ORDER BY mode, name")
        rows = c.fetchall()
        presets = []
        for r in rows:
            try:
                cfg = _json.loads(r['config_json']) if r['config_json'] else {}
            except Exception:
                cfg = {}
            presets.append({
                'id': r['id'],
                'name': r['name'],
                'mode': r['mode'],
                'config': cfg,
                'updated_at': r['updated_at']
            })
        return presets


def save_preset(name, mode, config_dict, preset_id=None):
    """Crea un nuovo preset o aggiorna uno esistente (per id o per nome)."""
    import json as _json
    clean_name = str(name or "").strip().upper()
    clean_mode = str(mode or "sd").strip().lower()
    if clean_mode not in ("sd", "dom", "tdm") or not clean_name:
        return None

    cfg_str = _json.dumps(config_dict or {})
    now_str = get_current_time()

    with get_db_connection() as conn:
        c = conn.cursor()
        if preset_id is not None:
            c.execute("SELECT id FROM mission_presets WHERE id = ?", (int(preset_id),))
            if c.fetchone():
                c.execute(
                    "UPDATE mission_presets SET name = ?, mode = ?, config_json = ?, updated_at = ? WHERE id = ?",
                    (clean_name, clean_mode, cfg_str, now_str, int(preset_id))
                )
                conn.commit()
                return int(preset_id)

        # Se esiste già un preset con lo stesso identico nome, lo aggiorna
        c.execute("SELECT id FROM mission_presets WHERE name = ?", (clean_name,))
        existing = c.fetchone()
        if existing:
            c.execute(
                "UPDATE mission_presets SET mode = ?, config_json = ?, updated_at = ? WHERE id = ?",
                (clean_mode, cfg_str, now_str, existing[0])
            )
            conn.commit()
            return existing[0]

        c.execute(
            "INSERT INTO mission_presets (name, mode, config_json, updated_at) VALUES (?, ?, ?, ?)",
            (clean_name, clean_mode, cfg_str, now_str)
        )
        new_id = c.lastrowid
        conn.commit()
        return new_id


def delete_preset(preset_id):
    """Elimina un preset dal database tramite il suo ID."""
    with get_db_connection() as conn:
        conn.execute("DELETE FROM mission_presets WHERE id = ?", (int(preset_id),))
        conn.commit()

# =============================================================================
# GESTIONALE GIORNATE DI GIOCO (CALENDARIO, PARTECIPANTI, SCALETTA E DEPLOY)
# =============================================================================

def get_total_guest_cards_count():
    """Restituisce il numero totale di tessere GUEST (is_pro = 0) registrate nel database."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT COUNT(*) FROM players WHERE COALESCE(is_pro, 0) = 0")
        row = c.fetchone()
        return row[0] if row else 0


def get_all_events():
    """Restituisce la lista di tutte le giornate programmate con i contatori logistici."""
    import json as _json
    total_guest_cards = get_total_guest_cards_count()
    with get_db_connection(use_row_factory=True) as conn:
        c = conn.cursor()
        c.execute('''
            SELECT e.*,
                   COUNT(ep.id) AS total_registered,
                   SUM(CASE WHEN ep.team = 'ALPHA' THEN 1 ELSE 0 END) AS alpha_count,
                   SUM(CASE WHEN ep.team = 'BRAVO' THEN 1 ELSE 0 END) AS bravo_count,
                   SUM(CASE WHEN ep.uid IS NULL OR ep.uid = '' THEN 1 ELSE 0 END) AS guest_count,
                   SUM(CASE WHEN ep.needs_rental = 1 THEN 1 ELSE 0 END) AS rental_count,
                   SUM(CASE WHEN ep.paid = 1 THEN 1 ELSE 0 END) AS paid_count
            FROM game_events e
            LEFT JOIN event_participants ep ON e.id = ep.event_id
            GROUP BY e.id
            ORDER BY e.event_date DESC, e.start_time DESC
        ''')
        rows = c.fetchall()
        events = []
        for r in rows:
            d = dict(r)
            try:
                d['schedule'] = _json.loads(d.get('schedule_json') or '[]')
            except Exception:
                d['schedule'] = []
            d.pop('schedule_json', None)
            d['total_registered']  = d['total_registered'] or 0
            d['alpha_count']       = d['alpha_count'] or 0
            d['bravo_count']       = d['bravo_count'] or 0
            d['guest_count']       = d['guest_count'] or 0
            d['rental_count']      = d['rental_count'] or 0
            d['paid_count']        = d['paid_count'] or 0
            d['total_guest_cards'] = total_guest_cards
            events.append(d)
        return events


def get_event_details(event_id):
    """Restituisce i dettagli completi di una giornata, inclusa la lista partecipanti arricchita dal DB."""
    import json as _json
    total_guest_cards = get_total_guest_cards_count()
    with get_db_connection(use_row_factory=True) as conn:
        c = conn.cursor()
        c.execute("SELECT * FROM game_events WHERE id = ?", (int(event_id),))
        ev_row = c.fetchone()
        if not ev_row:
            return None

        ev = dict(ev_row)
        try:
            ev['schedule'] = _json.loads(ev.get('schedule_json') or '[]')
        except Exception:
            ev['schedule'] = []
        ev.pop('schedule_json', None)
        ev['total_guest_cards'] = total_guest_cards

        c.execute('''
            SELECT ep.id, ep.event_id, ep.uid, ep.guest_name, ep.team,
                   ep.needs_rental, ep.paid, ep.notes,
                   p.alias AS db_alias, p.is_pro, p.asd_team,
                   p.games_played, p.wins
            FROM event_participants ep
            LEFT JOIN players p ON ep.uid = p.uid
            WHERE ep.event_id = ?
            ORDER BY COALESCE(p.alias, ep.guest_name)
        ''', (int(event_id),))

        participants = []
        for pr in c.fetchall():
            pd = dict(pr)
            games = pd.get('games_played') or 0
            wins  = pd.get('wins') or 0
            is_guest = not bool(pd.get('uid'))
            pd['is_guest']     = is_guest
            pd['display_name'] = pd.get('db_alias') or pd.get('guest_name') or 'OSPITE'
            pd['is_pro']       = 0 if is_guest else (pd.get('is_pro') or 0)
            pd['asd_team']     = pd.get('asd_team') or 'NESSUNA'
            pd['win_rate']     = round((wins / games) * 100) if games > 0 else 50
            if pd.get('team') not in ('ALPHA', 'BRAVO'):
                pd['team'] = 'ALPHA'
            participants.append(pd)

        ev['participants'] = participants
        return ev


def save_game_event(data):
    """Crea o aggiorna una giornata di gioco nel calendario."""
    event_id    = data.get('id')
    title       = str(data.get('title') or 'GIOCATA DOMENICALE').strip().upper()
    event_date  = str(data.get('event_date') or datetime.date.today().strftime("%Y-%m-%d")).strip()
    start_time  = str(data.get('start_time') or '08:30').strip()
    alpha_name  = str(data.get('alpha_name') or 'ALPHA').strip().upper()
    alpha_color = str(data.get('alpha_color') or '#ff3333').strip()
    bravo_name  = str(data.get('bravo_name') or 'BRAVO').strip().upper()
    bravo_color = str(data.get('bravo_color') or '#55ff55').strip()
    notes       = str(data.get('notes') or '').strip()

    with get_db_connection() as conn:
        c = conn.cursor()
        if event_id:
            c.execute('''
                UPDATE game_events
                SET title = ?, event_date = ?, start_time = ?,
                    alpha_name = ?, alpha_color = ?, bravo_name = ?, bravo_color = ?, notes = ?
                WHERE id = ?
            ''', (title, event_date, start_time,
                  alpha_name, alpha_color, bravo_name, bravo_color, notes, int(event_id)))
            conn.commit()
            return int(event_id)
        else:
            c.execute('''
                INSERT INTO game_events
                (title, event_date, start_time, status, max_players,
                 alpha_name, alpha_color, bravo_name, bravo_color, schedule_json, notes, created_at)
                VALUES (?, ?, ?, 'PLANNED', 0, ?, ?, ?, ?, '[]', ?, ?)
            ''', (title, event_date, start_time,
                  alpha_name, alpha_color, bravo_name, bravo_color, notes, get_current_time()))
            new_id = c.lastrowid
            conn.commit()
            return new_id


def delete_game_event(event_id):
    """Elimina una giornata di gioco e tutti i suoi partecipanti associati."""
    with get_db_connection() as conn:
        conn.execute("DELETE FROM event_participants WHERE event_id = ?", (int(event_id),))
        conn.execute("DELETE FROM game_events WHERE id = ?", (int(event_id),))
        conn.commit()


def save_event_schedule(event_id, schedule_list, notes=None):
    """Salva la scaletta delle modalità (Mission Playlist) ed eventuali note della giornata."""
    import json as _json
    sched_str = _json.dumps(schedule_list if isinstance(schedule_list, list) else [])
    with get_db_connection() as conn:
        if notes is not None:
            conn.execute("UPDATE game_events SET schedule_json = ?, notes = ? WHERE id = ?",
                         (sched_str, str(notes), int(event_id)))
        else:
            conn.execute("UPDATE game_events SET schedule_json = ? WHERE id = ?",
                         (sched_str, int(event_id)))
        conn.commit()


def add_event_participant(event_id, uid=None, guest_name=None, team='AUTO', needs_rental=0):
    """
    Aggiunge un partecipante (PRO con uid oppure Ospite senza uid) alla giornata.
    Gli ospiti vengono salvati solo con il loro nome e riceveranno una tessera GUEST
    fisica solo al momento dello schieramento nel roster di gioco.
    """
    ev_id = int(event_id)
    clean_uid = str(uid).strip() if uid and str(uid).strip() not in ('', 'AUTO_GUEST') else None
    clean_gname = str(guest_name or '').strip().upper()

    with get_db_connection() as conn:
        c = conn.cursor()

        if clean_uid:
            c.execute("SELECT id FROM event_participants WHERE event_id = ? AND uid = ?", (ev_id, clean_uid))
            if c.fetchone():
                return False

        # Assegna automaticamente alla colonna con meno giocatori se non specificato
        clean_team = str(team or 'AUTO').upper()
        if clean_team not in ('ALPHA', 'BRAVO'):
            c.execute('''
                SELECT SUM(CASE WHEN team = 'ALPHA' THEN 1 ELSE 0 END),
                       SUM(CASE WHEN team = 'BRAVO' THEN 1 ELSE 0 END)
                FROM event_participants WHERE event_id = ?
            ''', (ev_id,))
            counts = c.fetchone()
            a_cnt = (counts[0] or 0) if counts else 0
            b_cnt = (counts[1] or 0) if counts else 0
            clean_team = 'ALPHA' if a_cnt <= b_cnt else 'BRAVO'

        c.execute('''
            INSERT INTO event_participants (event_id, uid, guest_name, team, needs_rental, paid)
            VALUES (?, ?, ?, ?, ?, 0)
        ''', (ev_id, clean_uid, clean_gname, clean_team, 1 if needs_rental else 0))
        conn.commit()
        return True


def update_event_participant(participant_id, action, value=None):
    """Aggiorna squadra bozza (swap o set), flag noleggio o stato pagamento di un iscritto."""
    pid = int(participant_id)
    with get_db_connection() as conn:
        c = conn.cursor()
        if action == 'set_team' and value in ('ALPHA', 'BRAVO'):
            c.execute("UPDATE event_participants SET team = ? WHERE id = ?", (value, pid))
        elif action == 'swap_team':
            c.execute('''
                UPDATE event_participants
                SET team = CASE WHEN team = 'ALPHA' THEN 'BRAVO' ELSE 'ALPHA' END
                WHERE id = ?
            ''', (pid,))
        elif action == 'toggle_rental':
            c.execute("UPDATE event_participants SET needs_rental = CASE WHEN needs_rental = 1 THEN 0 ELSE 1 END WHERE id = ?", (pid,))
        elif action == 'toggle_paid':
            c.execute("UPDATE event_participants SET paid = CASE WHEN paid = 1 THEN 0 ELSE 1 END WHERE id = ?", (pid,))
        conn.commit()


def remove_event_participant(participant_id):
    """Rimuove un partecipante dalla giornata programmata."""
    with get_db_connection() as conn:
        conn.execute("DELETE FROM event_participants WHERE id = ?", (int(participant_id),))
        conn.commit()


def auto_balance_event_teams(event_id):
    """
    Suddivide automaticamente i partecipanti tra ALPHA e BRAVO:
    1. Raggruppa gli operatori PRO appartenenti alla stessa ASD/Squadra per non separare i compagni di club.
    2. Ordina i singoli e gli ospiti bilanciando il numero di operatori e il Win-Rate.
    """
    details = get_event_details(event_id)
    if not details or not details.get('participants'):
        return

    participants = details['participants']
    groups = {}
    solos = []

    for p in participants:
        asd = (p.get('asd_team') or 'NESSUNA').strip().upper()
        if asd and asd != 'NESSUNA':
            groups.setdefault(asd, []).append(p)
        else:
            solos.append(p)

    sorted_groups = sorted(groups.values(), key=lambda g: len(g), reverse=True)
    sorted_solos = sorted(solos, key=lambda x: (x.get('is_pro', 0), x.get('win_rate', 50)), reverse=True)

    alpha_list = []
    bravo_list = []
    alpha_score = 0.0
    bravo_score = 0.0

    for grp in sorted_groups:
        grp_score = sum(x.get('win_rate', 50) for x in grp)
        if len(alpha_list) < len(bravo_list) or (len(alpha_list) == len(bravo_list) and alpha_score <= bravo_score):
            alpha_list.extend(grp)
            alpha_score += grp_score
        else:
            bravo_list.extend(grp)
            bravo_score += grp_score

    for p in sorted_solos:
        w = p.get('win_rate', 50)
        if len(alpha_list) < len(bravo_list) or (len(alpha_list) == len(bravo_list) and alpha_score <= bravo_score):
            alpha_list.append(p)
            alpha_score += w
        else:
            bravo_list.append(p)
            bravo_score += w

    with get_db_connection() as conn:
        c = conn.cursor()
        for p in alpha_list:
            c.execute("UPDATE event_participants SET team = 'ALPHA' WHERE id = ?", (p['id'],))
        for p in bravo_list:
            c.execute("UPDATE event_participants SET team = 'BRAVO' WHERE id = ?", (p['id'],))
        conn.commit()


def batch_update_event_teams(event_id, team_map):
    """Aggiorna in blocco le squadre base dei partecipanti ({participant_id: 'ALPHA'|'BRAVO'})."""
    if not isinstance(team_map, dict):
        return
    with get_db_connection() as conn:
        c = conn.cursor()
        for pid_str, team_val in team_map.items():
            if team_val in ('ALPHA', 'BRAVO'):
                c.execute(
                    "UPDATE event_participants SET team = ? WHERE id = ? AND event_id = ?",
                    (team_val, int(pid_str), int(event_id))
                )
        conn.commit()


def deploy_event_to_active_roster(event_id, clear_existing=True, custom_team_map=None):
    """
    Importa i partecipanti della giornata nel Roster attivo (active_roster):
    - Se custom_team_map è presente (partita con squadre dedicate), usa la squadra specifica del round.
    - Assegna a ciascun Ospite una tessera GUEST libera e la salva in 'assigned_uid' così
      l'ospite mantiene la stessa identica tessera fisica anche rimescolando le squadre nei round successivi!
    """
    details = get_event_details(event_id)
    if not details:
        return {'imported': 0, 'missing_guests': 0}

    with get_db_connection() as conn:
        c = conn.cursor()

        # Conserva lo stato IN/FUORI se stiamo solo rimescolando le squadre a giornata già iniziata
        c.execute("SELECT uid, status FROM active_roster")
        previous_status = {r[0]: r[1] for r in c.fetchall()}

        if clear_existing:
            c.execute("DELETE FROM active_roster")

        if details.get('alpha_name') and details.get('alpha_color'):
            c.execute("UPDATE factions_config SET display_name = ?, hex_color = ? WHERE team_id = 'ALPHA'",
                      (details['alpha_name'], details['alpha_color']))
        if details.get('bravo_name') and details.get('bravo_color'):
            c.execute("UPDATE factions_config SET display_name = ?, hex_color = ? WHERE team_id = 'BRAVO'",
                      (details['bravo_name'], details['bravo_color']))

        # Tessere già riservate da altri ospiti di questo stesso evento
        reserved_uids = {
            p.get('assigned_uid') for p in details['participants']
            if p.get('assigned_uid')
        }

        c.execute('''
            SELECT uid FROM players
            WHERE COALESCE(is_pro, 0) = 0
              AND uid NOT IN (SELECT uid FROM active_roster)
            ORDER BY original_alias, uid
        ''')
        available_guest_uids = [r[0] for r in c.fetchall() if r[0] not in reserved_uids]

        imported = 0
        missing_guests = 0

        for p in details['participants']:
            pid_str = str(p['id'])
            if isinstance(custom_team_map, dict) and custom_team_map.get(pid_str) in ('ALPHA', 'BRAVO'):
                team = custom_team_map[pid_str]
            else:
                team = p.get('team') if p.get('team') in ('ALPHA', 'BRAVO') else 'ALPHA'

            uid = p.get('uid')
            if uid:
                st = previous_status.get(uid, 'FUORI')
                c.execute("INSERT OR REPLACE INTO active_roster (uid, team, status) VALUES (?, ?, ?)", (uid, team, st))
                imported += 1
            else:
                assigned_uid = p.get('assigned_uid')
                if assigned_uid:
                    c.execute("SELECT uid FROM players WHERE uid = ?", (assigned_uid,))
                    if not c.fetchone():
                        assigned_uid = None

                if not assigned_uid and available_guest_uids:
                    assigned_uid = available_guest_uids.pop(0)
                    c.execute("UPDATE event_participants SET assigned_uid = ? WHERE id = ?", (assigned_uid, p['id']))

                if assigned_uid:
                    g_name = (p.get('guest_name') or p.get('display_name') or 'OSPITE').strip().upper()
                    c.execute("UPDATE players SET alias = ? WHERE uid = ?", (g_name, assigned_uid))
                    st = previous_status.get(assigned_uid, 'FUORI')
                    c.execute("INSERT OR REPLACE INTO active_roster (uid, team, status) VALUES (?, ?, ?)", (assigned_uid, team, st))
                    imported += 1
                else:
                    missing_guests += 1

        c.execute("UPDATE game_events SET status = 'ACTIVE' WHERE id = ?", (int(event_id),))
        conn.commit()
        return {'imported': imported, 'missing_guests': missing_guests}