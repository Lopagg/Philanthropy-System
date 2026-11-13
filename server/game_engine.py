"""
game_engine.py
Motore di gioco autoritativo, controller di missione e client MQTT per Zulu Game System.

Architettura:
- Il server è l'unica autorità che decreta la vittoria (per tempo, eliminazione squadra o obiettivo).
- Gestisce i motori di modalità (SDEngine, DominationEngine, TDMEngine) con tick a 1 Hz in background.
- Mantiene il registro live dei nodi hardware connessi (DeviceRegistry) e sincronizza il Kiosk.
"""

import paho.mqtt.client as mqtt
import logging
import json
import time
from threading import Lock
import db_manager

logger = logging.getLogger(__name__)

socketio = None
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
mission_ready_flag = False
background_thread_started = False
_mqtt_started = False
tts_announcer_callback = None


# =============================================================================
# FUNZIONI DI SUPPORTO E COSTRUZIONE PAYLOAD MQTT
# =============================================================================

def get_pure_rgb(hex_code):
    """Converte un colore esadecimale (#RRGGBB) in una lista [R, G, B] per i LED dei nodi."""
    h = str(hex_code).lstrip('#').lower()
    if h == 'ff3333':
        return [255, 0, 0]
    if h == '55ff55':
        return [0, 255, 0]
    try:
        return [int(h[i:i+2], 16) for i in (0, 2, 4)]
    except Exception:
        return [255, 255, 255]


def build_force_win_payload(mc, winner_team_id, reason_str):
    """
    Costruisce il comando FORCE_WIN contenente sia i campi estesi per RAT-V2
    (nome custom fazione, colore RGB custom, motivo vittoria) sia il campo legacy per RAT-V1.
    Supporta anche il verdetto DRAW (pareggio).
    """
    if winner_team_id == "DRAW":
        return {
            "cmd": "FORCE_WIN",
            "winner": "DRAW",
            "winner_team_id": "DRAW",
            "winner_name": "PAREGGIO",
            "winner_color": [255, 153, 0],
            "reason": reason_str
        }

    config = db_manager.get_factions_config()
    team_cfg = config.get(winner_team_id, {})
    winner_custom_name = team_cfg.get('name', winner_team_id)
    default_hex = '#ff3333' if winner_team_id == 'ALPHA' else '#55ff55'
    winner_rgb = get_pure_rgb(team_cfg.get('color', default_hex))
    
    if mc.mode == 'sd':
        rat_legacy_winner = "ALPHA" if winner_team_id == mc.sd_attacker_team else "BRAVO"
    else:
        rat_legacy_winner = winner_team_id

    return {
        "cmd": "FORCE_WIN",
        "winner": rat_legacy_winner,
        "winner_team_id": winner_team_id,
        "winner_name": winner_custom_name,
        "winner_color": winner_rgb,
        "reason": reason_str
    }


SIREN_DURATIONS_MS = {
    "SIREN_LONG":  3000,  # 3 secondi
    "SIREN_SHORT": 2000   # 2 secondi
}


def trigger_siren(siren_type="SIREN_LONG"):
    """Invia in broadcast il comando di attivazione sirena da campo con durata esplicita (3s lunga, 2s corta)."""
    dur_ms = SIREN_DURATIONS_MS.get(siren_type, 3000)
    logger.info(f"[GAME ENGINE] Sirena attivata: {siren_type} ({dur_ms} ms)")
    try:
        mqtt_client.publish("zulu/cmd/broadcast", json.dumps({
            "cmd": siren_type,
            "duration_ms": dur_ms
        }))
    except Exception as e:
        logger.error(f"[SIREN ERROR] {e}")


def trigger_end_match_sequence(end_state):
    """Attiva la sirena di fine match, salva le statistiche su DB e aggiorna i client web."""
    logger.info(f"[GAME ENGINE] Sequenza di Fine Partita innescata: {end_state}")
    trigger_siren("SIREN_LONG")
    
    db_manager.process_match_stats(end_state, mission_control.mode)
    
    if socketio:
        socketio.emit('database_update', db_manager.get_all_players())
        socketio.emit('factions_update', db_manager.get_factions_config())


def trigger_victory_voice_announcement(mc, winner_team_id, reason_str):
    """
    Costruisce la frase vocale con il motivo della vittoria e il nome personalizzato
    della squadra vincente e avvia la generazione TTS asincrona verso il RAT-V2.
    """
    if not tts_announcer_callback or not mc.target_v2 or not mc.target_id or mc.target_id == "SERVER-TDM":
        return

    reason_clean = str(reason_str or "Fine partita").strip().capitalize()
    if not reason_clean.endswith(("!", ".")):
        reason_clean += "!"

    if winner_team_id == "DRAW":
        if "pareggio" in reason_clean.lower():
            speech_text = "Tempo scaduto! Partita terminata in pareggio!"
        else:
            speech_text = f"Fine partita! {reason_clean} Partita in pareggio!"
    else:
        config = db_manager.get_factions_config()
        winner_name = config.get(winner_team_id, {}).get('name', winner_team_id)
        speech_text = f"{reason_clean} Vince la squadra {winner_name}!"

    try:
        tts_announcer_callback(mc.target_id, speech_text)
    except Exception as e:
        logger.error(f"[VICTORY TTS ERROR] {e}")


def normalize_uid_str(raw_str):
    """Normalizza una lista di UID separati da virgola rimuovendo spazi e due punti."""
    if not raw_str:
        return ""
    parts = [p.replace(":", "").replace(" ", "").strip().upper() for p in str(raw_str).split(",")]
    clean = [p for p in parts if p]
    return ",".join(clean)


def resolve_rfid_sequence_for_rat(seq_list, team_id):
    """
    Traduce le regole ad alto livello degli step RFID (TEAM, PLAYERS, CUSTOM, ANY)
    in una stringa di UID esadecimali separati da virgola pronta per la validazione su RAT-V2.
    """
    if not isinstance(seq_list, list):
        return []
    roster_rows = db_manager.get_active_roster_status()
    team_uids = [r[0].replace(":", "").replace(" ", "").strip().upper() for r in roster_rows if r[1] == team_id]

    resolved = []
    for step in seq_list:
        s_copy = dict(step)
        if s_copy.get("type") == "RFID":
            rfid_mode = s_copy.get("rfid_mode", "ANY")
            if rfid_mode == "TEAM":
                s_copy["val_str"] = ",".join(team_uids) if team_uids else "__EMPTY_TEAM__"
            elif rfid_mode == "PLAYERS":
                norm = normalize_uid_str(s_copy.get("val_str", ""))
                s_copy["val_str"] = norm if norm else "__EMPTY_PLAYERS__"
            elif rfid_mode == "CUSTOM":
                norm = normalize_uid_str(s_copy.get("val_str", ""))
                s_copy["val_str"] = norm if norm else "__EMPTY_CUSTOM__"
            else:
                s_copy["val_str"] = "*"
            s_copy.pop("rfid_mode", None)
            s_copy.pop("player_names", None)
        resolved.append(s_copy)
    return resolved


def publish_sd_settings_to_target(target_id, cmd_obj):
    """Invia il pacchetto SET_SD_SETTINGS formattato in base alla versione del terminale (V1 o V2)."""
    if not target_id or target_id in ["SERVER-ROUTER", "SERVER-TDM"]:
        return
    if "arm_seq" in cmd_obj:
        attacker_team = cmd_obj.get("attacker", "ALPHA")
        defender_team = "BRAVO" if attacker_team == "ALPHA" else "ALPHA"
        v2_payload = {
            "cmd": "SET_SD_SETTINGS",
            "bomb_time": int(cmd_obj.get("bomb_time", 10)),
            "fx_smoke": bool(cmd_obj.get("fx_smoke", False)),
            "fx_relay": bool(cmd_obj.get("fx_relay", False)),
            "arm_fail_mode": str(cmd_obj.get("arm_fail_mode", "RETRY_STEP")),
            "defuse_fail_mode": str(cmd_obj.get("defuse_fail_mode", "RETRY_STEP")),
            "arm_seq": resolve_rfid_sequence_for_rat(cmd_obj.get("arm_seq", []), attacker_team),
            "defuse_seq": resolve_rfid_sequence_for_rat(cmd_obj.get("defuse_seq", []), defender_team)
        }
        mqtt_client.publish(f"zulu/cmd/{target_id}", json.dumps(v2_payload), qos=1)
    else:
        use_arm = str(cmd_obj.get("use_arm_pin", "true")).lower() == "true"
        use_defuse = str(cmd_obj.get("use_defuse_pin", "true")).lower() == "true"
        flat_payload = {
            "cmd": "SET_SD_SETTINGS",
            "bomb_time": int(cmd_obj.get("bomb_time", 10)),
            "arm_time": int(cmd_obj.get("arm_time", 5)),
            "defuse_time": int(cmd_obj.get("defuse_time", 10)),
            "arm_pin": str(cmd_obj.get("arm_pin", "1234")),
            "defuse_pin": str(cmd_obj.get("defuse_pin", "4321")),
            "use_arm_pin": use_arm,
            "use_defuse_pin": use_defuse
        }
        mqtt_client.publish(f"zulu/cmd/{target_id}", json.dumps(flat_payload), qos=1)


def publish_dom_settings_to_target(target_id, cmd_obj):
    """Invia il pacchetto SET_DOM_SETTINGS formattato per RAT-V2 (sequenze modulari) o RAT-V1 (legacy)."""
    if not target_id or target_id in ["SERVER-ROUTER", "SERVER-TDM"]:
        return
    if "alpha_seq" in cmd_obj or "bravo_seq" in cmd_obj:
        config = db_manager.get_factions_config()
        a_seq = cmd_obj.get("alpha_seq", [{"type": "HOLD", "val_num": 10}])
        b_seq = cmd_obj.get("bravo_seq", a_seq)
        v2_payload = {
            "cmd": "SET_DOM_SETTINGS",
            "duration": int(cmd_obj.get("duration", cmd_obj.get("game_duration", 15))),
            "fx_smoke": bool(cmd_obj.get("fx_smoke", False)),
            "fx_relay": bool(cmd_obj.get("fx_relay", False)),
            "alpha_fail_mode": str(cmd_obj.get("alpha_fail_mode", "RETRY_STEP")),
            "bravo_fail_mode": str(cmd_obj.get("bravo_fail_mode", "RETRY_STEP")),
            "t1": config.get('ALPHA', {}).get('name', 'ALPHA'),
            "t1_c": get_pure_rgb(config.get('ALPHA', {}).get('color', '#ff3333')),
            "t2": config.get('BRAVO', {}).get('name', 'BRAVO'),
            "t2_c": get_pure_rgb(config.get('BRAVO', {}).get('color', '#55ff55')),
            "alpha_seq": resolve_rfid_sequence_for_rat(a_seq, "ALPHA"),
            "bravo_seq": resolve_rfid_sequence_for_rat(b_seq, "BRAVO")
        }
        mqtt_client.publish(f"zulu/cmd/{target_id}", json.dumps(v2_payload), qos=1)
    else:
        flat_payload = {
            "cmd": "SET_DOM_SETTINGS",
            "duration": int(cmd_obj.get("duration", cmd_obj.get("game_duration", 15))),
            "capture_time": int(cmd_obj.get("capture_time", 10)),
            "countdown": int(cmd_obj.get("countdown", 10))
        }
        mqtt_client.publish(f"zulu/cmd/{target_id}", json.dumps(flat_payload), qos=1)


# =============================================================================
# MOTORI DELLE MODALITÀ DI GIOCO (BASE, TDM, SEARCH & DESTROY, DOMINATION)
# =============================================================================

class BaseGameMode:
    """Interfaccia base per i motori di modalità eseguiti sul server."""
    def __init__(self, mission_ctrl):
        self.mc = mission_ctrl

    def tick(self, alpha_in, bravo_in):
        pass

    def handle_event(self, device_id, payload):
        pass


class TDMEngine(BaseGameMode):
    """Motore autoritativo per la modalità Team Deathmatch (solo server + Kiosk)."""
    def __init__(self, mission_ctrl):
        super().__init__(mission_ctrl)
        self.alpha_zero_ticks = 0
        self.bravo_zero_ticks = 0
        self.grace_period = 5

    def tick(self, alpha_in, bravo_in):
        mc = self.mc
        if mc.global_time_left > 0:
            mc.global_time_left -= 1
        match_ends = False
        end_state_str = "ACTIVE"
        
        self.alpha_zero_ticks = (self.alpha_zero_ticks + 1) if alpha_in == 0 else 0
        self.bravo_zero_ticks = (self.bravo_zero_ticks + 1) if bravo_in == 0 else 0
        
        if mc.global_time_left <= 0:
            match_ends = True
            if alpha_in > bravo_in:
                end_state_str = "ALPHA WINS"
            elif bravo_in > alpha_in:
                end_state_str = "BRAVO WINS"
            else:
                end_state_str = "DRAW"
        elif self.alpha_zero_ticks >= self.grace_period and self.bravo_zero_ticks >= self.grace_period:
            match_ends = True
            end_state_str = "DRAW"
        elif self.alpha_zero_ticks >= self.grace_period and bravo_in > 0:
            match_ends = True
            end_state_str = "BRAVO WINS"
        elif self.bravo_zero_ticks >= self.grace_period and alpha_in > 0:
            match_ends = True
            end_state_str = "ALPHA WINS"
            
        if match_ends:
            mc.state = end_state_str
            trigger_end_match_sequence(end_state_str)
            doc = {
                "id": "SERVER-TDM",
                "type": "TDM_UPDATE",
                "payload": {"mode": "TEAM_DEATHMATCH", "state": end_state_str, "game_time": mc.global_time_left}
            }
            if socketio:
                socketio.emit('esp_event', {"parsed_data": doc})
            mqtt_client.publish("zulu/telemetry/SERVER-TDM", json.dumps(doc), qos=1)
        elif mc.state != "STANDBY":
            doc = {
                "id": "SERVER-TDM",
                "type": "TDM_UPDATE",
                "payload": {"mode": "TEAM_DEATHMATCH", "state": mc.state, "game_time": mc.global_time_left}
            }
            if socketio:
                socketio.emit('esp_event', {"parsed_data": doc})
            mqtt_client.publish("zulu/telemetry/SERVER-TDM", json.dumps(doc), qos=1)


class SDEngine(BaseGameMode):
    """
    Motore autoritativo per Search & Destroy.
    Gestisce il tempo globale, l'eliminazione delle squadre (tenendo conto di eventuale
    bomba già innescata) e gli eventi MATCH_EVENT (BOMB_EXPLODED / BOMB_DEFUSED) inviati dal RAT.
    """
    def __init__(self, mission_ctrl):
        super().__init__(mission_ctrl)
        self.alpha_zero_ticks = 0
        self.bravo_zero_ticks = 0
        self.grace_period = 5
        self.bomb_armed = False
        self.last_win_cmd = None

    def _conclude_match(self, winner_team_id, reason_str, bomb_time_override=None):
        """Invia il verdetto FORCE_WIN al terminale RAT, salva le statistiche e aggiorna la UI."""
        mc = self.mc
        end_state_str = f"{winner_team_id} WINS"
        mc.state = end_state_str
        cmd_to_send = build_force_win_payload(mc, winner_team_id, reason_str)
        self.last_win_cmd = cmd_to_send

        try:
            mqtt_client.publish(f"zulu/cmd/{mc.target_id}", json.dumps(cmd_to_send), qos=1)
        except Exception as e:
            logger.error(f"[MQTT ERROR] Impossibile inviare verdetto FORCE_WIN: {e}")

        registry.update_telemetry(mc.target_id, {"state": end_state_str}, end_state_str)
        trigger_end_match_sequence(end_state_str)
        trigger_victory_voice_announcement(mc, winner_team_id, reason_str)

        doc = {
            "id": mc.target_id,
            "type": "SD_UPDATE",
            "payload": {
                "mode": "SEARCH_AND_DESTROY",
                "state": end_state_str,
                "game_time": mc.global_time_left,
                "bomb_time": bomb_time_override
            }
        }
        if socketio:
            socketio.emit('esp_event', {"parsed_data": doc})

    def tick(self, alpha_in, bravo_in):
        mc = self.mc
        if mc.global_time_left > 0:
            mc.global_time_left -= 1
            
        winner_team_id = None
        reason_str = ""
        
        self.alpha_zero_ticks = (self.alpha_zero_ticks + 1) if alpha_in == 0 else 0
        self.bravo_zero_ticks = (self.bravo_zero_ticks + 1) if bravo_in == 0 else 0

        if mc.global_time_left <= 0:
            winner_team_id = mc.sd_defender_team
            reason_str = "TEMPO SCADUTO!"
        elif self.alpha_zero_ticks >= self.grace_period and bravo_in > 0:
            if not (mc.sd_attacker_team == "ALPHA" and self.bomb_armed):
                winner_team_id = "BRAVO"
                reason_str = "SQUADRA ELIMINATA!"
        elif self.bravo_zero_ticks >= self.grace_period and alpha_in > 0:
            if not (mc.sd_attacker_team == "BRAVO" and self.bomb_armed):
                winner_team_id = "ALPHA"
                reason_str = "SQUADRA ELIMINATA!"
            
        if winner_team_id:
            self._conclude_match(winner_team_id, reason_str, bomb_time_override=None)
        else:
            doc = {
                "id": mc.target_id,
                "type": "TIME_UPDATE",
                "payload": {
                    "mode": "SEARCH_AND_DESTROY",
                    "game_time": mc.global_time_left
                }
            }
            if socketio:
                socketio.emit('esp_event', {"parsed_data": doc})

    def handle_event(self, device_id, payload):
        mc = self.mc
        # Se il RAT ritrasmette MATCH_EVENT per QoS dopo che il round è già stato chiuso,
        # gli ritrasmettiamo l'ultimo comando FORCE_WIN per sbloccare la sua schermata.
        if mc.state != "ACTIVE":
            if self.last_win_cmd:
                try:
                    mqtt_client.publish(f"zulu/cmd/{mc.target_id}", json.dumps(self.last_win_cmd), qos=1)
                except Exception:
                    pass
            return
            
        event_name = payload.get('event')
        if event_name == 'BOMB_EXPLODED':
            self._conclude_match(mc.sd_attacker_team, "BOMBA ESPLOSA!", bomb_time_override=0)
        elif event_name == 'BOMB_DEFUSED':
            self._conclude_match(mc.sd_defender_team, "BOMBA DISINNESCATA!", bomb_time_override=None)


class DominationEngine(BaseGameMode):
    """
    Motore autoritativo per la modalità Dominio (Domination).
    - Conta a 1 Hz il tempo di possesso di ciascuna fazione (score_a, score_b).
    - Riceve dal RAT lo stato di cattura/possesso (NEUTRAL, CAPTURING A/B, OWNED ALPHA/BRAVO).
    - Decreta la vittoria per scadenza tempo (maggior possesso) o per eliminazione squadra.
    """
    def __init__(self, mission_ctrl):
        super().__init__(mission_ctrl)
        self.alpha_zero_ticks = 0
        self.bravo_zero_ticks = 0
        self.grace_period = 5
        self.owner = "NEUTRAL"       # "NEUTRAL", "ALPHA", "BRAVO"
        self.display_state = "NEUTRAL"
        self.capture_prog = 0
        self.score_a = 0
        self.score_b = 0
        self.last_win_cmd = None

    def set_zone_owner(self, team_id):
        """Imposta la fazione proprietaria della zona ('ALPHA' o 'BRAVO')."""
        if team_id in ["ALPHA", "BRAVO"]:
            self.owner = team_id
            self.display_state = f"OWNED {team_id}"
            self.capture_prog = 0
            logger.info(f"[DOMINATION] Zona conquistata da {team_id}! (A:{self.score_a}s B:{self.score_b}s)")

    def sync_from_rat_telemetry(self, payload):
        """Sincronizza lo stato di cattura e il proprietario a partire da DOM_UPDATE del RAT."""
        raw_state = str(payload.get("state", "")).upper()
        if "CAPTURE_PROG" in payload or "capture_prog" in payload:
            self.capture_prog = int(payload.get("capture_prog") or 0)

        if "OWNED ALPHA" in raw_state or "ALPHA OWNED" in raw_state:
            self.owner = "ALPHA"
            self.display_state = "OWNED ALPHA"
            self.capture_prog = 0
        elif "OWNED BRAVO" in raw_state or "BRAVO OWNED" in raw_state:
            self.owner = "BRAVO"
            self.display_state = "OWNED BRAVO"
            self.capture_prog = 0
        elif "CAPTURING A" in raw_state:
            self.display_state = "CAPTURING A..."
        elif "CAPTURING B" in raw_state:
            self.display_state = "CAPTURING B..."
        elif raw_state == "NEUTRAL":
            self.owner = "NEUTRAL"
            self.display_state = "NEUTRAL"
            self.capture_prog = 0

        if not self.mc.target_v2:
            if isinstance(payload.get("score_a"), int) and payload["score_a"] > self.score_a:
                self.score_a = payload["score_a"]
            if isinstance(payload.get("score_b"), int) and payload["score_b"] > self.score_b:
                self.score_b = payload["score_b"]

    def _conclude_match(self, winner_team_id, reason_str):
        """Chiude la partita di Dominio, invia FORCE_WIN al RAT, suona la sirena e salva le statistiche."""
        mc = self.mc
        end_state_str = "DRAW" if winner_team_id == "DRAW" else f"{winner_team_id} WINS"
        mc.state = end_state_str
        cmd_to_send = build_force_win_payload(mc, winner_team_id, reason_str)
        cmd_to_send["score_a"] = self.score_a
        cmd_to_send["score_b"] = self.score_b
        self.last_win_cmd = cmd_to_send

        try:
            mqtt_client.publish(f"zulu/cmd/{mc.target_id}", json.dumps(cmd_to_send), qos=1)
        except Exception as e:
            logger.error(f"[MQTT ERROR] Impossibile inviare FORCE_WIN Domination: {e}")

        registry.update_telemetry(mc.target_id, {
            "state": end_state_str,
            "score_a": self.score_a,
            "score_b": self.score_b,
            "capture_prog": 0
        }, end_state_str)

        trigger_end_match_sequence(end_state_str)
        trigger_victory_voice_announcement(mc, winner_team_id, reason_str)

        doc = {
            "id": mc.target_id,
            "type": "DOM_UPDATE",
            "payload": {
                "mode": "DOMINATION",
                "state": end_state_str,
                "game_time": mc.global_time_left,
                "score_a": self.score_a,
                "score_b": self.score_b,
                "capture_prog": 0
            }
        }
        if socketio:
            socketio.emit('esp_event', {"parsed_data": doc})

    def tick(self, alpha_in, bravo_in):
        mc = self.mc
        if mc.global_time_left > 0:
            mc.global_time_left -= 1

        # Incrementa il tempo di possesso della squadra che detiene attualmente la zona
        if self.owner == "ALPHA":
            self.score_a += 1
        elif self.owner == "BRAVO":
            self.score_b += 1

        self.alpha_zero_ticks = (self.alpha_zero_ticks + 1) if alpha_in == 0 else 0
        self.bravo_zero_ticks = (self.bravo_zero_ticks + 1) if bravo_in == 0 else 0

        winner_team_id = None
        reason_str = ""

        if mc.global_time_left <= 0:
            if self.score_a > self.score_b:
                winner_team_id = "ALPHA"
                reason_str = "TEMPO SCADUTO!"
            elif self.score_b > self.score_a:
                winner_team_id = "BRAVO"
                reason_str = "TEMPO SCADUTO!"
            else:
                winner_team_id = "DRAW"
                reason_str = "PAREGGIO!"
        elif self.alpha_zero_ticks >= self.grace_period and self.bravo_zero_ticks >= self.grace_period:
            winner_team_id = "DRAW"
            reason_str = "SQUADRE ELIMINATE!"
        elif self.alpha_zero_ticks >= self.grace_period and bravo_in > 0:
            winner_team_id = "BRAVO"
            reason_str = "SQUADRA ELIMINATA!"
        elif self.bravo_zero_ticks >= self.grace_period and alpha_in > 0:
            winner_team_id = "ALPHA"
            reason_str = "SQUADRA ELIMINATA!"

        if winner_team_id:
            self._conclude_match(winner_team_id, reason_str)
        else:
            doc = {
                "id": mc.target_id,
                "type": "DOM_UPDATE",
                "payload": {
                    "mode": "DOMINATION",
                    "state": self.display_state,
                    "game_time": mc.global_time_left,
                    "score_a": self.score_a,
                    "score_b": self.score_b,
                    "capture_prog": self.capture_prog
                }
            }
            if socketio:
                socketio.emit('esp_event', {"parsed_data": doc})

    def handle_event(self, device_id, payload):
        mc = self.mc
        if mc.state != "ACTIVE":
            if self.last_win_cmd:
                try:
                    mqtt_client.publish(f"zulu/cmd/{mc.target_id}", json.dumps(self.last_win_cmd), qos=1)
                except Exception:
                    pass
            return

        event_name = payload.get('event')
        if event_name == 'ZONE_CAPTURED_ALPHA':
            self.set_zone_owner("ALPHA")
        elif event_name == 'ZONE_CAPTURED_BRAVO':
            self.set_zone_owner("BRAVO")
        elif event_name == 'ZONE_NEUTRAL':
            self.owner = "NEUTRAL"
            self.display_state = "NEUTRAL"
            self.capture_prog = 0


# =============================================================================
# CONTROLLER CENTRALE DI MISSIONE E REGISTRO DISPOSITIVI
# =============================================================================

class MissionController:
    """Coordina l'avvio, l'arresto e lo stato globale della missione in corso."""
    def __init__(self):
        self.lock = Lock()
        self.active_mission = False
        self.mode = None
        self.target_id = None
        self.state = "STANDBY"
        self.global_duration = 0
        self.global_time_left = 0
        self.sd_attacker_team = "ALPHA"
        self.sd_defender_team = "BRAVO"
        self.sd_settings = {}
        self.active_engine = None
        self.target_v2 = False

    def start_mission(self, mode, target_id, duration=15):
        with self.lock:
            self.active_mission = True
            self.mode = mode
            self.target_id = target_id
            self.state = "ACTIVE"
            self.global_duration = int(duration) * 60
            self.global_time_left = int(duration) * 60
            
            if target_id and target_id in registry.devices:
                self.target_v2 = "V2" in registry.devices[target_id].get("version", "") or "V2" in target_id
            else:
                self.target_v2 = "V2" in str(target_id)
            
            if mode == 'sd':
                self.sd_attacker_team = self.sd_settings.get('attacker', "ALPHA")
                self.sd_defender_team = "BRAVO" if self.sd_attacker_team == "ALPHA" else "ALPHA"
                self.active_engine = SDEngine(self)
            elif mode == 'dom':
                self.active_engine = DominationEngine(self)
            elif mode == 'tdm':
                self.active_engine = TDMEngine(self)
            else:
                self.active_engine = BaseGameMode(self)
                
            logger.info(f"[MISSION CONTROL] Missione Avviata: {mode.upper()} su {target_id} (Durata: {duration}m, V2: {self.target_v2})")

        if mode == 'tdm':
            if socketio:
                rules_payload = dict(self.sd_settings) if isinstance(self.sd_settings, dict) else {}
                rules_payload["mode"] = "TEAM_DEATHMATCH"
                rules_payload["game_duration"] = duration
                socketio.emit('esp_event', {"parsed_data": {"id": "SERVER-TDM", "type": "MODE_ENTER", "payload": rules_payload}})
                socketio.emit('esp_event', {"parsed_data": {"id": "SERVER-TDM", "type": "SETTINGS_UPDATE", "payload": rules_payload}})
            trigger_siren("SIREN_LONG")
            publish_kiosk_led_sync()
            return

        if target_id and target_id != "SERVER-TDM":
            try:
                config = db_manager.get_factions_config()
                t1_id = self.sd_attacker_team if mode == 'sd' else 'ALPHA'
                t2_id = self.sd_defender_team if mode == 'sd' else 'BRAVO'

                faction_payload = {
                    "cmd": "SET_FACTIONS",
                    "t1": config.get(t1_id, {}).get('name', t1_id),
                    "t1_c": get_pure_rgb(config.get(t1_id, {}).get('color', '#ff3333')),
                    "t2": config.get(t2_id, {}).get('name', t2_id),
                    "t2_c": get_pure_rgb(config.get(t2_id, {}).get('color', '#55ff55'))
                }
                mqtt_client.publish(f"zulu/cmd/{target_id}", json.dumps(faction_payload), qos=1)

                if mode == 'sd' and self.sd_settings and not self.target_v2:
                    publish_sd_settings_to_target(target_id, self.sd_settings)
                elif mode == 'dom' and self.sd_settings and not self.target_v2:
                    publish_dom_settings_to_target(target_id, self.sd_settings)
                
                start_payload = {"cmd": "START_SD_GAME" if mode == 'sd' else "START_DOM_GAME"}
                if mode == 'sd' and self.sd_settings:
                    start_payload["bomb_time"] = int(self.sd_settings.get("bomb_time", 10))
                    start_payload["fx_smoke"] = bool(self.sd_settings.get("fx_smoke", False))
                    start_payload["fx_relay"] = bool(self.sd_settings.get("fx_relay", False))
                    start_payload["arm_fail_mode"] = str(self.sd_settings.get("arm_fail_mode", "RETRY_STEP"))
                    start_payload["defuse_fail_mode"] = str(self.sd_settings.get("defuse_fail_mode", "RETRY_STEP"))
                    if "arm_seq" in self.sd_settings:
                        start_payload["arm_seq"] = resolve_rfid_sequence_for_rat(self.sd_settings.get("arm_seq", []), self.sd_attacker_team)
                    if "defuse_seq" in self.sd_settings:
                        start_payload["defuse_seq"] = resolve_rfid_sequence_for_rat(self.sd_settings.get("defuse_seq", []), self.sd_defender_team)
                elif mode == 'dom' and self.sd_settings:
                    start_payload["duration"] = int(duration)
                    start_payload["fx_smoke"] = bool(self.sd_settings.get("fx_smoke", False))
                    start_payload["fx_relay"] = bool(self.sd_settings.get("fx_relay", False))
                    start_payload["alpha_fail_mode"] = str(self.sd_settings.get("alpha_fail_mode", "RETRY_STEP"))
                    start_payload["bravo_fail_mode"] = str(self.sd_settings.get("bravo_fail_mode", "RETRY_STEP"))
                    start_payload["t1"] = config.get('ALPHA', {}).get('name', 'ALPHA')
                    start_payload["t1_c"] = get_pure_rgb(config.get('ALPHA', {}).get('color', '#ff3333'))
                    start_payload["t2"] = config.get('BRAVO', {}).get('name', 'BRAVO')
                    start_payload["t2_c"] = get_pure_rgb(config.get('BRAVO', {}).get('color', '#55ff55'))
                    if "alpha_seq" in self.sd_settings:
                        a_seq = self.sd_settings.get("alpha_seq", [{"type": "HOLD", "val_num": 10}])
                        b_seq = self.sd_settings.get("bravo_seq", a_seq)
                        start_payload["alpha_seq"] = resolve_rfid_sequence_for_rat(a_seq, "ALPHA")
                        start_payload["bravo_seq"] = resolve_rfid_sequence_for_rat(b_seq, "BRAVO")

                mqtt_client.publish(f"zulu/cmd/{target_id}", json.dumps(start_payload), qos=1)
                
                initial_state = "SAFE" if mode == 'sd' else "NEUTRAL"
                registry.update_telemetry(target_id, {"state": initial_state}, initial_state)
                trigger_siren("SIREN_LONG")
                publish_kiosk_led_sync()

                if socketio:
                    mode_name = "SEARCH_AND_DESTROY" if mode == 'sd' else "DOMINATION"
                    rules_payload = dict(self.sd_settings) if isinstance(self.sd_settings, dict) else {}
                    rules_payload["mode"] = mode_name
                    rules_payload["game_duration"] = duration
                    
                    socketio.emit('esp_event', {"parsed_data": {"id": target_id, "type": "MODE_ENTER", "payload": rules_payload}})
                    socketio.emit('esp_event', {"parsed_data": {"id": target_id, "type": "SETTINGS_UPDATE", "payload": rules_payload}})
                    socketio.emit('esp_event', {"parsed_data": {
                        "id": target_id,
                        "type": "SD_UPDATE" if mode == 'sd' else "DOM_UPDATE",
                        "payload": {
                            "mode": mode_name,
                            "state": initial_state,
                            "game_time": self.global_time_left,
                            "bomb_time": None,
                            "score_a": 0,
                            "score_b": 0,
                            "capture_prog": 0,
                            "arm_prog": 0,
                            "def_prog": 0
                        }
                    }})
            except Exception as e:
                logger.error(f"[START MISSION SYNC ERROR] {e}")

    def force_stop_mission(self):
        with self.lock:
            if not self.active_mission or self.state == "STOPPED":
                return
            self.state = "STOPPED"
            rt = self.target_id
            mode = self.mode
            time_left = self.global_time_left

        trigger_end_match_sequence("STOPPED")
        publish_kiosk_led_sync()

        if rt and rt != "SERVER-TDM":
            try:
                mqtt_client.publish(f"zulu/cmd/{rt}", json.dumps({"cmd": "FORCE_END_GAME"}), qos=1)
            except Exception:
                pass
            registry.update_telemetry(rt, {"state": "STOPPED"}, "STOPPED")

        if socketio:
            msg_type = "TDM_UPDATE" if mode == 'tdm' else ("SD_UPDATE" if mode == 'sd' else "DOM_UPDATE")
            mode_str = "TEAM_DEATHMATCH" if mode == 'tdm' else ("SEARCH_AND_DESTROY" if mode == 'sd' else "DOMINATION")
            doc = {
                "id": rt or "SERVER",
                "type": msg_type,
                "payload": {
                    "mode": mode_str,
                    "state": "STOPPED",
                    "game_time": time_left,
                    "bomb_time": None,
                    "arm_prog": 0,
                    "def_prog": 0
                }
            }
            socketio.emit('esp_event', {"parsed_data": doc})

    def close_and_dismiss_mission(self):
        with self.lock:
            rt = self.target_id
            self.active_mission = False
            self.state = "STANDBY"
            self.active_engine = None
            logger.info("[MISSION CONTROL] Missione Chiusa Definitivamente.")

        publish_kiosk_led_sync()

        if rt and rt != "SERVER-TDM":
            try:
                mqtt_client.publish(f"zulu/cmd/{rt}", json.dumps({"cmd": "RETURN_TO_MENU"}), qos=1)
            except Exception:
                pass
            registry.update_device(rt, "MODE_EXIT")

        if socketio:
            socketio.emit('esp_event', {"parsed_data": {"id": rt or "SERVER", "type": "MODE_EXIT"}})
            active_devs, _ = registry.get_active_devices()
            socketio.emit('devices_update', active_devs)

    def end_mission(self):
        with self.lock:
            if not self.active_mission:
                return False
            self.active_mission = False
            self.state = "STANDBY"
            self.active_engine = None
            logger.info("[MISSION CONTROL] Missione Chiusa Definitivamente.")
            return True

    def is_authorized_node(self, device_id):
        with self.lock:
            if not self.active_mission:
                return False
            return (self.mode == 'tdm' or device_id == self.target_id)


mission_control = MissionController()


class DeviceRegistry:
    """Mantiene lo stato di presenza, versione, IP e telemetria di tutti i nodi MQTT attivi."""
    def __init__(self):
        self.devices = {}
        self.timeout_seconds = 15
        self.lock = Lock()

    def update_device(self, device_id, msg_type, mode=None, version=None, ip=None, batt_v=None, batt_pct=None, volume=None):
        now = time.time()
        changed = False
        with self.lock:
            if device_id not in self.devices:
                meta = db_manager.get_device_meta(device_id)
                display_name = meta['alias'] if meta['alias'] else device_id
                saved_vol = meta['volume'] if meta['volume'] is not None else (volume if volume is not None else 100)
                self.devices[device_id] = {
                    "id": device_id,
                    "name": display_name,
                    "photo": meta['photo'],
                    "volume": saved_vol,
                    "type": "NODE",
                    "mode": "BOOTING...",
                    "version": "Unknown",
                    "ip": "MQTT",
                    "batt_v": 0.0,
                    "batt_pct": -1,
                    "game_state": "STANDBY",
                    "telemetry": {}
                }
                changed = True
            
            dev = self.devices[device_id]
            dev["last_seen"] = now
            dev["status"] = "ONLINE"
            if ip and dev.get("ip") != ip:
                dev["ip"] = ip
                changed = True
            if version and dev.get("version") != version:
                dev["version"] = version
                changed = True
            if batt_v is not None and dev.get("batt_v") != batt_v:
                dev["batt_v"] = batt_v
                changed = True
            if batt_pct is not None and dev.get("batt_pct") != batt_pct:
                dev["batt_pct"] = batt_pct
                changed = True
            if volume is not None and dev.get("volume") != volume:
                dev["volume"] = int(volume)
                changed = True
            if mode and dev.get("mode") != mode:
                dev["mode"] = mode
                changed = True
            elif msg_type == "MODE_EXIT" and dev.get("mode") != "MAIN MENU":
                dev["mode"] = "MAIN MENU"
                dev["game_state"] = "STANDBY"
                dev["telemetry"] = {}
                changed = True
        return changed

    def set_device_volume(self, device_id, volume):
        vol = max(0, min(100, int(volume)))
        with self.lock:
            db_manager.set_device_volume(device_id, vol)
            if device_id in self.devices:
                self.devices[device_id]["volume"] = vol
                return True
        return False

    def set_device_photo(self, device_id, filename):
        with self.lock:
            db_manager.set_device_photo(device_id, filename)
            if device_id in self.devices:
                self.devices[device_id]["photo"] = filename
                return True
        return False

    def update_telemetry(self, device_id, payload, new_state=None):
        changed = False
        with self.lock:
            if device_id in self.devices:
                self.devices[device_id]["telemetry"].update(payload)
                if new_state and self.devices[device_id]["game_state"] != new_state:
                    self.devices[device_id]["game_state"] = new_state
                    changed = True
        return changed

    def rename_device(self, device_id, new_name):
        with self.lock:
            if device_id in self.devices:
                self.devices[device_id]["name"] = new_name
                db_manager.set_device_alias(device_id, new_name)
                return True
        return False

    def get_active_devices(self):
        now = time.time()
        to_remove = []
        active_list = []
        needs_update = False
        with self.lock:
            for d_id, data in self.devices.items():
                if now - data["last_seen"] > self.timeout_seconds:
                    to_remove.append(d_id)
                else:
                    active_list.append(data)
            for d_id in to_remove:
                del self.devices[d_id]
                needs_update = True
        return active_list, needs_update


registry = DeviceRegistry()


# =============================================================================
# GESTIONE CALLBACK MQTT E THREAD DI BACKGROUND
# =============================================================================

def on_mqtt_connect(client, userdata, flags, rc):
    logger.info(f"[MQTT] Connesso al Broker con codice: {rc}")
    client.subscribe("zulu/telemetry/#")


def publish_kiosk_led_sync():
    """Sincronizza immediatamente lo stato dei LED del Kiosk (tutti IN CAMPO, stato partita e colori fazione)."""
    try:
        players = db_manager.get_active_roster_status()
        total_players = len(players)
        players_out = sum(1 for p in players if p[2] != 'IN')
        all_in = (total_players > 0 and players_out == 0)

        with mission_control.lock:
            is_running = (mission_control.active_mission and mission_control.state in ["PREPARING", "ACTIVE"])
            m_state = mission_control.state if mission_control.active_mission else "STANDBY"

        factions = db_manager.get_factions_config()
        a_rgb = get_pure_rgb(factions['ALPHA']['color'])
        b_rgb = get_pure_rgb(factions['BRAVO']['color'])

        sync_json = json.dumps({
            "cmd": "KIOSK_LED_SYNC",
            "all_players_in": all_in,
            "game_running": is_running,
            "game_ready": mission_ready_flag,
            "mission_state": m_state,
            "t1": factions['ALPHA']['name'],
            "t1_c": a_rgb,
            "t2": factions['BRAVO']['name'],
            "t2_c": b_rgb
        })
        mqtt_client.publish("zulu/cmd/NODE-KIOSK-1", sync_json)
        mqtt_client.publish("zulu/cmd/broadcast", sync_json)
    except Exception as e:
        logger.error(f"[KIOSK SYNC ERROR] {e}")


def notify_kiosk_checkin(team, status):
    """Invia al Kiosk il comando per riprodurre suono e animazione LED anche per azioni da Web/Mobile."""
    try:
        action_str = "IN_ALL" if team == "ALL" else f"{status}_{team}"
        mqtt_client.publish("zulu/cmd/broadcast", json.dumps({
            "cmd": "KIOSK_REPLY",
            "action": action_str
        }))
        publish_kiosk_led_sync()
    except Exception as e:
        logger.error(f"[KIOSK NOTIFY ERROR] {e}")


def on_mqtt_message(client, userdata, msg):
    global mission_ready_flag
    if not socketio:
        return
    try:
        payload_str = msg.payload.decode('utf-8')
        parsed = json.loads(payload_str)
        device_id = parsed.get('id')
        msg_type = parsed.get('type')
        payload = parsed.get('payload', {})
        
        if msg_type not in ['HEARTBEAT', 'TIME_UPDATE', 'DOM_UPDATE', 'SD_UPDATE', 'TERMINAL_INPUT']:
            logger.info(f"[MQTT RX] {device_id} -> TYPE: {msg_type} | PAYLOAD: {payload}")
        
        if not device_id:
            return

        # 1. Comandi fisici provenienti dal Kiosk
        if msg_type == 'KIOSK_CMD':
            cmd_str = payload.get('cmd')
            if cmd_str:
                if cmd_str == "GAME_START":
                    can_override_start = not mission_control.active_mission or mission_control.state not in ["ACTIVE", "PREPARING"]
                    if mission_ready_flag and can_override_start:
                        target = mission_control.target_id
                        mode = mission_control.mode
                        dur = int(mission_control.global_duration / 60) if mission_control.global_duration > 0 else 15
                        roster_rows = db_manager.get_active_roster_status()
                        players_out = sum(1 for p in roster_rows if p[2] != 'IN')
                        terminal_ok = (mode == 'tdm') or (target and target in registry.devices)

                        if terminal_ok and len(roster_rows) > 0 and players_out == 0 and mode:
                            logger.info(f"[KIOSK OVERRIDE] Avvio Hardware Missione: {mode} su {target}")
                            mission_control.start_mission(mode, target, duration=dur)
                            socketio.emit('mission_start_success')
                        else:
                            logger.warning("[KIOSK OVERRIDE BLOCKED] Tentativo di avvio rifiutato: terminale non valido o giocatori fuori campo.")
                elif cmd_str == "FORCE_END_GAME":
                    mission_control.force_stop_mission()
                elif cmd_str == "CLOSE_MISSION":
                    mission_control.close_and_dismiss_mission()

                socketio.emit('kiosk_hardware_cmd', {'cmd': cmd_str})
            return

        # 2. Scansione e assegnazione tag dal Kiosk
        elif msg_type == 'TAG_SCANNED':
            uid = payload.get('uid')
            reply_action = "UNKNOWN"
            if uid:
                socketio.emit('raw_tag_scanned', {'uid': uid})
                row = db_manager.get_player_status(uid)
                if row:
                    new_status = db_manager.toggle_player_status(uid, row[0])
                    reply_action = f"{new_status}_{row[1]}"
                    if new_status == 'FUORI':
                        with mission_control.lock:
                            is_active = (mission_control.active_mission and mission_control.state == "ACTIVE")
                        if is_active:
                            db_manager.increment_death(uid)
                            logger.info(f"[DEATH] Operatore {uid} eliminato (uscito durante partita attiva).")
                    socketio.emit('roster_update', db_manager.get_full_roster())
                else:
                    reply_action = "WAIT_ASSIGN"
            mqtt_client.publish(f"zulu/cmd/{device_id}", json.dumps({"cmd": "KIOSK_REPLY", "action": reply_action}), qos=1)
            publish_kiosk_led_sync()

        elif msg_type == 'TAG_ASSIGN':
            if payload.get('uid') and payload.get('team'):
                db_manager.register_player(payload.get('uid'), payload.get('team'))
                socketio.emit('roster_update', db_manager.get_full_roster())
                publish_kiosk_led_sync()

        # 3. Aggiornamento Registro Dispositivi e Sincronizzazione Volume
        mode = payload.get('mode') or parsed.get('mode')
        version = payload.get('version') or parsed.get('version')
        ip = payload.get('ip') or parsed.get('ip')
        batt_v = payload.get('batt_v') if 'batt_v' in payload else parsed.get('batt_v')
        batt_pct = payload.get('batt_pct') if 'batt_pct' in payload else parsed.get('batt_pct')
        rep_vol = payload.get('volume') if 'volume' in payload else parsed.get('volume')

        meta = db_manager.get_device_meta(device_id)
        db_vol = meta.get('volume')
        if rep_vol is not None and db_vol is not None and int(rep_vol) != int(db_vol):
            # Il server ha un volume salvato per questo nodo diverso da quello attuale del RAT: lo riallinea subito!
            mqtt_client.publish(f"zulu/cmd/{device_id}", json.dumps({"cmd": "SET_VOLUME", "volume": int(db_vol)}), qos=1)
            rep_vol = int(db_vol)
        elif rep_vol is not None and db_vol is None:
            db_manager.set_device_volume(device_id, int(rep_vol))

        has_changed = registry.update_device(device_id, msg_type, mode, version, ip, batt_v, batt_pct, rep_vol)

        # Aggiorna RSSI e Heap se presenti nell'HEARTBEAT
        hb_rssi = payload.get('rssi') if 'rssi' in payload else parsed.get('rssi')
        hb_heap = payload.get('heap_kb') if 'heap_kb' in payload else parsed.get('heap_kb')
        if hb_rssi is not None or hb_heap is not None:
            tel_patch = {}
            if hb_rssi is not None: tel_patch["rssi"] = hb_rssi
            if hb_heap is not None: tel_patch["heap_kb"] = hb_heap
            registry.update_telemetry(device_id, tel_patch)

        # 3B. Diagnostica di Rete (Ping/Pong RTT e stato RSSI/Heap)
        if msg_type == 'NET_PING':
            pong_payload = {
                "cmd": "NET_PONG",
                "client_ts": payload.get("client_ts", 0)
            }
            mqtt_client.publish(f"zulu/cmd/{device_id}", json.dumps(pong_payload), qos=0)
            registry.update_telemetry(device_id, {
                "rssi": payload.get("rssi"),
                "heap_kb": payload.get("heap_kb")
            })
            return

        if msg_type == 'NET_DIAG_REPORT':
            registry.update_telemetry(device_id, {
                "rtt_ms": payload.get("rtt_ms"),
                "rssi": payload.get("rssi"),
                "heap_kb": payload.get("heap_kb")
            })
            active_devs, _ = registry.get_active_devices()
            socketio.emit('devices_update', active_devs)
            socketio.emit('net_diag_result', {
                "id": device_id,
                "rtt_ms": payload.get("rtt_ms"),
                "rssi": payload.get("rssi"),
                "heap_kb": payload.get("heap_kb")
            })
            return

        # 4. Telemetria Scansioni RFID dal RAT-V2
        if msg_type == 'RFID_SCAN':
            scanned_uid = payload.get('uid', '')
            accepted = payload.get('accepted', False)
            context_str = payload.get('context', 'STEP')
            player_profile = db_manager.get_player_profile(scanned_uid) if scanned_uid else None
            alias_str = player_profile['alias'] if player_profile else "KEYCARD / NON REGISTRATA"
            logger.info(f"[RAT RFID SCAN] Nodo: {device_id} | UID: {scanned_uid} ({alias_str}) | Contesto: {context_str} | Valida: {accepted}")
            registry.update_telemetry(device_id, {
                "last_rfid_uid": scanned_uid,
                "last_rfid_alias": alias_str,
                "last_rfid_accepted": accepted,
                "last_rfid_context": context_str
            })
            socketio.emit('esp_event', {"parsed_data": {
                "id": device_id,
                "type": "RFID_SCAN",
                "payload": {
                    "uid": scanned_uid,
                    "alias": alias_str,
                    "accepted": accepted,
                    "context": context_str
                }
            }})
            return

        # 5. Eventi Critici di Fine Partita dal RAT (BOMB_EXPLODED / BOMB_DEFUSED / ZONE_CAPTURED)
        if msg_type == 'MATCH_EVENT':
            if mission_control.is_authorized_node(device_id) and mission_control.active_engine:
                mission_control.active_engine.handle_event(device_id, payload)
            return

        # 6. Telemetria di Avanzamento Partita (SD_UPDATE / DOM_UPDATE)
        if msg_type in ['SD_UPDATE', 'DOM_UPDATE']:
            if mission_control.is_authorized_node(device_id):
                if mission_control.state in ['ALPHA WINS', 'BRAVO WINS', 'DRAW', 'STOPPED']:
                    return

                raw_state = payload.get('state', '')
                if mission_control.mode == 'sd':
                    is_armed = raw_state in ['ARMED', 'DEFUSING...']
                    if isinstance(mission_control.active_engine, SDEngine):
                        mission_control.active_engine.bomb_armed = is_armed
                    if not is_armed:
                        payload['bomb_time'] = None

                    if raw_state == 'T WINS':
                        raw_state = f"{mission_control.sd_attacker_team} WINS"
                    elif raw_state == 'CT WINS':
                        raw_state = f"{mission_control.sd_defender_team} WINS"
                    payload['state'] = raw_state

                elif mission_control.mode == 'dom' and isinstance(mission_control.active_engine, DominationEngine):
                    mission_control.active_engine.sync_from_rat_telemetry(payload)
                    payload['score_a'] = mission_control.active_engine.score_a
                    payload['score_b'] = mission_control.active_engine.score_b
                    payload['state'] = mission_control.active_engine.display_state
                    raw_state = payload['state']

                payload['game_time'] = mission_control.global_time_left
                old_state = registry.devices[device_id].get("game_state", "STANDBY")

                if registry.update_telemetry(device_id, payload, raw_state):
                    if raw_state in ['ALPHA WINS', 'BRAVO WINS', 'DRAW', 'STOPPED'] and old_state not in ['ALPHA WINS', 'BRAVO WINS', 'DRAW', 'STOPPED']:
                        mission_control.state = raw_state
                        trigger_end_match_sequence(raw_state)

                parsed['payload'] = payload
                socketio.emit('esp_event', {"parsed_data": parsed})
        
        if has_changed:
            active_devs, _ = registry.get_active_devices()
            socketio.emit('devices_update', active_devs)

    except Exception as e:
        logger.error(f"[MQTT ERROR] Eccezione parsing: {e}")


mqtt_client.on_connect = on_mqtt_connect
mqtt_client.on_message = on_mqtt_message


def start_mqtt(sio_instance):
    """Avvia il loop di rete MQTT collegandosi al broker locale."""
    global socketio, _mqtt_started
    socketio = sio_instance
    if _mqtt_started:
        return
    try:
        mqtt_client.connect("127.0.0.1", 1883, 60)
        mqtt_client.loop_start()
        _mqtt_started = True
    except Exception as e:
        logger.error(f"[CRITICAL MQTT] {e}")


def background_cleanup(fetch_weather_callback):
    """
    Task di background eseguito a 1 Hz:
    - Aggiorna il tick del motore di gioco attivo.
    - Sincronizza lo stato dei LED del Kiosk.
    - Rimuove dal DeviceRegistry i nodi andati in timeout (>15s).
    - Aggiorna il meteo ogni 10s.
    """
    global mission_ready_flag
    tick_counter = 0
    while True:
        if socketio:
            socketio.sleep(1)
        tick_counter += 1
        
        players = db_manager.get_active_roster_status()
        alpha_in = sum(1 for p in players if p[1] == 'ALPHA' and p[2] == 'IN')
        bravo_in = sum(1 for p in players if p[1] == 'BRAVO' and p[2] == 'IN')
        
        with mission_control.lock:
            game_is_running = mission_control.active_mission and mission_control.state in ["ACTIVE", "PREPARING"]
            
        if game_is_running:
            mission_ready_flag = False

        publish_kiosk_led_sync()

        with mission_control.lock:
            if mission_control.active_mission and mission_control.state == "ACTIVE":
                if mission_control.active_engine:
                    mission_control.active_engine.tick(alpha_in, bravo_in)

        if tick_counter % 2 == 0:
            active_devs, rem = registry.get_active_devices()
            if rem and socketio:
                socketio.emit('devices_update', active_devs)
        
        if tick_counter % 10 == 0:
            weather_data = fetch_weather_callback()
            if socketio:
                socketio.emit('weather_update', {'weather': weather_data})
            
            with mission_control.lock:
                if mission_control.active_mission and mission_control.state == "ACTIVE" and mission_control.target_id and not mission_control.target_v2:
                    if mission_control.mode in ['sd', 'dom']:
                        sync_payload = {
                            "cmd": "SYNC_TIME",
                            "global_time_left": mission_control.global_time_left
                        }
                        try:
                            mqtt_client.publish(f"zulu/cmd/{mission_control.target_id}", json.dumps(sync_payload), qos=0)
                        except Exception as e:
                            logger.error(f"[SYNC ERROR] {e}")