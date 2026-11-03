// src/GameModes/DominationMode.cpp

#include "DominationMode.h"
#include <ArduinoJson.h>

// Costruttore
DominationMode::DominationMode(HardwareManager* hardware, NetworkManager* network, DominationSettings* settings, AppState* appState, MainMenuDisplayFunction displayFunc)
    : _hardware(hardware),
      _network(network),
      _settings(settings),
      _appStatePtr(appState),
      _mainMenuDisplayFunc(displayFunc),
      _currentState(ModeState::MODE_SUB_MENU),
      _lastZoneState(ModeState::IN_GAME_NEUTRAL),
      _subMenuIndex(0),
      _menuIndex(0),
      _lastTelemetryTime(0) {
}

void DominationMode::enter() {
    Serial.println("Entrato in modalita' Dominio");
    _currentState = ModeState::MODE_SUB_MENU;
    _subMenuIndex = 0;
    _team1PossessionTime = 0;
    _team2PossessionTime = 0;
    _endGameStatus = ""; // Reset stato finale
    displaySubMenu();
    _hardware->setStripColor(0, 255, 255);
    
    // Invia configurazione iniziale
    sendSettingsStatus();
}

/**
 * @brief Funzione di ingresso diretto in partita, saltando menu e countdown.
 * @details Chiamata dalla Modalità Terminale, avvia direttamente la partita.
 */
void DominationMode::enterInGame() {
    Serial.println("Entrato in modalita' Dominio (remoto)");
    
    JsonDocument doc;
    doc["mode"] = "DOMINATION";
    _network->sendEvent("MODE_ENTER", doc);

    // Salta direttamente allo stato di gioco attivo
    _currentState = ModeState::IN_GAME_NEUTRAL;
    _lastZoneState = ModeState::IN_GAME_NEUTRAL;
    _gameStartTime = _hardware->getRTCTime();
    _lastGameSecond = -1;
    _team1PossessionTime = 0;
    _team2PossessionTime = 0;
    _endGameStatus = "";

    // Esegui effetti visivi e sonori di inizio partita
    _hardware->playTone(1500, 500);
    _hardware->setBrightness(255);
    _hardware->turnOffStrip();
    delay(100);
    _hardware->setStripColor(255, 255, 255);
    delay(500);
    _hardware->turnOffStrip();
    _hardware->setBrightness(80);

    // Disegna la schermata di gioco iniziale
    _hardware->clearLcd();
    _hardware->printLcd(4, 1, "ZONA NEUTRA");
    _hardware->printOled1("CONQUISTA", 2, 8, 25);
    _hardware->printOled2("CONQUISTA", 2, 8, 25);

    // Invia il messaggio di inizio partita
    JsonDocument startDoc;
    startDoc["mode"] = "DOMINATION";
    startDoc["duration_min"] = _settings->getGameDuration();
    _network->sendEvent("GAME_START", startDoc);

    sendTelemetry(); // Aggiornamento immediato
}

void DominationMode::loop() {
    char key = _hardware->getKey();
    bool btn1_is_pressed = _hardware->isButton1Pressed();
    bool btn1_was_pressed = _hardware->wasButton1Pressed();
    bool btn2_is_pressed = _hardware->isButton2Pressed();
    bool btn2_was_pressed = _hardware->wasButton2Pressed();

    switch (_currentState) {
        case ModeState::MODE_SUB_MENU:
            handleSubMenuInput(key, btn1_was_pressed, btn2_was_pressed);
            break;
        case ModeState::MENU_SETTINGS:
            handleSettingsInput(key, btn1_was_pressed, btn2_was_pressed);
            break;
        case ModeState::EDIT_DURATION:
        case ModeState::EDIT_CAPTURE_TIME:
        case ModeState::EDIT_COUNTDOWN:
            handleEditInput(key, btn1_was_pressed, btn2_was_pressed);
            break;
        case ModeState::IN_GAME_CONFIRM:
            handleConfirmInput(btn1_was_pressed, btn2_was_pressed);
            break;
        case ModeState::IN_GAME_COUNTDOWN:
            handleCountdown();
            break;
        case ModeState::IN_GAME_NEUTRAL:
            handleNeutralState(btn1_is_pressed, btn2_is_pressed);
            break;
        case ModeState::CAPTURING_TEAM1:
        case ModeState::CAPTURING_TEAM2:
            handleCapturingState(btn1_is_pressed, btn2_is_pressed);
            break;
        case ModeState::TEAM1_CAPTURED:
            handleCapturedState(1, btn1_is_pressed, btn2_is_pressed);
            break;
        case ModeState::TEAM2_CAPTURED:
            handleCapturedState(2, btn1_is_pressed, btn2_is_pressed);
            break;
        case ModeState::GAME_OVER:
            handleGameOverState(btn1_was_pressed, btn2_was_pressed, key);
            break;
    }

    long telemetryInterval = 1000;
    
    // Se stiamo CATTURANDO, aggiorna ogni 100ms per fluidità assoluta della barra
    if (_currentState == ModeState::CAPTURING_TEAM1 || _currentState == ModeState::CAPTURING_TEAM2) {
        telemetryInterval = 100; 
    }

    if (_currentState != ModeState::MODE_SUB_MENU && 
        _currentState != ModeState::MENU_SETTINGS && 
        millis() - _lastTelemetryTime > telemetryInterval) {
        
        sendTelemetry();
        _lastTelemetryTime = millis();
    }
}

// --- LOGICA TELEMETRIA ---
void DominationMode::sendTelemetry() {
    JsonDocument doc;

    // --- 1. GESTIONE STATO E TEMPO ---
    long timeToSend = 0;
    String stateToSend = "";

    if (_currentState == ModeState::IN_GAME_COUNTDOWN) {
        // --- FASE COUNTDOWN ---
        stateToSend = "PREPARING"; // "PREPARING" attiva il colore arancione sul sito
        
        // Calcola quanto manca all'inizio
        unsigned long elapsed = millis() - _countdownStartTime;
        long totalCountdown = _settings->getCountdownDuration() * 1000;
        timeToSend = (totalCountdown - elapsed) / 1000;
        if (timeToSend < 0) timeToSend = 0;
        
    } else {
        // --- FASE DI GIOCO O FINE ---
        
        // Mappa stati di gioco
        if (_currentState == ModeState::IN_GAME_NEUTRAL) stateToSend = "NEUTRAL";
        else if (_currentState == ModeState::TEAM1_CAPTURED) stateToSend = "OWNED ALPHA";
        else if (_currentState == ModeState::TEAM2_CAPTURED) stateToSend = "OWNED BRAVO";
        else if (_currentState == ModeState::CAPTURING_TEAM1) stateToSend = "CAPTURING A...";
        else if (_currentState == ModeState::CAPTURING_TEAM2) stateToSend = "CAPTURING B...";
        else if (_currentState == ModeState::GAME_OVER) stateToSend = _endGameStatus;
        else stateToSend = "CONFIG"; 

        // Calcolo Tempo Partita Rimanente
        long totalSeconds = _settings->getGameDuration() * 60;
        
        if (_currentState == ModeState::GAME_OVER) {
            // FIX: PARTITA FINITA O INTERROTTA: blocchiamo il timer a 0
            timeToSend = 0;
        } 
        else if (_currentState == ModeState::IN_GAME_NEUTRAL || 
                 _currentState == ModeState::CAPTURING_TEAM1 || 
                 _currentState == ModeState::CAPTURING_TEAM2 || 
                 _currentState == ModeState::TEAM1_CAPTURED  || 
                 _currentState == ModeState::TEAM2_CAPTURED) {
            // PARTITA IN CORSO: calcoliamo il tempo che scorre
            TimeSpan elapsed = _hardware->getRTCTime() - _gameStartTime;
            timeToSend = totalSeconds - elapsed.totalseconds();
            if (timeToSend < 0) timeToSend = 0;
        } 
        else {
            // MENU O PRE-PARTITA: mostra il timer pieno iniziale
            timeToSend = totalSeconds; 
        }
    }

    doc["state"] = stateToSend;
    doc["game_time"] = timeToSend; // Invia numero INTERO (risolve NaN:NaN)

    // --- 2. PUNTEGGI ---
    doc["score_a"] = _team1PossessionTime / 1000;
    doc["score_b"] = _team2PossessionTime / 1000;

    // --- 3. PROGRESSO CATTURA ---
    int progress = 0;
    // Se siamo in fase di cattura, invia progresso reale.
    // Altrimenti, se siamo in inserimento PIN (non usato qui ma per coerenza) o altro, 0.
    if (_currentState == ModeState::CAPTURING_TEAM1 || _currentState == ModeState::CAPTURING_TEAM2) {
        unsigned long elapsed = millis() - _captureStartTime;
        unsigned long total = _settings->getCaptureTime() * 1000;
        if (total > 0) {
            progress = (elapsed * 100) / total;
            if (progress > 100) progress = 100;
        }
    }
    doc["capture_prog"] = progress;

    _network->sendEvent("DOM_UPDATE", doc);
}

void DominationMode::exit() {
    Serial.println("Uscito da modalita' Dominio");
    
    JsonDocument doc;
    doc["mode"] = "DOMINATION";
    _network->sendEvent("MODE_EXIT", doc);

    _settings->saveParameters();
    _hardware->turnOffStrip();
    _hardware->clearOled1();
    _hardware->clearOled2();
}

void DominationMode::displaySubMenu() {
    _hardware->clearLcd();
    _hardware->printLcd(0, 0, "DOMINIO");
    String menuItems[] = { "Inizia Partita", "Impostazioni" };
    for (int i = 0; i < 2; i++) {
        String prefix = (i == _subMenuIndex) ? "> " : "  ";
        _hardware->printLcd(0, i + 1, prefix + menuItems[i]);
    }
    _hardware->printOled1("INDIETRO", 2, 10, 25);
    _hardware->printOled2("CONFERMA", 2, 18, 25);
}

void DominationMode::handleSubMenuInput(char key, bool btn1, bool btn2) {
    String menuItems[] = { "Inizia Partita", "Impostazioni" };
    int numItems = 2;

    if (key == '2') {
        _hardware->playTone(800, 50);
        _subMenuIndex = (_subMenuIndex - 1 + numItems) % numItems;
        displaySubMenu();
    } else if (key == '8') {
        _hardware->playTone(600, 50);
        _subMenuIndex = (_subMenuIndex + 1) % numItems;
        displaySubMenu();
    }

    if (btn1) {
        _hardware->playTone(300, 70);
        exit(); 
        *_appStatePtr = APP_STATE_MAIN_MENU;
        _mainMenuDisplayFunc();
        return;
    }

    if (btn2) {
        _hardware->playTone(1200, 100);
        switch (_subMenuIndex) {
            case 0: // Inizia Partita
                _currentState = ModeState::IN_GAME_CONFIRM;
                displayConfirmScreen();
                break;
            case 1: // Impostazioni
                _currentState = ModeState::MENU_SETTINGS;
                _menuIndex = 0;
                displaySettingsMenu();
                break;
        }
    }
}

void DominationMode::displaySettingsMenu() {
    _hardware->clearLcd();
    _hardware->printLcd(0, 0, "IMPOSTAZIONI DOMINIO");
    String menuItems[] = { "Durata Partita", "Tempo Conquista", "Durata Countdown" };
    int numItems = 3;
    for (int i = 0; i < numItems; i++) {
        String prefix = (i == _menuIndex) ? "> " : "  ";
        _hardware->printLcd(0, i + 1, prefix + menuItems[i]);
    }
    _hardware->printOled1("INDIETRO", 2, 10, 25);
    _hardware->printOled2("CONFERMA", 2, 18, 25);
}

void DominationMode::handleSettingsInput(char key, bool btn1, bool btn2) {
    String menuItems[] = { "Durata Partita", "Tempo Conquista", "Durata Countdown" };
    int numItems = 3;

    if (key == '2') {
        _hardware->playTone(800, 50);
        _menuIndex = (_menuIndex - 1 + numItems) % numItems;
        displaySettingsMenu();
    } else if (key == '8') {
        _hardware->playTone(600, 50);
        _menuIndex = (_menuIndex + 1) % numItems;
        displaySettingsMenu();
    }

    if (btn1) {
        _hardware->playTone(300, 70);
        _currentState = ModeState::MODE_SUB_MENU;
        displaySubMenu();
        sendSettingsStatus();
        return;
    }

    if (btn2) {
        _hardware->playTone(1200, 100);
        _currentInputBuffer = "";
        switch (_menuIndex) {
            case 0: _currentState = ModeState::EDIT_DURATION; break;
            case 1: _currentState = ModeState::EDIT_CAPTURE_TIME; break;
            case 2: _currentState = ModeState::EDIT_COUNTDOWN; break;
        }
        updateDisplayForCurrentState();
    }
}

void DominationMode::displayEditScreen(const String& title, const String& currentValue, const String& unit) {
    _hardware->clearLcd();
    _hardware->printLcd(0, 0, title);
    _hardware->printLcd(0, 1, "Attuale: " + currentValue + unit);
    _hardware->printLcd(0, 2, "Nuovo: " + _currentInputBuffer + "_");
    _hardware->printLcd(0, 3, "P1:Annulla | P2:OK");
    _hardware->setStripColor(0, 0, 255);
    _hardware->printOled1("ANNULLA", 2, 22, 25);
    _hardware->printOled2("CONFERMA", 2, 18, 25);
}

void DominationMode::handleEditInput(char key, bool btn1, bool btn2) {
    if (btn1) {
        _hardware->playTone(300, 70);
        _currentState = ModeState::MENU_SETTINGS;
        displaySettingsMenu();
        _hardware->setStripColor(0, 255, 255);
        return;
    }
    
    bool displayNeedsUpdate = false;
    if (isdigit(key)) {
        _hardware->playTone(700, 30);
        _currentInputBuffer += key;
        displayNeedsUpdate = true;
    } else if (key == '*') {
        if (_currentInputBuffer.length() > 0) {
            _currentInputBuffer.remove(_currentInputBuffer.length() - 1);
        }
        displayNeedsUpdate = true;
    }

    if (btn2) {
        _hardware->playTone(1200, 100);
        if (_currentInputBuffer.length() > 0) {
            int value = _currentInputBuffer.toInt();
            if (_currentState == ModeState::EDIT_DURATION) {
                _settings->setGameDuration(value);
            } else if (_currentState == ModeState::EDIT_CAPTURE_TIME) {
                _settings->setCaptureTime(value);
            } else if (_currentState == ModeState::EDIT_COUNTDOWN) {
                _settings->setCountdownDuration(value);
            }
            _currentState = ModeState::MENU_SETTINGS;
            displaySettingsMenu();
            _hardware->setStripColor(0, 255, 255);
        } else {
            _currentState = ModeState::MENU_SETTINGS;
            displaySettingsMenu();
            _hardware->setStripColor(0, 255, 255);
        }
        return;
    }

    if (displayNeedsUpdate) {
        updateDisplayForCurrentState();
    }
}

void DominationMode::updateDisplayForCurrentState() {
    switch (_currentState) {
        case ModeState::EDIT_DURATION:
            displayEditScreen("Durata Partita", String(_settings->getGameDuration()), "min");
            break;
        case ModeState::EDIT_CAPTURE_TIME:
            displayEditScreen("Tempo Conquista", String(_settings->getCaptureTime()), "s");
            break;
        case ModeState::EDIT_COUNTDOWN:
            displayEditScreen("Durata Countdown", String(_settings->getCountdownDuration()), "s");
            break;
        default:
            break;
    }
}

void DominationMode::displayConfirmScreen() {
    _hardware->clearLcd();
    _hardware->printLcd(0, 1, "INIZIARE LA PARTITA?");
    _hardware->printLcd(0, 3, "P1:Annulla | P2:OK");
    _hardware->printOled1("ANNULLA", 2, 22, 25);
    _hardware->printOled2("INIZIA", 2, 28, 25);
}

void DominationMode::handleConfirmInput(bool btn1, bool btn2) {
    if (btn1) {
        _currentState = ModeState::MODE_SUB_MENU;
        displaySubMenu();
    }
    if (btn2) {
        _currentState = ModeState::IN_GAME_COUNTDOWN;
        _countdownStartTime = millis();
        _lastCountdownSecond = -1;

        _team1PossessionTime = 0;
        _team2PossessionTime = 0;
        _endGameStatus = "";

        _hardware->clearLcd();
        _hardware->printLcd(4, 1, "LA PARTITA");
        _hardware->printLcd(3, 2, "INIZIA TRA...");
        _hardware->setStripColor(255, 255, 255);
        _hardware->clearOled1();
        _hardware->clearOled2();
        
        // Forza un invio dati immediato per pulire l'interfaccia web
        sendTelemetry();
    }
}

void DominationMode::handleCountdown() {
    unsigned long countdownDuration = _settings->getCountdownDuration() * 1000;
    unsigned long elapsedTime = millis() - _countdownStartTime;
    
    if (elapsedTime >= countdownDuration) {
        // FINE COUNTDOWN - INIZIO PARTITA
        _currentState = ModeState::IN_GAME_NEUTRAL;
        _lastZoneState = ModeState::IN_GAME_NEUTRAL;
        _gameStartTime = _hardware->getRTCTime();
        _lastGameSecond = -1;
        _team1PossessionTime = 0;
        _team2PossessionTime = 0;
        _endGameStatus = ""; // Reset

        _hardware->playTone(1500, 500);
        _hardware->setBrightness(255);
        _hardware->turnOffStrip();
        delay(100);
        _hardware->setStripColor(255, 255, 255);
        delay(500);
        _hardware->turnOffStrip();
        _hardware->setBrightness(80);

        _hardware->clearLcd();
        _hardware->printLcd(4, 1, "ZONA NEUTRA");
        _hardware->printOled1("CONQUISTA", 2, 8, 25);
        _hardware->printOled2("CONQUISTA", 2, 8, 25);

        sendTelemetry(); // Aggiorna sito
        return;
    }

    int remainingSeconds = (_settings->getCountdownDuration()) - (elapsedTime / 1000);
    if (remainingSeconds != _lastCountdownSecond) {
        String secStr = String(remainingSeconds);
        if(remainingSeconds < 10) secStr = "0" + secStr;
        
        _hardware->printLcd(9, 3, secStr);
        
        if (remainingSeconds > 3) {
            _hardware->playTone(800, 100);
        } else if (remainingSeconds > 0) {
            _hardware->playTone(1200, 150);
        }
        
        _lastCountdownSecond = remainingSeconds;
        sendTelemetry(); // Aggiorna timer standby sul sito
    }
}

void DominationMode::updateGameTimerOnRow(int row) {
    long totalSeconds = _settings->getGameDuration() * 60;

    if (_gameStartTime.year() < 2024) {
        Serial.println("ERRORE: Orario di inizio partita non valido!");
        _hardware->printLcd(0, 3, "ERRORE OROLOGIO RTC");
        return;
    }

    TimeSpan elapsed = _hardware->getRTCTime() - _gameStartTime;
    long remainingSeconds = totalSeconds - elapsed.totalseconds();

    if (remainingSeconds < 0) remainingSeconds = 0;

    // Controllo Fine Partita
    if (remainingSeconds == 0 && _currentState != ModeState::GAME_OVER) {
        _currentState = ModeState::GAME_OVER;
        
        _hardware->playToneAsync(400, 1000);

        if (_team1PossessionTime > _team2PossessionTime) _endGameStatus = "ALPHA WINS";
        else if (_team2PossessionTime > _team1PossessionTime) _endGameStatus = "BRAVO WINS";
        else _endGameStatus = "DRAW";
        
        // Imposta vincitore numerico per effetti luce
        if (_endGameStatus == "ALPHA WINS") _winner = 1;
        else if (_endGameStatus == "BRAVO WINS") _winner = 2;
        else _winner = 0;

        _hardware->clearLcd();
        if (_winner == 1) _hardware->printLcd(2, 1, "VINCE SQUADRA 1!");
        else if (_winner == 2) _hardware->printLcd(2, 1, "VINCE SQUADRA 2!");
        else _hardware->printLcd(6, 1, "PAREGGIO!");
        
        char scoreBuffer[20];
        sprintf(scoreBuffer, "S1: %02lu:%02lu", (_team1PossessionTime / 1000) / 60, (_team1PossessionTime / 1000) % 60);
        _hardware->printLcd(6, 2, scoreBuffer);
        sprintf(scoreBuffer, "S2: %02lu:%02lu", (_team2PossessionTime / 1000) / 60, (_team2PossessionTime / 1000) % 60);
        _hardware->printLcd(6, 3, scoreBuffer);

        _hardware->printOled1("ESCI", 2, 35, 25);
        _hardware->printOled2("ESCI", 2, 35, 25);
        
        sendTelemetry(); // Invia stato finale
        return;
    }

    if (remainingSeconds != _lastGameSecond) {
        int minutes = remainingSeconds / 60;
        int seconds = remainingSeconds % 60;
        char timeBuffer[10];
        sprintf(timeBuffer, "%02d : %02d", minutes, seconds);
        _hardware->printLcd(6, row, timeBuffer);

        if (remainingSeconds > 0 && remainingSeconds < totalSeconds && remainingSeconds % 60 == 0) {
            _hardware->playTone(1500, 150);
            _hardware->flashCurrentColor(2, 100);
        } else if (remainingSeconds == 60) {
            _hardware->playTone(1600, 80); delay(100); _hardware->playTone(1600, 80);
        } else if (remainingSeconds <= 10 && remainingSeconds > 3) {
            _hardware->playTone(800, 100);
            _hardware->flashCurrentColor(1, 100);
        } else if (remainingSeconds <= 3 && remainingSeconds > 0) {
            _hardware->playTone(1200, 150);
            _hardware->flashCurrentColor(1, 100);
        }
        
        _lastGameSecond = remainingSeconds;
    }
}

void DominationMode::handleNeutralState(bool btn1_is_pressed, bool btn2_is_pressed) {
    updateGameTimerOnRow(2);
    _hardware->updateBreathingEffect(255, 255, 255);
    
    if (btn1_is_pressed) {
        _currentState = ModeState::CAPTURING_TEAM1;
        _captureStartTime = millis();
        _captureSoundLastUpdate = 0;
        displayCapturingScreen(1);
        sendTelemetry(); // Inizio cattura
    }
    if (btn2_is_pressed) {
        _currentState = ModeState::CAPTURING_TEAM2;
        _captureStartTime = millis();
        _captureSoundLastUpdate = 0;
        displayCapturingScreen(2);
        sendTelemetry(); // Inizio cattura
    }
}

void DominationMode::displayCapturingScreen(int team) {
    _hardware->clearLcd();
    _hardware->printLcd(5, 1, "CONQUISTA!");
    if (team == 1) {
        _hardware->printOled1("CONQUISTA", 2, 8, 25);
        _hardware->clearOled2();
    } else {
        _hardware->clearOled1();
        _hardware->printOled2("CONQUISTA", 2, 8, 25);
    }
}

void DominationMode::handleCapturingState(bool btn1_is_pressed, bool btn2_is_pressed) {
    updateGameTimerOnRow(3);

    int teamCapturing = (_currentState == ModeState::CAPTURING_TEAM1) ? 1 : 2;
    bool isStillPressed = (teamCapturing == 1) ? btn1_is_pressed : btn2_is_pressed;

    if (!isStillPressed) {
        // CATTURA FALLITA / INTERROTTA
        _currentState = _lastZoneState;
        if (_lastZoneState == ModeState::IN_GAME_NEUTRAL) {
            _hardware->clearLcd();
            _hardware->printLcd(4, 1, "ZONA NEUTRA");
            _hardware->printOled1("CONQUISTA", 2, 8, 25);
            _hardware->printOled2("CONQUISTA", 2, 8, 25);
        } else if (_lastZoneState == ModeState::TEAM1_CAPTURED) {
            _hardware->clearLcd();
            _hardware->printLcd(5, 1, "ZONA ROSSA");
            _hardware->clearOled1();
            _hardware->printOled2("CONQUISTA", 2, 8, 25);
        } else {
            _hardware->clearLcd();
            _hardware->printLcd(5, 1, "ZONA VERDE");
            _hardware->printOled1("CONQUISTA", 2, 8, 25);
            _hardware->clearOled2();
        }
        _hardware->noTone();
        sendTelemetry(); // Aggiorna stato annullato
        return;
    }

    unsigned long captureDuration = _settings->getCaptureTime() * 1000;
    unsigned long elapsedTime = millis() - _captureStartTime;

    if (elapsedTime >= captureDuration) {
        // CATTURA COMPLETATA
        _hardware->noTone();
        _hardware->playTone(1500, 80);
        delay(100);
        _hardware->playTone(1500, 80);

        _lastPossessionUpdateTime = millis();
        if (teamCapturing == 1) {
            _currentState = ModeState::TEAM1_CAPTURED;
            _lastZoneState = ModeState::TEAM1_CAPTURED;
            _hardware->clearLcd();
            _hardware->printLcd(5, 1, "ZONA ROSSA");
            _hardware->clearOled1();
            _hardware->printOled2("CONQUISTA", 2, 8, 25);
        } else {
            _currentState = ModeState::TEAM2_CAPTURED;
            _lastZoneState = ModeState::TEAM2_CAPTURED;
            _hardware->clearLcd();
            _hardware->printLcd(5, 1, "ZONA VERDE");
            _hardware->printOled1("CONQUISTA", 2, 8, 25);
            _hardware->clearOled2();
        }
        sendTelemetry(); // Conferma cattura
        return;
    }

    // Visualizzazione Barra Progresso LCD e LED
    int barWidthChars = 16;
    int totalPixels = barWidthChars * 5;
    int progressPixels = map(elapsedTime, 0, captureDuration, 0, totalPixels);
    int fullChars = progressPixels / 5;
    int partialPixels = progressPixels % 5;

    _hardware->printLcd(2, 2, "");
    for (int i = 0; i < fullChars; i++) _hardware->writeCustomChar(4);
    if (partialPixels > 0 && fullChars < barWidthChars) _hardware->writeCustomChar(partialPixels - 1);
    for (int i = fullChars + (partialPixels > 0 ? 1 : 0); i < barWidthChars; i++) _hardware->printLcd(i + 2, 2, " ");

    uint8_t r = (teamCapturing == 1) ? 255 : 0;
    uint8_t g = (teamCapturing == 2) ? 255 : 0;
    int ledsToShow = map(elapsedTime, 0, captureDuration, 0, _hardware->getStripLedCount());
    
    for(int i=0; i < _hardware->getStripLedCount(); i++){
        if (i < ledsToShow) {
            _hardware->setPixelColor(i, r, g, 0);
        } else {
            if (_lastZoneState == ModeState::TEAM1_CAPTURED) {
                _hardware->setPixelColor(i, 255, 0, 0);
            } else if (_lastZoneState == ModeState::TEAM2_CAPTURED) {
                _hardware->setPixelColor(i, 0, 255, 0);
            } else { 
                _hardware->setPixelColor(i, 255, 255, 255);
            }
        }
    }
    _hardware->showStrip();

    if (millis() - _captureSoundLastUpdate > 50) {
        _captureSoundLastUpdate = millis();
        int frequency = map(elapsedTime, 0, captureDuration, 400, 1200);
        _hardware->updateTone(frequency);
    }
}

void DominationMode::handleCapturedState(int team, bool btn1_is_pressed, bool btn2_is_pressed) {
    updateGameTimerOnRow(2);
    
    uint8_t r = (team == 1) ? 255 : 0;
    uint8_t g = (team == 2) ? 255 : 0;
    _hardware->updateBreathingEffect(r, g, 0);

    unsigned long now = millis();
    if (team == 1) {
        _team1PossessionTime += now - _lastPossessionUpdateTime;
    } else {
        _team2PossessionTime += now - _lastPossessionUpdateTime;
    }
    _lastPossessionUpdateTime = now;

    bool enemyButtonPressed = (team == 1) ? btn2_is_pressed : btn1_is_pressed;
    if (enemyButtonPressed) {
        _currentState = (team == 1) ? ModeState::CAPTURING_TEAM2 : ModeState::CAPTURING_TEAM1;
        _captureStartTime = millis();
        _captureSoundLastUpdate = 0;
        displayCapturingScreen((team == 1) ? 2 : 1);
        sendTelemetry(); // Inizio tentativo rubare zona
        return;
    }
}

void DominationMode::handleGameOverState(bool btn1_was_pressed, bool btn2_was_pressed, char key) {
    uint8_t r = 0, g = 0, b = 0;
    if (_winner == 1) { r = 255; }
    else if (_winner == 2) { g = 255; }
    else { r = 255; g = 255; b = 255; }
    _hardware->updateWinnerWaveEffect(r, g, b, 0.1, 1, 10);

    if (btn1_was_pressed || btn2_was_pressed || key != NO_KEY) {
        exit();
        *_appStatePtr = APP_STATE_MAIN_MENU;
        _mainMenuDisplayFunc();
    }
}

/**
 * @brief Termina forzatamente la partita in corso.
 * @details Chiamata quando viene ricevuto il comando di rete corrispondente.
 * Calcola il vincitore in base al tempo di possesso attuale e passa allo stato GAME_OVER.
 */
void DominationMode::forceEndGame() {
    Serial.println("!!! COMANDO RICEVUTO: forceEndGame in Dominio !!!");

    if (_currentState == ModeState::GAME_OVER) {
        return;
    }
    
    _currentState = ModeState::GAME_OVER;
    _hardware->playToneAsync(400, 1000);

    // Aggiorna punteggi finali
    unsigned long now = millis();
    if (_lastZoneState == ModeState::TEAM1_CAPTURED) {
        _team1PossessionTime += now - _lastPossessionUpdateTime;
    } else if (_lastZoneState == ModeState::TEAM2_CAPTURED) {
        _team2PossessionTime += now - _lastPossessionUpdateTime;
    }

    if (_team1PossessionTime > _team2PossessionTime) _endGameStatus = "ALPHA WINS";
    else if (_team2PossessionTime > _team1PossessionTime) _endGameStatus = "BRAVO WINS";
    else _endGameStatus = "DRAW"; // O STOPPED

    // Imposta _winner per effetti
    if (_endGameStatus == "ALPHA WINS") _winner = 1;
    else if (_endGameStatus == "BRAVO WINS") _winner = 2;
    else _winner = 0;

    _hardware->clearLcd();
    _hardware->printLcd(2, 0, "PARTITA TERMINATA");
    if (_winner == 1) _hardware->printLcd(2, 1, "VINCE SQUADRA 1!");
    else if (_winner == 2) _hardware->printLcd(2, 1, "VINCE SQUADRA 2!");
    else _hardware->printLcd(6, 1, "PAREGGIO!");
    
    char scoreBuffer[20];
    sprintf(scoreBuffer, "S1: %02lu:%02lu", (_team1PossessionTime / 1000) / 60, (_team1PossessionTime / 1000) % 60);
    _hardware->printLcd(6, 2, scoreBuffer);
    sprintf(scoreBuffer, "S2: %02lu:%02lu", (_team2PossessionTime / 1000) / 60, (_team2PossessionTime / 1000) % 60);
    _hardware->printLcd(6, 3, scoreBuffer);

    _hardware->printOled1("ESCI", 2, 35, 25);
    _hardware->printOled2("ESCI", 2, 35, 25);
    
    sendTelemetry();
}

void DominationMode::forceWin(String winnerTeam) {
    if (_currentState == ModeState::GAME_OVER) return;

    _currentState = ModeState::GAME_OVER;
    _hardware->playToneAsync(400, 1000);

    // Congela i tempi di possesso attuali
    unsigned long now = millis();
    if (_lastZoneState == ModeState::TEAM1_CAPTURED) {
        _team1PossessionTime += now - _lastPossessionUpdateTime;
    } else if (_lastZoneState == ModeState::TEAM2_CAPTURED) {
        _team2PossessionTime += now - _lastPossessionUpdateTime;
    }

    if (winnerTeam == "ALPHA") {
        _endGameStatus = "ALPHA WINS";
        _winner = 1;
    } else {
        _endGameStatus = "BRAVO WINS";
        _winner = 2;
    }

    _hardware->clearLcd();
    _hardware->printLcd(0, 0, "SQUADRA ELIMINATA!");
    if (_winner == 1) _hardware->printLcd(2, 1, "VINCE SQUADRA 1!");
    else if (_winner == 2) _hardware->printLcd(2, 1, "VINCE SQUADRA 2!");
    
    char scoreBuffer[20];
    sprintf(scoreBuffer, "S1: %02lu:%02lu", (_team1PossessionTime / 1000) / 60, (_team1PossessionTime / 1000) % 60);
    _hardware->printLcd(6, 2, scoreBuffer);
    sprintf(scoreBuffer, "S2: %02lu:%02lu", (_team2PossessionTime / 1000) / 60, (_team2PossessionTime / 1000) % 60);
    _hardware->printLcd(6, 3, scoreBuffer);

    _hardware->printOled1("ESCI", 2, 35, 25);
    _hardware->printOled2("ESCI", 2, 35, 25);
    
    sendTelemetry();
}

void DominationMode::sendSettingsStatus() {
    JsonDocument doc;
    doc["type"] = "SETTINGS_UPDATE"; // Tag generico
    doc["duration"] = _settings->getGameDuration();
    doc["capture_time"] = _settings->getCaptureTime();
    doc["countdown"] = _settings->getCountdownDuration();
    _network->sendEvent("SETTINGS_UPDATE", doc);
}