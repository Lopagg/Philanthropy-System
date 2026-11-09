/**
 * @file SearchDestroyMode.cpp
 * @brief Implementazione della macchina a stati, degli effetti LED (Breathing, Progresso,
 *        Esplosione scenica multi-stadio) e della telemetria per Search & Destroy.
 */

#include "GameModes/SearchDestroyMode.h"
#include "Interactions/RfidInteraction.h"

extern HardwareManager hardware;
extern NetworkManager network;
extern const char* DEVICE_ID;

static bool          s_explosionAnimActive = false;
static unsigned long s_explosionStartMs = 0;
static uint8_t       s_winnerColor[3] = { 0, 255, 0 };

// ============================================================================
// COSTRUTTORE, DISTRUTTORE E INIZIALIZZAZIONE
// ============================================================================

SearchDestroyMode::SearchDestroyMode()
    : _state(SD_SAFE),
      _lastTelemetryTime(0),
      _lastServerRetryTime(0),
      _pendingServerEvent(""),
      _armSequence("INNESCO", 1, 400, 1200, 255, 0, 0, EV_BTN_1_PRESSED),
      _defuseSequence("DISINNESCO", 2, 1200, 400, 0, 255, 0, EV_BTN_1_PRESSED),
      _bombTimer(3),
      _fxSmoke(false),
      _fxRelay(false),
      _fxPulseActive(false),
      _fxPulseStartTime(0) {
    _armSequence.setDefaultHoldStep(5);
    _defuseSequence.setDefaultHoldStep(10);
    _bombTimer.setDuration(600);
}

SearchDestroyMode::~SearchDestroyMode() {
    stopAllEffects();
    hardware.noTone();
}

void SearchDestroyMode::stopAllEffects() {
    _fxPulseActive = false;
    s_explosionAnimActive = false;
    hardware.setRelay(false);
    hardware.triggerSmoke(0);
}

void SearchDestroyMode::setOledLabels(const char* p1Label, const char* p2Label) {
    hardware.setOledLabels(p1Label ? String(p1Label) : "", p2Label ? String(p2Label) : "");
}

void SearchDestroyMode::begin() {
    stopAllEffects();
    _pendingServerEvent = "";
    enterSafeState();
    hardware.playAudio("/audio/common/match_start.mp3", AUDIO_CH_VOICE);
}

// ============================================================================
// TRANSIZIONI DI STATO E CONFIGURAZIONE JSON
// ============================================================================

void SearchDestroyMode::enterSafeState() {
    hardware.noTone();
    _state = SD_SAFE;
    hardware.clearLcd();
    hardware.printLcdLine(0, "PIANTA LA BOMBA", true);

    String keyHint;
    if (!_armSequence.areInitialKeysReady(keyHint)) {
        hardware.printLcdLine(2, "Prepara chiavi:", true);
        hardware.printLcdLine(3, keyHint, true);
    } else {
        hardware.printLcdLine(2, "Premi P1 per", true);
        hardware.printLcdLine(3, "iniziare innesco", true);
    }
    
    setOledLabels("INNESCA", "");
    hardware.updateBreathingEffect(0, 0, 255);
    sendTelemetry("SAFE");
}

void SearchDestroyMode::enterArmedState(bool playSound) {
    hardware.noTone();
    _state = SD_ARMED;
    hardware.printLcdLine(0, "BOMBA INNESCATA!", true);
    hardware.printLcdLine(1, "", false);
    hardware.printLcdLine(2, "", false);
    hardware.setLedsColor(255, 0, 0);

    if (playSound) {
        hardware.playAudio("/audio/sd/bomb_armed.mp3", AUDIO_CH_VOICE);
    }

    _bombTimer.forceRedraw();
    setOledLabels("", "DISINNESCA");
    sendTelemetry("ARMED");
}

void SearchDestroyMode::processJsonConfig(const char* jsonStr) {
    JsonDocument doc;
    if (deserializeJson(doc, jsonStr)) return;
    
    if (doc["cmd"] == "SET_SD_SETTINGS" || doc["cmd"] == "START_SD_GAME") {
        if (!doc["bomb_time"].isNull()) {
            _bombTimer.setDuration((doc["bomb_time"] | 10) * 60);
        }
        if (!doc["fx_smoke"].isNull()) _fxSmoke = doc["fx_smoke"] | false;
        if (!doc["fx_relay"].isNull()) _fxRelay = doc["fx_relay"] | false;
        
        if (doc["arm_seq"].is<JsonArray>()) {
            _armSequence.loadFromJson(doc["arm_seq"].as<JsonArray>(), 5);
        }
        if (doc["defuse_seq"].is<JsonArray>()) {
            _defuseSequence.loadFromJson(doc["defuse_seq"].as<JsonArray>(), 10);
        }
        if (!doc["arm_fail_mode"].isNull()) {
            _armSequence.setFailMode(InteractionSequence::parseFailMode(doc["arm_fail_mode"], false));
        }
        if (!doc["defuse_fail_mode"].isNull()) {
            _defuseSequence.setFailMode(InteractionSequence::parseFailMode(doc["defuse_fail_mode"], true));
        }
        Serial.println("[S&D] Sequenze e parametri configurati con successo.");
    }
}

// ============================================================================
// TELEMETRIA E GESTIONE ESITI (DISINNESCO / ESPLOSIONE)
// ============================================================================

void SearchDestroyMode::sendTelemetry(const char* statusStr) {
    String payload = "{\"id\":\"" + String(DEVICE_ID) + "\",\"type\":\"SD_UPDATE\",\"payload\":{";
    payload += "\"state\":\"" + String(statusStr) + "\"";
    
    if (_state == SD_ARMED || _state == SD_DEFUSING) {
        payload += ",\"bomb_time\":" + String(_bombTimer.getRemainingSeconds());
    } else {
        payload += ",\"bomb_time\":-1";
    }
    
    if (_state == SD_ARMING) {
        payload += ",\"arm_prog\":" + String(_armSequence.getProgress()) + ",\"def_prog\":0";
    } else if (_state == SD_DEFUSING) {
        payload += ",\"arm_prog\":0,\"def_prog\":" + String(_defuseSequence.getProgress());
    } else {
        payload += ",\"arm_prog\":0,\"def_prog\":0";
    }

    payload += "}}";
    network.sendTelemetry(payload);
}

void SearchDestroyMode::sendMatchEventToServer() {
    if (_pendingServerEvent.length() == 0) return;
    String payload = "{\"id\":\"" + String(DEVICE_ID) + "\",\"type\":\"MATCH_EVENT\",\"payload\":{\"event\":\"" + _pendingServerEvent + "\"}}";
    network.sendTelemetry(payload);
}

void SearchDestroyMode::triggerDefusal() {
    hardware.noTone();
    _bombTimer.stop();
    _state = SD_WAITING_SERVER;
    _pendingServerEvent = "BOMB_DEFUSED";
    _lastServerRetryTime = millis();

    hardware.clearLcd();
    hardware.printLcdLine(1, "BOMBA DISINNESCATA", true);
    hardware.printLcdLine(2, "Attesa Arbitro...", true);
    hardware.setLedsColor(0, 255, 0);
    hardware.playAudio("/audio/sd/bomb_defused.mp3", AUDIO_CH_VOICE);
    setOledLabels("", ""); 
    
    sendMatchEventToServer();
}

void SearchDestroyMode::triggerExplosion() {
    hardware.noTone();
    _bombTimer.stop();
    _state = SD_WAITING_SERVER;
    _pendingServerEvent = "BOMB_EXPLODED";
    _lastServerRetryTime = millis();

    hardware.clearLcd();
    hardware.printLcdLine(1, "BOMBA ESPLOSA!", true);
    hardware.printLcdLine(2, "Attesa Arbitro...", true);
    setOledLabels("", "");

    if (_fxSmoke) hardware.triggerSmoke(255);
    if (_fxRelay) hardware.setRelay(true);
    _fxPulseActive = true;
    _fxPulseStartTime = millis();

    s_explosionAnimActive = true;
    s_explosionStartMs = millis();
    hardware.setLedsColor(255, 255, 255);

    hardware.playAudio("/audio/sd/bomb_exploded.mp3", AUDIO_CH_SFX);
    
    sendMatchEventToServer();
}

// ============================================================================
// GESTIONE EVENTI E AGGIORNAMENTO CICLICO
// ============================================================================

void SearchDestroyMode::handleEvent(const SystemEvent& event) {
    if (event.type == EV_SERVER_COMMAND) {
        JsonDocument doc;
        if (!deserializeJson(doc, event.stringData)) {
            const char* cmd = doc["cmd"];
            if (cmd) {
                if (strcmp(cmd, "SET_SD_SETTINGS") == 0 || strcmp(cmd, "START_SD_GAME") == 0) {
                    processJsonConfig(event.stringData);
                }
                else if (strcmp(cmd, "FORCE_WIN") == 0) {
                    _fxPulseActive = false;
                    hardware.setRelay(false);
                    hardware.triggerSmoke(0);
                    hardware.noTone();
                    _bombTimer.stop();
                    _state = SD_ENDED;
                    _pendingServerEvent = "";

                    const char* reason = doc["reason"] | "FINE PARTITA";
                    const char* winnerName = doc["winner_name"] | (doc["winner"] | "SQUADRA");

                    hardware.clearLcd();
                    hardware.printLcdLine(0, "--- FINE PARTITA ---", true);
                    hardware.printLcdLine(1, String(reason), true);
                    hardware.printLcdLine(2, "VINCE SQUADRA:", true);
                    hardware.printLcdLine(3, String(winnerName), true);

                    if (doc["winner_color"].is<JsonArray>()) {
                        JsonArray col = doc["winner_color"].as<JsonArray>();
                        s_winnerColor[0] = col[0] | 0;
                        s_winnerColor[1] = col[1] | 0;
                        s_winnerColor[2] = col[2] | 0;
                    } else {
                        const char* legacyWinner = doc["winner"] | "";
                        if (strstr(legacyWinner, "ALPHA") != nullptr) {
                            s_winnerColor[0] = 255; s_winnerColor[1] = 0; s_winnerColor[2] = 0;
                        } else {
                            s_winnerColor[0] = 0; s_winnerColor[1] = 255; s_winnerColor[2] = 0;
                        }
                    }

                    if (!s_explosionAnimActive) {
                        hardware.setLedsColor(s_winnerColor[0], s_winnerColor[1], s_winnerColor[2]);
                    }

                    // Segnale acustico immediato per Tempo Scaduto / Squadra Eliminata
                    // (seguito subito dopo dall'annuncio vocale TTS generato dal server)
                    if (strcmp(reason, "TEMPO SCADUTO!") == 0 || strcmp(reason, "SQUADRA ELIMINATA!") == 0) {
                        hardware.playTone(880, 250);
                    }

                    setOledLabels("", "");
                }
                else if (strcmp(cmd, "FORCE_END_GAME") == 0) {
                    stopAllEffects();
                    hardware.noTone();
                    _bombTimer.stop();
                    _state = SD_ENDED;
                    _pendingServerEvent = "";
                    hardware.clearLcd();
                    hardware.printLcdLine(1, "PARTITA TERMINATA", true);
                    hardware.printLcdLine(2, "DALL'ARBITRO", true);
                    hardware.setLedsColor(0, 0, 0);
                    setOledLabels("", "");
                }
            }
        }
        return;
    }

    if (_state == SD_SAFE) {
        String keyHint;
        if (event.type == EV_BTN_1_PRESSED) {
            if (!_armSequence.areInitialKeysReady(keyHint)) {
                hardware.playTone(250, 400);
                hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
                hardware.printLcdLine(2, "CHIAVE FUORI POSTO", true);
                hardware.printLcdLine(3, keyHint, true);
                return;
            }
            _state = SD_ARMING;
            hardware.clearLcd();
            _armSequence.begin();
            sendTelemetry("ARMING...");
        } else if (_armSequence.canStartWithKeyEvent(event)) {
            _state = SD_ARMING;
            hardware.clearLcd();
            _armSequence.beginWithDirectKeyEvent(event);
            if (_armSequence.getResult() == INTERACTION_COMPLETED) {
                _bombTimer.start();
                enterArmedState(true);
            } else {
                sendTelemetry("ARMING...");
            }
        } else if (event.type == EV_RFID_SCANNED) {
            if (_armSequence.isFirstStepRfid()) {
                if (!_armSequence.areInitialKeysReady(keyHint)) {
                    RfidInteraction::sendRfidTelemetry(String(event.stringData), false, "SAFE_KEY_ERR");
                    hardware.playTone(250, 400);
                    hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
                    hardware.printLcdLine(2, "CHIAVE FUORI POSTO", true);
                    hardware.printLcdLine(3, keyHint, true);
                    return;
                }
                hardware.clearLcd();
                if (_armSequence.tryStartWithRfidEvent(event, "ARM_START")) {
                    if (_armSequence.getResult() == INTERACTION_COMPLETED) {
                        _bombTimer.start();
                        enterArmedState(true);
                    } else {
                        _state = SD_ARMING;
                        sendTelemetry("ARMING...");
                    }
                } else {
                    enterSafeState();
                    hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
                    hardware.printLcdLine(2, "TESSERA ERRATA!", true);
                }
            } else {
                RfidInteraction::sendRfidTelemetry(String(event.stringData), false, "SAFE_IDLE");
            }
        } else if (event.type == EV_KEY_1_STATE_CHANGED || event.type == EV_KEY_2_STATE_CHANGED) {
            enterSafeState();
        }
    } 
    else if (_state == SD_ARMING) {
        _armSequence.handleEvent(event);
    }
    else if (_state == SD_ARMED) {
        String keyHint;
        if (event.type == EV_BTN_2_PRESSED) {
            if (!_defuseSequence.areInitialKeysReady(keyHint)) {
                hardware.playTone(250, 400);
                hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
                hardware.printLcdLine(1, "CHIAVE FUORI POSTO", true);
                hardware.printLcdLine(2, keyHint, true);
                return;
            }
            _state = SD_DEFUSING;
            _defuseSequence.begin();
            sendTelemetry("DEFUSING...");
        } else if (_defuseSequence.canStartWithKeyEvent(event)) {
            _state = SD_DEFUSING;
            _defuseSequence.beginWithDirectKeyEvent(event);
            if (_defuseSequence.getResult() == INTERACTION_COMPLETED) {
                triggerDefusal();
            } else {
                sendTelemetry("DEFUSING...");
            }
        } else if (event.type == EV_RFID_SCANNED) {
            if (_defuseSequence.isFirstStepRfid()) {
                if (!_defuseSequence.areInitialKeysReady(keyHint)) {
                    RfidInteraction::sendRfidTelemetry(String(event.stringData), false, "ARMED_KEY_ERR");
                    hardware.playTone(250, 400);
                    hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
                    hardware.printLcdLine(1, "CHIAVE FUORI POSTO", true);
                    hardware.printLcdLine(2, keyHint, true);
                    return;
                }
                if (_defuseSequence.tryStartWithRfidEvent(event, "DEFUSE_START")) {
                    if (_defuseSequence.getResult() == INTERACTION_COMPLETED) {
                        triggerDefusal();
                    } else {
                        _state = SD_DEFUSING;
                        sendTelemetry("DEFUSING...");
                    }
                } else {
                    if (_defuseSequence.getFailMode() == SEQ_FAIL_EXPLODE) {
                        triggerExplosion();
                        return;
                    }
                    hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
                    hardware.printLcdLine(2, "TESSERA ERRATA!", true);
                }
            } else {
                RfidInteraction::sendRfidTelemetry(String(event.stringData), false, "ARMED_IDLE");
            }
        } else if (event.type == EV_KEY_1_STATE_CHANGED || event.type == EV_KEY_2_STATE_CHANGED) {
            hardware.printLcdLine(1, "", false);
            hardware.printLcdLine(2, "", false);
        }
    }
    else if (_state == SD_DEFUSING) {
        _defuseSequence.handleEvent(event);
    }
}

void SearchDestroyMode::update() {
    unsigned long nowMs = millis();

    // 1. Spegnimento asincrono dopo 500ms dell'impulso elettrico (fumo e relè 12V)
    if (_fxPulseActive && (nowMs - _fxPulseStartTime >= 500)) {
        _fxPulseActive = false;
        hardware.setRelay(false);
        hardware.triggerSmoke(0);
    }

    // 2. Animazione LED Scenica dell'Esplosione (ispirata al V1: cicli Bianco -> Arancione -> Rosso)
    if (s_explosionAnimActive) {
        unsigned long expEl = nowMs - s_explosionStartMs;
        if (expEl < 1610) {
            unsigned long phase = expEl % 230;
            if (phase < 50)       hardware.setLedsColor(255, 255, 255);
            else if (phase < 130) hardware.setLedsColor(255, 100, 0);
            else                  hardware.setLedsColor(255, 0, 0);
        } else if (expEl < 3000) {
            uint8_t greenFire = ((expEl / 90) % 2 == 0) ? 75 : 0;
            hardware.setLedsColor(255, greenFire, 0);
        } else {
            s_explosionAnimActive = false;
        }
    }

    // 3. Gestione Breathing LED in stato di fine round o attesa verdetto server
    if (_state == SD_ENDED && !s_explosionAnimActive) {
        hardware.updateBreathingEffect(s_winnerColor[0], s_winnerColor[1], s_winnerColor[2]);
    }

    if (_state == SD_WAITING_SERVER) {
        if (!s_explosionAnimActive) {
            if (_pendingServerEvent == "BOMB_DEFUSED") {
                hardware.updateBreathingEffect(0, 255, 0);
            } else {
                hardware.updateBreathingEffect(255, 0, 0);
            }
        }
        if (nowMs - _lastServerRetryTime >= 2000) {
            _lastServerRetryTime = nowMs;
            sendMatchEventToServer();
        }
        return;
    }

    // 4. Effetto Breathing Blu neutro in stato SD_SAFE
    if (_state == SD_SAFE) {
        hardware.updateBreathingEffect(0, 0, 255);
    }

    // 5. Invio telemetria periodica (10 Hz durante innesco/disinnesco, 1 Hz a riposo)
    bool isHighAction = (_state == SD_ARMING || _state == SD_DEFUSING);
    unsigned long telemetryInterval = isHighAction ? 100 : 1000;

    if (_state != SD_ENDED && (nowMs - _lastTelemetryTime >= telemetryInterval)) {
        _lastTelemetryTime = nowMs;
        if (_state == SD_SAFE) sendTelemetry("SAFE");
        else if (_state == SD_ARMING) sendTelemetry("ARMING...");
        else if (_state == SD_ARMED) sendTelemetry("ARMED");
        else if (_state == SD_DEFUSING) sendTelemetry("DEFUSING...");
    }

    // 6. Aggiornamento sequenza di innesco o disinnesco:
    //    InteractionSequence pilota direttamente i LED per TUTTI gli step (HOLD, PIN, KEY, RFID)
    //    senza mai tornare al colore precedente tra uno step e l'altro!
    if (_state == SD_ARMING) {
        _armSequence.update();
        InteractionResult res = _armSequence.getResult();
        if (res == INTERACTION_CANCELLED || res == INTERACTION_FAILED) {
            enterSafeState();
            return;
        } else if (res == INTERACTION_COMPLETED) {
            _bombTimer.start();
            enterArmedState(true);
            return;
        }
    }
    else if (_state == SD_DEFUSING) {
        _defuseSequence.update();
        InteractionResult res = _defuseSequence.getResult();
        if (res == INTERACTION_FAILED && _defuseSequence.getFailMode() == SEQ_FAIL_EXPLODE) {
            triggerExplosion();
            return;
        } else if (res == INTERACTION_CANCELLED || res == INTERACTION_FAILED) {
            enterArmedState(false);
            return;
        } else if (res == INTERACTION_COMPLETED) {
            triggerDefusal();
            return;
        }
    }

    // 7. Aggiornamento conto alla rovescia bomba:
    //    Durante SD_DEFUSING il timer aggiorna l'LCD e riproduce gli annunci e i beep in polifonia,
    //    mentre i LED restano sempre sotto il controllo di _defuseSequence.
    if (_state == SD_ARMED || _state == SD_DEFUSING) {
        bool allowTimerLedControl = (_state == SD_ARMED && !hardware.updateLedFlash());
        if (_bombTimer.update(allowTimerLedControl)) {
            triggerExplosion();
        }
    }
}