/**
 * @file DominationMode.cpp
 * @brief Implementazione della modalità Dominio (Domination) per RAT-V2 con:
 *        - Audio polifonico (zona contesa, zona conquistata, avvio match e TTS vittoria)
 *        - Effetto Breathing LED continuo (Ciano in neutro, colore fazione in possesso/vittoria)
 *        - Transizione cromatica progressiva tra il colore del possessore attuale e l'attaccante
 *        - Gestione penalità errore step (RETRY_STEP / RESTART_SEQ).
 */

#include "GameModes/DominationMode.h"
#include "Interactions/RfidInteraction.h"

extern HardwareManager hardware;
extern NetworkManager network;
extern const char* DEVICE_ID;

// ============================================================================
// COSTRUTTORE, DISTRUTTORE E INIZIALIZZAZIONE
// ============================================================================

DominationMode::DominationMode()
    : _state(DOM_NEUTRAL),
      _owner(OWNER_NONE),
      _lastTelemetryTime(0),
      _lastScoreTickTime(0),
      _scoreAlphaSec(0),
      _scoreBravoSec(0),
      _team1Name("ALPHA"),
      _team2Name("BRAVO"),
      _t1Color{255, 0, 0},
      _t2Color{0, 255, 0},
      _endColor{0, 255, 0},
      _hasEndWinnerColor(false),
      _alphaSequence("CONQUISTA A", 1, 400, 1100, 255, 0, 0, EV_BTN_2_PRESSED, "ALPHA\nCONQUISTA"),
      _bravoSequence("CONQUISTA B", 2, 400, 1100, 0, 255, 0, EV_BTN_1_PRESSED, "BRAVO\nCONQUISTA"),
      _fxSmoke(false),
      _fxRelay(false),
      _fxPulseActive(false),
      _fxPulseStartTime(0) {
    _alphaSequence.setDefaultHoldStep(10);
    _bravoSequence.setDefaultHoldStep(10);
    updateSequenceColors();
}

DominationMode::~DominationMode() {
    stopAllEffects();
    hardware.noTone();
}

void DominationMode::stopAllEffects() {
    _fxPulseActive = false;
    hardware.setRelay(false);
    hardware.triggerSmoke(0);
}

void DominationMode::triggerCaptureEffects() {
    if (!_fxSmoke && !_fxRelay) return;
    if (_fxSmoke) hardware.triggerSmoke(255);
    if (_fxRelay) hardware.setRelay(true);
    _fxPulseActive = true;
    _fxPulseStartTime = millis();
}

void DominationMode::setOledLabels(const String& p1Label, const String& p2Label) {
    hardware.setOledLabels(p1Label, p2Label);
}

void DominationMode::updateSequenceColors() {
    // Sfondo LED durante la conquista: Ciano (0,180,180) se la zona è neutra,
    // oppure il colore della fazione avversaria che detiene attualmente la zona
    uint8_t bgAlpha[3] = { 0, 180, 180 };
    uint8_t bgBravo[3] = { 0, 180, 180 };

    if (_owner == OWNER_BRAVO) {
        bgAlpha[0] = _t2Color[0]; bgAlpha[1] = _t2Color[1]; bgAlpha[2] = _t2Color[2];
    }
    if (_owner == OWNER_ALPHA) {
        bgBravo[0] = _t1Color[0]; bgBravo[1] = _t1Color[1]; bgBravo[2] = _t1Color[2];
    }

    _alphaSequence.setLedColors(_t1Color[0], _t1Color[1], _t1Color[2], bgAlpha[0], bgAlpha[1], bgAlpha[2]);
    _bravoSequence.setLedColors(_t2Color[0], _t2Color[1], _t2Color[2], bgBravo[0], bgBravo[1], bgBravo[2]);
}

void DominationMode::begin() {
    stopAllEffects();
    _owner = OWNER_NONE;
    _hasEndWinnerColor = false;
    _scoreAlphaSec = 0;
    _scoreBravoSec = 0;
    _lastScoreTickTime = millis();
    updateSequenceColors();
    enterCurrentOwnerIdleState(false);
    hardware.playAudio("/audio/common/match_start.mp3", AUDIO_CH_VOICE);
}

// ============================================================================
// GESTIONE STATI E DISPLAY LCD / OLED
// ============================================================================

void DominationMode::renderScoresLine() {
    if (_state != DOM_NEUTRAL && _state != DOM_OWNED_ALPHA && _state != DOM_OWNED_BRAVO) {
        return;
    }
    uint32_t ma = _scoreAlphaSec / 60;
    uint32_t sa = _scoreAlphaSec % 60;
    uint32_t mb = _scoreBravoSec / 60;
    uint32_t sb = _scoreBravoSec % 60;

    char lineBuf[21];
    snprintf(lineBuf, sizeof(lineBuf), "A:%02lu:%02lu      B:%02lu:%02lu",
             (unsigned long)ma, (unsigned long)sa,
             (unsigned long)mb, (unsigned long)sb);
    hardware.printLcdLine(1, String(lineBuf), false);
}

void DominationMode::renderIdleScreen() {
    hardware.clearLcd();

    if (_owner == OWNER_ALPHA) {
        hardware.printLcdLine(0, "ZONA: " + _team1Name.substring(0, 14), true);
        hardware.updateBreathingEffect(_t1Color[0], _t1Color[1], _t1Color[2]);
    } else if (_owner == OWNER_BRAVO) {
        hardware.printLcdLine(0, "ZONA: " + _team2Name.substring(0, 14), true);
        hardware.updateBreathingEffect(_t2Color[0], _t2Color[1], _t2Color[2]);
    } else {
        hardware.printLcdLine(0, "--- ZONA NEUTRA ---", true);
        hardware.updateBreathingEffect(0, 180, 180);
    }

    renderScoresLine();

    String keyHintA, keyHintB;
    bool readyA = _alphaSequence.areInitialKeysReady(keyHintA);
    bool readyB = _bravoSequence.areInitialKeysReady(keyHintB);

    if (_owner != OWNER_ALPHA && !readyA) {
        hardware.printLcdLine(2, "Chiavi " + _team1Name.substring(0, 12) + ":", true);
        hardware.printLcdLine(3, keyHintA, true);
    } else if (_owner != OWNER_BRAVO && !readyB) {
        hardware.printLcdLine(2, "Chiavi " + _team2Name.substring(0, 12) + ":", true);
        hardware.printLcdLine(3, keyHintB, true);
    } else {
        if (_owner == OWNER_NONE) {
            hardware.printLcdLine(2, "P1: " + _team1Name.substring(0, 15), true);
            hardware.printLcdLine(3, "P2: " + _team2Name.substring(0, 15), true);
        } else if (_owner == OWNER_ALPHA) {
            hardware.printLcdLine(2, "In possesso " + _team1Name.substring(0, 7), true);
            hardware.printLcdLine(3, "P2: Cattura " + _team2Name.substring(0, 7), true);
        } else {
            hardware.printLcdLine(2, "In possesso " + _team2Name.substring(0, 7), true);
            hardware.printLcdLine(3, "P1: Cattura " + _team1Name.substring(0, 7), true);
        }
    }

    String labelP1 = (_owner == OWNER_ALPHA) ? "" : (_team1Name.substring(0, 10) + "\nCONQUISTA");
    String labelP2 = (_owner == OWNER_BRAVO) ? "" : (_team2Name.substring(0, 10) + "\nCONQUISTA");
    setOledLabels(labelP1, labelP2);
}

void DominationMode::enterCurrentOwnerIdleState(bool playCaptureSound) {
    hardware.noTone();
    if (_owner == OWNER_ALPHA) {
        _state = DOM_OWNED_ALPHA;
    } else if (_owner == OWNER_BRAVO) {
        _state = DOM_OWNED_BRAVO;
    } else {
        _state = DOM_NEUTRAL;
    }

    updateSequenceColors();
    renderIdleScreen();

    if (playCaptureSound) {
        hardware.triggerLedFlash(255, 255, 255, 100, 80, 2);
        hardware.playAudio("/audio/dom/zone_captured.mp3", AUDIO_CH_VOICE);
        hardware.playTone(960, 220);
        triggerCaptureEffects();
    }

    sendTelemetry();
}

void DominationMode::startAlphaCapture() {
    String keyHint;
    if (!_alphaSequence.areInitialKeysReady(keyHint)) {
        hardware.playTone(250, 400);
        hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
        hardware.printLcdLine(2, "CHIAVE FUORI POSTO", true);
        hardware.printLcdLine(3, keyHint, true);
        return;
    }
    _state = DOM_CAPTURING_ALPHA;
    updateSequenceColors();
    hardware.clearLcd();
    if (_owner == OWNER_BRAVO) {
        hardware.playAudio("/audio/dom/zone_contested.mp3", AUDIO_CH_VOICE);
    }
    _alphaSequence.begin();
    sendTelemetry();
}

void DominationMode::startBravoCapture() {
    String keyHint;
    if (!_bravoSequence.areInitialKeysReady(keyHint)) {
        hardware.playTone(250, 400);
        hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
        hardware.printLcdLine(2, "CHIAVE FUORI POSTO", true);
        hardware.printLcdLine(3, keyHint, true);
        return;
    }
    _state = DOM_CAPTURING_BRAVO;
    updateSequenceColors();
    hardware.clearLcd();
    if (_owner == OWNER_ALPHA) {
        hardware.playAudio("/audio/dom/zone_contested.mp3", AUDIO_CH_VOICE);
    }
    _bravoSequence.begin();
    sendTelemetry();
}

void DominationMode::processJsonConfig(const char* jsonStr) {
    JsonDocument doc;
    if (deserializeJson(doc, jsonStr)) return;

    const char* cmd = doc["cmd"] | "";

    // Aggiorna nomi e colori fazione sia da SET_FACTIONS sia se inclusi in SET_DOM_SETTINGS / START_DOM_GAME
    bool factionsUpdated = false;
    if (!doc["t1"].isNull()) { _team1Name = doc["t1"].as<String>(); factionsUpdated = true; }
    if (!doc["t2"].isNull()) { _team2Name = doc["t2"].as<String>(); factionsUpdated = true; }
    if (doc["t1_c"].is<JsonArray>()) {
        JsonArray c1 = doc["t1_c"].as<JsonArray>();
        _t1Color[0] = c1[0] | 255;
        _t1Color[1] = c1[1] | 0;
        _t1Color[2] = c1[2] | 0;
        factionsUpdated = true;
    }
    if (doc["t2_c"].is<JsonArray>()) {
        JsonArray c2 = doc["t2_c"].as<JsonArray>();
        _t2Color[0] = c2[0] | 0;
        _t2Color[1] = c2[1] | 255;
        _t2Color[2] = c2[2] | 0;
        factionsUpdated = true;
    }

    if (factionsUpdated) {
        _alphaSequence.setHoldActionLabel(_team1Name.substring(0, 10) + "\nCONQUISTA");
        _bravoSequence.setHoldActionLabel(_team2Name.substring(0, 10) + "\nCONQUISTA");
        updateSequenceColors();
        if (_state == DOM_NEUTRAL || _state == DOM_OWNED_ALPHA || _state == DOM_OWNED_BRAVO) {
            renderIdleScreen();
        }
    }

    if (strcmp(cmd, "SET_DOM_SETTINGS") == 0 || strcmp(cmd, "START_DOM_GAME") == 0) {
        if (!doc["fx_smoke"].isNull()) _fxSmoke = doc["fx_smoke"] | false;
        if (!doc["fx_relay"].isNull()) _fxRelay = doc["fx_relay"] | false;

        uint32_t fallbackCapSec = doc["capture_time"] | 10;

        if (doc["alpha_seq"].is<JsonArray>()) {
            _alphaSequence.loadFromJson(doc["alpha_seq"].as<JsonArray>(), fallbackCapSec);
        } else {
            _alphaSequence.setDefaultHoldStep(fallbackCapSec);
        }

        if (doc["bravo_seq"].is<JsonArray>()) {
            _bravoSequence.loadFromJson(doc["bravo_seq"].as<JsonArray>(), fallbackCapSec);
        } else {
            _bravoSequence.setDefaultHoldStep(fallbackCapSec);
        }

        if (!doc["alpha_fail_mode"].isNull()) {
            _alphaSequence.setFailMode(InteractionSequence::parseFailMode(doc["alpha_fail_mode"], false));
        }
        if (!doc["bravo_fail_mode"].isNull()) {
            _bravoSequence.setFailMode(InteractionSequence::parseFailMode(doc["bravo_fail_mode"], false));
        }

        _alphaSequence.setHoldActionLabel(_team1Name.substring(0, 10) + "\nCONQUISTA");
        _bravoSequence.setHoldActionLabel(_team2Name.substring(0, 10) + "\nCONQUISTA");
        updateSequenceColors();
        Serial.println("[DOM] Sequenze e parametri Dominio configurati con successo.");
    }
}

// ============================================================================
// TELEMETRIA E COMUNICAZIONE SERVER
// ============================================================================

void DominationMode::sendTelemetry() {
    String stateStr = "NEUTRAL";
    uint8_t capProg = 0;

    if (_state == DOM_CAPTURING_ALPHA) {
        stateStr = "CAPTURING A...";
        capProg = _alphaSequence.getProgress();
    } else if (_state == DOM_CAPTURING_BRAVO) {
        stateStr = "CAPTURING B...";
        capProg = _bravoSequence.getProgress();
    } else if (_owner == OWNER_ALPHA) {
        stateStr = "OWNED ALPHA";
    } else if (_owner == OWNER_BRAVO) {
        stateStr = "OWNED BRAVO";
    }

    String payload = "{\"id\":\"" + String(DEVICE_ID) + "\",\"type\":\"DOM_UPDATE\",\"payload\":{";
    payload += "\"mode\":\"DOMINATION\",";
    payload += "\"state\":\"" + stateStr + "\",";
    payload += "\"score_a\":" + String(_scoreAlphaSec) + ",";
    payload += "\"score_b\":" + String(_scoreBravoSec) + ",";
    payload += "\"capture_prog\":" + String(capProg);
    payload += "}}";
    network.sendTelemetry(payload);
}

void DominationMode::sendCaptureEventToServer(const char* eventName) {
    String payload = "{\"id\":\"" + String(DEVICE_ID) + "\",\"type\":\"MATCH_EVENT\",\"payload\":{\"event\":\"" + String(eventName) + "\"}}";
    network.sendTelemetry(payload);
}

// ============================================================================
// GESTIONE EVENTI E AGGIORNAMENTO CICLICO
// ============================================================================

void DominationMode::handleEvent(const SystemEvent& event) {
    // 1. Comandi provenienti dal Server Centrale via MQTT
    if (event.type == EV_SERVER_COMMAND) {
        JsonDocument doc;
        if (!deserializeJson(doc, event.stringData)) {
            const char* cmd = doc["cmd"];
            if (cmd) {
                if (strcmp(cmd, "SET_DOM_SETTINGS") == 0 ||
                    strcmp(cmd, "START_DOM_GAME") == 0 ||
                    strcmp(cmd, "SET_FACTIONS") == 0) {
                    processJsonConfig(event.stringData);
                }
                else if (strcmp(cmd, "FORCE_WIN") == 0) {
                    stopAllEffects();
                    hardware.noTone();
                    _state = DOM_ENDED;
                    _hasEndWinnerColor = true;

                    if (!doc["score_a"].isNull()) _scoreAlphaSec = doc["score_a"] | _scoreAlphaSec;
                    if (!doc["score_b"].isNull()) _scoreBravoSec = doc["score_b"] | _scoreBravoSec;

                    const char* reason = doc["reason"] | "FINE PARTITA";
                    const char* winnerId = doc["winner_team_id"] | (doc["winner"] | "");
                    const char* winnerName = doc["winner_name"] | winnerId;

                    hardware.clearLcd();
                    hardware.printLcdLine(0, "--- FINE PARTITA ---", true);
                    hardware.printLcdLine(1, String(reason), true);

                    if (strcmp(winnerId, "DRAW") == 0) {
                        hardware.printLcdLine(2, "ESITO MATCH:", true);
                        hardware.printLcdLine(3, "PAREGGIO!", true);
                        _endColor[0] = 255; _endColor[1] = 150; _endColor[2] = 0;
                    } else {
                        hardware.printLcdLine(2, "VINCE SQUADRA:", true);
                        hardware.printLcdLine(3, String(winnerName), true);
                        if (doc["winner_color"].is<JsonArray>()) {
                            JsonArray col = doc["winner_color"].as<JsonArray>();
                            _endColor[0] = col[0] | 0;
                            _endColor[1] = col[1] | 0;
                            _endColor[2] = col[2] | 0;
                        } else if (strstr(winnerId, "ALPHA") != nullptr) {
                            _endColor[0] = _t1Color[0]; _endColor[1] = _t1Color[1]; _endColor[2] = _t1Color[2];
                        } else {
                            _endColor[0] = _t2Color[0]; _endColor[1] = _t2Color[1]; _endColor[2] = _t2Color[2];
                        }
                    }

                    hardware.setLedsColor(_endColor[0], _endColor[1], _endColor[2]);
                    // Tono immediato di fine partita (lascia libero il canale vocale per il TTS del server)
                    hardware.playTone(880, 250);
                    setOledLabels("", "");
                }
                else if (strcmp(cmd, "FORCE_END_GAME") == 0) {
                    stopAllEffects();
                    hardware.noTone();
                    _state = DOM_ENDED;
                    _hasEndWinnerColor = false;
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

    if (_state == DOM_ENDED) return;

    // 2. Gestione Input Fisici quando la zona è a riposo (NEUTRAL, OWNED_ALPHA, OWNED_BRAVO)
    if (_state == DOM_NEUTRAL || _state == DOM_OWNED_ALPHA || _state == DOM_OWNED_BRAVO) {
        bool canAlphaCapture = (_owner != OWNER_ALPHA);
        bool canBravoCapture = (_owner != OWNER_BRAVO);

        if (event.type == EV_BTN_1_PRESSED && canAlphaCapture) {
            startAlphaCapture();
            return;
        }
        if (event.type == EV_BTN_2_PRESSED && canBravoCapture) {
            startBravoCapture();
            return;
        }

        // Avvio diretto tramite selettore a chiave se il primo step è KEY
        if (canAlphaCapture && _alphaSequence.canStartWithKeyEvent(event)) {
            _state = DOM_CAPTURING_ALPHA;
            updateSequenceColors();
            hardware.clearLcd();
            if (_owner == OWNER_BRAVO) {
                hardware.playAudio("/audio/dom/zone_contested.mp3", AUDIO_CH_VOICE);
            }
            _alphaSequence.beginWithDirectKeyEvent(event);
            if (_alphaSequence.getResult() == INTERACTION_COMPLETED) {
                _owner = OWNER_ALPHA;
                sendCaptureEventToServer("ZONE_CAPTURED_ALPHA");
                enterCurrentOwnerIdleState(true);
            } else {
                sendTelemetry();
            }
            return;
        }
        if (canBravoCapture && _bravoSequence.canStartWithKeyEvent(event)) {
            _state = DOM_CAPTURING_BRAVO;
            updateSequenceColors();
            hardware.clearLcd();
            if (_owner == OWNER_ALPHA) {
                hardware.playAudio("/audio/dom/zone_contested.mp3", AUDIO_CH_VOICE);
            }
            _bravoSequence.beginWithDirectKeyEvent(event);
            if (_bravoSequence.getResult() == INTERACTION_COMPLETED) {
                _owner = OWNER_BRAVO;
                sendCaptureEventToServer("ZONE_CAPTURED_BRAVO");
                enterCurrentOwnerIdleState(true);
            } else {
                sendTelemetry();
            }
            return;
        }

        // Avvio diretto tramite scansione RFID se il primo step è RFID
        if (event.type == EV_RFID_SCANNED) {
            String keyHint;
            if (canAlphaCapture && _alphaSequence.canStartWithRfidEvent(event)) {
                if (!_alphaSequence.areInitialKeysReady(keyHint)) {
                    RfidInteraction::sendRfidTelemetry(String(event.stringData), false, "DOM_KEY_ERR");
                    hardware.playTone(250, 400);
                    hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
                    hardware.printLcdLine(2, "CHIAVE FUORI POSTO", true);
                    hardware.printLcdLine(3, keyHint, true);
                    return;
                }
                updateSequenceColors();
                hardware.clearLcd();
                if (_alphaSequence.tryStartWithRfidEvent(event, "DOM_ALPHA_START")) {
                    if (_owner == OWNER_BRAVO) {
                        hardware.playAudio("/audio/dom/zone_contested.mp3", AUDIO_CH_VOICE);
                    }
                    if (_alphaSequence.getResult() == INTERACTION_COMPLETED) {
                        _owner = OWNER_ALPHA;
                        sendCaptureEventToServer("ZONE_CAPTURED_ALPHA");
                        enterCurrentOwnerIdleState(true);
                    } else {
                        _state = DOM_CAPTURING_ALPHA;
                        sendTelemetry();
                    }
                    return;
                }
            }
            if (canBravoCapture && _bravoSequence.canStartWithRfidEvent(event)) {
                if (!_bravoSequence.areInitialKeysReady(keyHint)) {
                    RfidInteraction::sendRfidTelemetry(String(event.stringData), false, "DOM_KEY_ERR");
                    hardware.playTone(250, 400);
                    hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
                    hardware.printLcdLine(2, "CHIAVE FUORI POSTO", true);
                    hardware.printLcdLine(3, keyHint, true);
                    return;
                }
                updateSequenceColors();
                hardware.clearLcd();
                if (_bravoSequence.tryStartWithRfidEvent(event, "DOM_BRAVO_START")) {
                    if (_owner == OWNER_ALPHA) {
                        hardware.playAudio("/audio/dom/zone_contested.mp3", AUDIO_CH_VOICE);
                    }
                    if (_bravoSequence.getResult() == INTERACTION_COMPLETED) {
                        _owner = OWNER_BRAVO;
                        sendCaptureEventToServer("ZONE_CAPTURED_BRAVO");
                        enterCurrentOwnerIdleState(true);
                    } else {
                        _state = DOM_CAPTURING_BRAVO;
                        sendTelemetry();
                    }
                    return;
                }
            }

            RfidInteraction::sendRfidTelemetry(String(event.stringData), false, "DOM_IDLE");
            if ((canAlphaCapture && _alphaSequence.isFirstStepRfid()) ||
                (canBravoCapture && _bravoSequence.isFirstStepRfid())) {
                hardware.playTone(250, 400);
                hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
                renderIdleScreen();
                hardware.printLcdLine(3, "TESSERA ERRATA!", true);
            }
            return;
        }

        if (event.type == EV_KEY_1_STATE_CHANGED || event.type == EV_KEY_2_STATE_CHANGED) {
            renderIdleScreen();
        }
    }
    // 3. Delega degli eventi durante una sequenza di conquista attiva
    else if (_state == DOM_CAPTURING_ALPHA) {
        _alphaSequence.handleEvent(event);
        InteractionResult res = _alphaSequence.getResult();
        if (res == INTERACTION_CANCELLED || res == INTERACTION_FAILED) {
            enterCurrentOwnerIdleState(false);
        } else if (res == INTERACTION_COMPLETED) {
            _owner = OWNER_ALPHA;
            sendCaptureEventToServer("ZONE_CAPTURED_ALPHA");
            enterCurrentOwnerIdleState(true);
        }
    }
    else if (_state == DOM_CAPTURING_BRAVO) {
        _bravoSequence.handleEvent(event);
        InteractionResult res = _bravoSequence.getResult();
        if (res == INTERACTION_CANCELLED || res == INTERACTION_FAILED) {
            enterCurrentOwnerIdleState(false);
        } else if (res == INTERACTION_COMPLETED) {
            _owner = OWNER_BRAVO;
            sendCaptureEventToServer("ZONE_CAPTURED_BRAVO");
            enterCurrentOwnerIdleState(true);
        }
    }
}

void DominationMode::update() {
    if (_fxPulseActive && (millis() - _fxPulseStartTime >= 500)) {
        stopAllEffects();
    }

    if (_state == DOM_ENDED) {
        if (_hasEndWinnerColor) {
            hardware.updateBreathingEffect(_endColor[0], _endColor[1], _endColor[2]);
        }
        return;
    }

    // 1. Effetto Breathing LED continuo quando la zona non è in fase di cattura
    if (_state == DOM_NEUTRAL) {
        hardware.updateBreathingEffect(0, 180, 180);
    } else if (_state == DOM_OWNED_ALPHA) {
        hardware.updateBreathingEffect(_t1Color[0], _t1Color[1], _t1Color[2]);
    } else if (_state == DOM_OWNED_BRAVO) {
        hardware.updateBreathingEffect(_t2Color[0], _t2Color[1], _t2Color[2]);
    }

    // 2. Conteggio locale a 1 Hz del tempo di possesso per aggiornare l'LCD del prop
    if (millis() - _lastScoreTickTime >= 1000) {
        _lastScoreTickTime += 1000;
        if (_owner == OWNER_ALPHA) {
            _scoreAlphaSec++;
            renderScoresLine();
        } else if (_owner == OWNER_BRAVO) {
            _scoreBravoSec++;
            renderScoresLine();
        }
    }

    // 3. Telemetria MQTT (10 Hz durante una cattura per fluidità barre, 1 Hz a riposo)
    bool isCapturing = (_state == DOM_CAPTURING_ALPHA || _state == DOM_CAPTURING_BRAVO);
    unsigned long telemetryInterval = isCapturing ? 100 : 1000;

    if (millis() - _lastTelemetryTime >= telemetryInterval) {
        _lastTelemetryTime = millis();
        sendTelemetry();
    }

    // 4. Avanzamento sequenza di conquista ALPHA
    if (_state == DOM_CAPTURING_ALPHA) {
        _alphaSequence.update();
        InteractionResult res = _alphaSequence.getResult();
        if (res == INTERACTION_CANCELLED || res == INTERACTION_FAILED) {
            enterCurrentOwnerIdleState(false);
        } else if (res == INTERACTION_COMPLETED) {
            _owner = OWNER_ALPHA;
            sendCaptureEventToServer("ZONE_CAPTURED_ALPHA");
            enterCurrentOwnerIdleState(true);
        }
    }
    // 5. Avanzamento sequenza di conquista BRAVO
    else if (_state == DOM_CAPTURING_BRAVO) {
        _bravoSequence.update();
        InteractionResult res = _bravoSequence.getResult();
        if (res == INTERACTION_CANCELLED || res == INTERACTION_FAILED) {
            enterCurrentOwnerIdleState(false);
        } else if (res == INTERACTION_COMPLETED) {
            _owner = OWNER_BRAVO;
            sendCaptureEventToServer("ZONE_CAPTURED_BRAVO");
            enterCurrentOwnerIdleState(true);
        }
    }
}