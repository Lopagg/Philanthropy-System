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
    """Restituisce un dizionario {'alias': ..., 'photo': ...} per il dispositivo specificato."""
    with get_db_connection(use_row_factory=True) as conn:
        c = conn.cursor()
        c.execute("SELECT alias, photo FROM device_aliases WHERE device_id = ?", (device_id,))
        row = c.fetchone()
        if row:
            return {'alias': row['alias'], 'photo': row['photo']}
        return {'alias': None, 'photo': None}


def get_device_alias(device_id):
    return get_device_meta(device_id)['alias']


def set_device_alias(device_id, alias):
    """Salva o aggiorna l'alias mantenendo eventuale foto già associata al dispositivo."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT device_id FROM device_aliases WHERE device_id = ?", (device_id,))
        if c.fetchone():
            c.execute("UPDATE device_aliases SET alias = ? WHERE device_id = ?", (alias, device_id))
        else:
            c.execute("INSERT INTO device_aliases (device_id, alias, photo) VALUES (?, ?, NULL)", (device_id, alias))
        conn.commit()


def set_device_photo(device_id, filename):
    """Associa o rimuove (filename=None) la foto personalizzata di un nodo hardware."""
    with get_db_connection() as conn:
        c = conn.cursor()
        c.execute("SELECT device_id FROM device_aliases WHERE device_id = ?", (device_id,))
        if c.fetchone():
            c.execute("UPDATE device_aliases SET photo = ? WHERE device_id = ?", (filename, device_id))
        else:
            c.execute("INSERT INTO device_aliases (device_id, alias, photo) VALUES (?, ?, ?)", (device_id, device_id, filename))
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