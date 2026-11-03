/**
 * @file SearchDestroyMode.cpp
 * @brief Implementazione della macchina a stati e della telemetria per Search & Destroy.
 */

#include "GameModes/SearchDestroyMode.h"
#include "Interactions/RfidInteraction.h"

extern HardwareManager hardware;
extern NetworkManager network;
extern const char* DEVICE_ID;

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
    hardware.playAudio("/audio/common/match_start.mp3");
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
    hardware.setLedsColor(0, 0, 255);
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
        hardware.playAudio("/audio/sd/bomb_armed.mp3");
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
    hardware.playAudio("/audio/sd/bomb_defused.mp3");
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

    // Attiva l'impulso non-bloccante (500ms) per fumo, relè 12V e flash bianco LED
    if (_fxSmoke) hardware.triggerSmoke(255);
    if (_fxRelay) hardware.setRelay(true);
    hardware.setLedsColor(255, 255, 255);
    _fxPulseActive = true;
    _fxPulseStartTime = millis();

    hardware.playAudio("/audio/sd/bomb_exploded.mp3");
    
    // Invia immediatamente l'evento al server (latenza zero)
    sendMatchEventToServer();
}

// ============================================================================
// GESTIONE EVENTI E AGGIORNAMENTO CICLICO
// ============================================================================

void SearchDestroyMode::handleEvent(const SystemEvent& event) {
    // 1. Comandi provenienti dal Server Centrale via MQTT
    if (event.type == EV_SERVER_COMMAND) {
        JsonDocument doc;
        if (!deserializeJson(doc, event.stringData)) {
            const char* cmd = doc["cmd"];
            if (cmd) {
                if (strcmp(cmd, "SET_SD_SETTINGS") == 0 || strcmp(cmd, "START_SD_GAME") == 0) {
                    processJsonConfig(event.stringData);
                }
                else if (strcmp(cmd, "FORCE_WIN") == 0) {
                    stopAllEffects();
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
                        hardware.setLedsColor(col[0] | 0, col[1] | 0, col[2] | 0);
                    } else {
                        const char* legacyWinner = doc["winner"] | "";
                        if (strstr(legacyWinner, "ALPHA") != nullptr) {
                            hardware.setLedsColor(255, 0, 0);
                        } else {
                            hardware.setLedsColor(0, 255, 0);
                        }
                    }

                    // Riproduce il suono di fine match se il round è terminato per Tempo Scaduto o Squadra Eliminata
                    if (strcmp(reason, "TEMPO SCADUTO!") == 0 || strcmp(reason, "SQUADRA ELIMINATA!") == 0) {
                        hardware.playAudio("/audio/common/match_end.mp3");
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

    // 2. Gestione Input Fisici in base allo stato della partita
    if (_state == SD_SAFE) {
        String keyHint;
        if (event.type == EV_BTN_1_PRESSED) {
            if (!_armSequence.areInitialKeysReady(keyHint)) {
                hardware.playTone(250, 400);
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
    // 1. Spegnimento asincrono dopo 500ms dell'impulso esplosione (fumo, relè 12V e flash bianco)
    if (_fxPulseActive && (millis() - _fxPulseStartTime >= 500)) {
        stopAllEffects();
        if (_state == SD_WAITING_SERVER) {
            hardware.setLedsColor(255, 0, 0);
        }
    }

    // 2. Retry QoS: se il RAT attende il verdetto dal server, reinvia l'evento ogni 2s
    if (_state == SD_WAITING_SERVER) {
        if (millis() - _lastServerRetryTime >= 2000) {
            _lastServerRetryTime = millis();
            sendMatchEventToServer();
        }
        return;
    }

    // 3. Invio telemetria periodica (10 Hz durante innesco/disinnesco, 1 Hz a riposo)
    bool isHighAction = (_state == SD_ARMING || _state == SD_DEFUSING);
    unsigned long telemetryInterval = isHighAction ? 100 : 1000;

    if (_state != SD_ENDED && (millis() - _lastTelemetryTime >= telemetryInterval)) {
        _lastTelemetryTime = millis();
        if (_state == SD_SAFE) sendTelemetry("SAFE");
        else if (_state == SD_ARMING) sendTelemetry("ARMING...");
        else if (_state == SD_ARMED) sendTelemetry("ARMED");
        else if (_state == SD_DEFUSING) sendTelemetry("DEFUSING...");
    }

    // 4. Aggiornamento sequenza di innesco o disinnesco
    if (_state == SD_ARMING) {
        _armSequence.update();
        if (_armSequence.getResult() == INTERACTION_CANCELLED) {
            enterSafeState();
            return;
        } else if (_armSequence.getResult() == INTERACTION_COMPLETED) {
            _bombTimer.start();
            enterArmedState(true);
            return;
        }
    }
    else if (_state == SD_DEFUSING) {
        _defuseSequence.update();
        if (_defuseSequence.getResult() == INTERACTION_CANCELLED) {
            enterArmedState(false);
            return;
        } else if (_defuseSequence.getResult() == INTERACTION_COMPLETED) {
            triggerDefusal();
            return;
        }
    }

    // 5. Aggiornamento conto alla rovescia bomba (attivo sia in SD_ARMED che in SD_DEFUSING)
    if (_state == SD_ARMED || _state == SD_DEFUSING) {
        if (_bombTimer.update(_state == SD_ARMED)) {
            triggerExplosion();
        }
    }
}