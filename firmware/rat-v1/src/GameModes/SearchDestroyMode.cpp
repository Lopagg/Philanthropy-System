// src/GameModes/SearchDestroyMode.cpp

/**
 * @file SearchDestroyMode.cpp
 * @brief Implementazione della classe SearchDestroyMode.
 */

#include "SearchDestroyMode.h"
#include <ArduinoJson.h>

extern String team1_name;
extern String team2_name;
extern uint8_t team1_color[3];
extern uint8_t team2_color[3];

/**
 * @brief Costruttore.
 * @details Inizializza tutte le variabili membro con i loro valori di default.
 * Viene chiamato una sola volta in main.cpp alla creazione dell'oggetto sdMode.
 */
SearchDestroyMode::SearchDestroyMode(HardwareManager* hardware, NetworkManager* network, SearchDestroySettings* settings, AppState* appState, MainMenuDisplayFunction displayFunc)
    : _hardware(hardware),
      _network(network),
      _settings(settings),
      _appStatePtr(appState),
      _mainMenuDisplayFunc(displayFunc),
      _currentState(ModeState::MODE_SUB_MENU),
      _menuIndex(0),
      _subMenuIndex(0),
      _tempBoolSelection(true),
      _armingStartTime(0),
      _armingSoundLastUpdate(0),
      _defusingStartTime(0),
      _stateChangeTime(0),
      _lastDisplayedSeconds(-1),
      _gameIsActive(false),
      _bombIsActive(false),
      _isServerAuthoritative(false),
      _lastTelemetryTime(0),
      _endGameStatus(""),
      _pendingServerEvent(""),
      _lastServerRetryTime(0) {
}

/**
 * @brief Funzione di ingresso della modalità.
 * @details Chiamata dal menu principale, imposta lo stato iniziale della modalità
 * e visualizza la prima schermata (il sottomenu). Invia anche i messaggi di
 * rete per notificare l'ingresso nella modalità e lo stato delle impostazioni.
 */
void SearchDestroyMode::enter() {
    Serial.println("Entrato in modalita' Cerca & Distruggi");
    _currentState = ModeState::MODE_SUB_MENU;
    _subMenuIndex = 0;
    _gameIsActive = false;
    _bombIsActive = false;
    _isServerAuthoritative = false;
    _endGameStatus = ""; // Reset vincitore
    displaySubMenu();
    _hardware->setStripColor(255, 100, 0);
    
    // --- MODIFICA QUI ---
    // Inviamo solo il cambio di modalità. 
    // I dati delle regole (timer, pin, ecc.) li lasciamo a sendSettingsStatus()
    // per evitare che una lista parziale sovrascriva quella completa sul sito.
    
    JsonDocument doc;
    doc["mode"] = "SEARCH_AND_DESTROY";
    _network->sendEvent("MODE_ENTER", doc);
    // --------------------

    sendSettingsStatus(); // Questo invierà la lista completa delle impostazioni
}

/**
 * @brief Funzione di ingresso diretto in partita, saltando i menu.
 */
void SearchDestroyMode::enterInGame() {
    JsonDocument doc;
    doc["mode"] = "SEARCH_AND_DESTROY";
    _network->sendEvent("MODE_ENTER", doc);
    
    sendSettingsStatus();
    Serial.println("Entrato in Cerca & Distruggi (remoto)");

    _gameMatchStartTime = _hardware->getRTCTime();
    _gameIsActive = true;
    _bombIsActive = false;
    _isServerAuthoritative = true;
    _endGameStatus = ""; // <-- Aggiunto reset status per le partite in sequenza
    
    // Opzionale: notifica avvio gioco immediato
    // _network->sendEvent("GAME_START");

    _hardware->playTone(1500, 150);
    _currentState = ModeState::IN_GAME_AWAIT_ARM; 
    displayAwaitArmScreen();
}

/**
 * @brief Ciclo principale della modalità.
 * @details Chiamata ad ogni iterazione del loop() di main.cpp quando questa modalità è attiva.
 * Legge lo stato dei pulsanti e, in base allo stato interno (_currentState),
 * delega il lavoro alla funzione di gestione appropriata (es. handleSubMenuInput, handleInGame, etc.).
 */
void SearchDestroyMode::loop() {
    char key = _hardware->getKey();
    bool btn1_is_pressed = _hardware->isButton1Pressed(); 
    bool btn1_was_pressed = _hardware->wasButton1Pressed();
    bool btn2_is_pressed = _hardware->isButton2Pressed();
    bool btn2_was_pressed = _hardware->wasButton2Pressed();

    // --- GESTIONE TELEMETRIA ---

    bool isHighAction = (_currentState == ModeState::IN_GAME_IS_ARMING || _currentState == ModeState::IN_GAME_IS_DEFUSING);
    unsigned long interval = isHighAction ? 100 : 1000;
    
    if (millis() - _lastTelemetryTime > interval) {
        _lastTelemetryTime = millis();
        sendTelemetry();
    }

    if (_currentState >= ModeState::IN_GAME_CONFIRM) {
        handleInGame(key, btn1_is_pressed, btn1_was_pressed, btn2_is_pressed, btn2_was_pressed);
    } else {
        switch (_currentState) {
            case ModeState::MODE_SUB_MENU:
                handleSubMenuInput(key, btn1_was_pressed, btn2_was_pressed);
                break;
            case ModeState::MENU_SETTINGS:
                handleSettingsInput(key, btn1_was_pressed, btn2_was_pressed);
                break;
            case ModeState::EDIT_BOMB_TIME:
            case ModeState::EDIT_GAME_DURATION:
            case ModeState::EDIT_ARM_PIN:
            case ModeState::EDIT_DISARM_PIN:
            case ModeState::EDIT_ARM_TIME:
            case ModeState::EDIT_DEFUSE_TIME:
                handleEditInput(key, btn1_was_pressed, btn2_was_pressed);
                break;
            case ModeState::EDIT_USE_ARM_PIN:
            case ModeState::EDIT_USE_DISARM_PIN:
                handleBooleanEditInput(key, btn1_was_pressed, btn2_was_pressed);
                break;
            default:
                _currentState = ModeState::MODE_SUB_MENU;
                displaySubMenu();
                break;
        }
    }
}

/**
 * @brief Funzione di uscita dalla modalità.
 * @details Chiamata quando l'utente sceglie di tornare al menu principale.
 * Invia il messaggio di rete di uscita, salva le impostazioni e spegne i display.
 */
void SearchDestroyMode::exit() {
    Serial.println("Uscito da modalita' Cerca & Distruggi");
    
    JsonDocument doc;
    doc["mode"] = "SEARCH_AND_DESTROY";
    _network->sendEvent("MODE_EXIT", doc);

    _settings->saveParameters();
    _hardware->turnOffStrip();
    _hardware->clearOled1();
    _hardware->clearOled2();
}

// --- Funzioni di Gestione Input ---

/**
 * @brief Gestisce la logica del sottomenu principale ("Inizia Partita", "Impostazioni").
 * @details Fase del gioco: Menu.
 */
void SearchDestroyMode::handleSubMenuInput(char key, bool btn1, bool btn2) {
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
            case 0: _currentState = ModeState::IN_GAME_CONFIRM; displayConfirmScreen(); break;
            case 1: _currentState = ModeState::MENU_SETTINGS; _menuIndex = 0; displaySettingsMenu(); break;
        }
    }
}

/**
 * @brief Gestisce la logica del menu delle impostazioni.
 * @details Fase del gioco: Menu.
 */
void SearchDestroyMode::handleSettingsInput(char key, bool btn1, bool btn2) {
    String menuItems[] = { "Timer Bomba", "Durata Partita", "PIN Armamento", "PIN Disarmo", "Tempo Armamento", "Tempo Disarmo", "Usa PIN armamento", "Usa PIN disarmo" };
    int numItems = sizeof(menuItems) / sizeof(menuItems[0]);
    if (key == '2') {
        _hardware->playTone(800, 50); _menuIndex = (_menuIndex - 1 + numItems) % numItems; displaySettingsMenu();
    } else if (key == '8') {
        _hardware->playTone(600, 50); _menuIndex = (_menuIndex + 1) % numItems; displaySettingsMenu();
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
            case 0: _currentState = ModeState::EDIT_BOMB_TIME; break;
            case 1: _currentState = ModeState::EDIT_GAME_DURATION; break;
            case 2: _currentState = ModeState::EDIT_ARM_PIN; break;
            case 3: _currentState = ModeState::EDIT_DISARM_PIN; break;
            case 4: _currentState = ModeState::EDIT_ARM_TIME; break;
            case 5: _currentState = ModeState::EDIT_DEFUSE_TIME; break;
            case 6: _currentState = ModeState::EDIT_USE_ARM_PIN; _tempBoolSelection = _settings->getUseArmingPin(); break;
            case 7: _currentState = ModeState::EDIT_USE_DISARM_PIN; _tempBoolSelection = _settings->getUseDisarmingPin(); break;
        }
        updateDisplayForCurrentState();
    }
}

/**
 * @brief Gestisce la logica delle schermate di modifica dei valori (numeri o stringhe).
 * @details Fase del gioco: Menu Impostazioni.
 */
void SearchDestroyMode::handleEditInput(char key, bool btn1, bool btn2) {
    if (btn1) {
        _hardware->playTone(300, 70); _currentState = ModeState::MENU_SETTINGS;
        displaySettingsMenu();
        _hardware->setStripColor(255, 100, 0); 
        return;
    }
    bool displayNeedsUpdate = false;
    if (isalnum(key) || key == '*') {
        _hardware->playTone(700, 30);
        if (key == '*') { if (_currentInputBuffer.length() > 0) _currentInputBuffer.remove(_currentInputBuffer.length() - 1); }
        else {
            bool isPinEdit = (_currentState == ModeState::EDIT_ARM_PIN || _currentState == ModeState::EDIT_DISARM_PIN);
            if (isPinEdit) { if (_currentInputBuffer.length() < 8) _currentInputBuffer += key; }
            else { _currentInputBuffer += key; }
        }
        displayNeedsUpdate = true;
    }
    if (btn2) {
        _hardware->playTone(1200, 100);
        bool isValid = false;
        bool isPinEdit = (_currentState == ModeState::EDIT_ARM_PIN || _currentState == ModeState::EDIT_DISARM_PIN);
        if (isPinEdit) { if (_currentInputBuffer.length() >= 1 && _currentInputBuffer.length() <= 8) isValid = true; }
        else { if (_currentInputBuffer.length() > 0) isValid = true; }
        if (isValid) {
            switch (_currentState) {
                case ModeState::EDIT_BOMB_TIME: _settings->setBombTime(_currentInputBuffer.toInt()); break;
                case ModeState::EDIT_GAME_DURATION: _settings->setGameDuration(_currentInputBuffer.toInt()); break;
                case ModeState::EDIT_ARM_PIN: _settings->setArmingPin(_currentInputBuffer); break;
                case ModeState::EDIT_DISARM_PIN: _settings->setDisarmingPin(_currentInputBuffer); break;
                case ModeState::EDIT_ARM_TIME: _settings->setArmingTime(_currentInputBuffer.toInt()); break;
                case ModeState::EDIT_DEFUSE_TIME: _settings->setDefuseTime(_currentInputBuffer.toInt()); break;
                default: break;
            }
            _settings->saveParameters();
            _currentState = ModeState::MENU_SETTINGS; displaySettingsMenu(); _hardware->setStripColor(255, 100, 0);
        } else {
            _hardware->clearLcd(); _hardware->printLcd(0, 1, "Errore!");
            if (isPinEdit) { _hardware->printLcd(0, 2, "Il PIN deve avere"); _hardware->printLcd(0, 3, "da 1 a 8 caratteri."); }
            else { _hardware->printLcd(0, 2, "Valore non valido."); }
            delay(2000); updateDisplayForCurrentState();
        }
        return;
    }
    if (displayNeedsUpdate) updateDisplayForCurrentState();
}

/**
 * @brief Gestisce la logica delle schermate di modifica dei valori booleani (Sì/No).
 * @details Fase del gioco: Menu Impostazioni.
 */
void SearchDestroyMode::handleBooleanEditInput(char key, bool btn1, bool btn2) {
    if (btn1) {
        _hardware->playTone(300, 70); _currentState = ModeState::MENU_SETTINGS;
        displaySettingsMenu(); _hardware->setStripColor(255, 100, 0); return;
    }
    if (key == '2' || key == '8') {
        _hardware->playTone(700, 30); _tempBoolSelection = !_tempBoolSelection; updateDisplayForCurrentState();
    }
    if (btn2) {
        _hardware->playTone(1200, 100);
        if (_currentState == ModeState::EDIT_USE_ARM_PIN) _settings->setUseArmingPin(_tempBoolSelection);
        else if (_currentState == ModeState::EDIT_USE_DISARM_PIN) _settings->setUseDisarmingPin(_tempBoolSelection);
        _settings->saveParameters(); _currentState = ModeState::MENU_SETTINGS;
        displaySettingsMenu(); _hardware->setStripColor(255, 100, 0); return;
    }
}

/**
 * @brief Gestisce tutta la logica quando la partita è in corso.
 * @details Fase del gioco: Dalla conferma di inizio fino alla fine della partita.
 * Contiene la logica del timer e una macchina a stati per gestire le azioni
 * del giocatore (innesco, disinnesco, inserimento PIN, ecc.).
 */
void SearchDestroyMode::handleInGame(char key, bool btn1_is_pressed, bool btn1_was_pressed, bool btn2_is_pressed, bool btn2_was_pressed) {
    
    // --- QoS APPLICATIVO: Retry Loop per eventi critici ---
    if (_currentState == ModeState::IN_GAME_WAITING_SERVER) {
        if (millis() - _lastServerRetryTime > 2000) {
            _lastServerRetryTime = millis();
            JsonDocument doc;
            doc["event"] = _pendingServerEvent;
            _network->sendEvent("MATCH_EVENT", doc);
            Serial.println("--- RETRY: Re-invio MATCH_EVENT al server ---");
        }
        return; // Finché aspetta, ignora tutti gli altri input
    }

    // GESTIONE TIMER GLOBALE PARTITA ---
    if (_gameIsActive && !_isServerAuthoritative) {
        long totalMatchSeconds = _settings->getGameDuration() * 60;
        TimeSpan matchElapsed = _hardware->getRTCTime() - _gameMatchStartTime;
        long remainingMatchSeconds = totalMatchSeconds - matchElapsed.totalseconds();

        if (remainingMatchSeconds <= 0 && _currentState != ModeState::IN_GAME_ENDED && _currentState != ModeState::IN_GAME_DEFUSED) {
            _endGameStatus = "CT WINS"; 
            _currentState = ModeState::IN_GAME_ENDED;
            _gameIsActive = false;
            
            _hardware->noTone();
            _hardware->clearLcd();
            _hardware->printLcd(1, 1, "TEMPO SCADUTO!");
            _hardware->printLcd(0, 2, "Vince " + team2_name + "!"); // Colore Dinamico del nome (CT)
            _hardware->playToneAsync(1000, 1000);
            
            _hardware->setStripColor(team2_color[0], team2_color[1], team2_color[2]); // Colore Dinamico CT
            
            sendTelemetry();
            return;
        }
    }

    // Prima parte: gestione del timer principale della bomba (se attivo)
    if (_bombIsActive) {
        long totalSeconds = _settings->getBombTime() * 60;
        TimeSpan elapsed = _hardware->getRTCTime() - _roundStartTime;
        long remainingSeconds = totalSeconds - elapsed.totalseconds();
        if (remainingSeconds < 0) remainingSeconds = 0;

        // Gestione aggiornamento display ...
        if (remainingSeconds != _lastDisplayedSeconds) {
            JsonDocument doc;
            doc["time"] = remainingSeconds;
            _network->sendEvent("TIME_UPDATE", doc);

            if (_currentState == ModeState::IN_GAME_COUNTDOWN) updateCountdownDisplay(remainingSeconds);
            else {
                char timeBuffer[10];
                sprintf(timeBuffer, "%02d : %02d", (int)(remainingSeconds/60), (int)(remainingSeconds%60));
                _hardware->printLcd(6, 3, timeBuffer);
            }
            if (remainingSeconds > 60 && remainingSeconds % 60 == 0) {
                _hardware->playTone(1500, 150);
                _hardware->setBrightness(255); _hardware->setStripColor(255, 0, 0); delay(400);
                _hardware->turnOffStrip(); delay(500); 
                _hardware->setStripColor(255, 0, 0); delay(400);
            } else if (remainingSeconds == 60 || remainingSeconds == 30) {
                _hardware->playTone(1600, 80); delay(100); _hardware->playTone(1600, 80);
                _hardware->setBrightness(255); _hardware->setStripColor(255, 0, 0); delay(200);
                _hardware->turnOffStrip(); delay(100);
                _hardware->setBrightness(255); _hardware->setStripColor(255, 0, 0); delay(200);
            }
            _lastDisplayedSeconds = remainingSeconds;
        }

        if (remainingSeconds <= 10 && remainingSeconds > 0) {
            int interval = map(remainingSeconds, 10, 1, 1000, 100);
            int frequency = map(remainingSeconds, 10, 1, 1200, 2200);
            if(millis() % interval < (interval / 2)) {
                _hardware->setBrightness(255); _hardware->setStripColor(255, 0, 0);
                _hardware->updateTone(frequency);
            } else _hardware->turnOffStrip(); _hardware->noTone();
        } else if (remainingSeconds > 0 && _currentState == ModeState::IN_GAME_COUNTDOWN) {
            _hardware->updateBreathingEffect(255, 0, 0);
        }

        // SCADENZA TEMPO BOMBA
        if (remainingSeconds <= 0) {
            _bombIsActive = false;
            _gameIsActive = false; 

            if (_isServerAuthoritative) {
                _hardware->noTone();
                _hardware->clearLcd();
                _hardware->printLcd(3, 1, "BOMBA ESPLOSA!");
                _hardware->printLcd(0, 2, "Invio dati...");
                
                // FEEDBACK IMMEDIATO LOCALE: Simula lo scoppio visivo prima di contattare il server
                for(int i=0; i<3; i++) {
                    _hardware->setBrightness(255);
                    _hardware->setStripColor(255, 255, 255); _hardware->playTone(2000, 50);
                    _hardware->setStripColor(255, 100, 0); _hardware->playTone(1000, 80);
                    _hardware->setStripColor(255, 0, 0); _hardware->playTone(400, 100);
                }
                
                // --- PREPARA IL QoS APPLICATIVO ---
                _pendingServerEvent = "BOMB_EXPLODED";
                _lastServerRetryTime = 0; // Forza l'invio immediato al prossimo loop
                _currentState = ModeState::IN_GAME_WAITING_SERVER;
                // ----------------------------------
                
                _hardware->printLcd(0, 2, "Attesa Arbitro...");
                _hardware->playToneAsync(150, 3000); // Il fischio di sottofondo continua mentre aspettiamo
                
                _currentState = ModeState::IN_GAME_WAITING_SERVER;
            } else {

                // LOGICA OFFLINE (Vince Subito T)
                _endGameStatus = "T WINS"; 
                _currentState = ModeState::IN_GAME_ENDED;
                
                JsonDocument doc;
                doc["winner"] = "TERRORISTS";
                doc["reason"] = "TIME_EXPIRED";
                _network->sendEvent("GAME_END", doc);

                _hardware->noTone();
                _hardware->clearLcd();
                _hardware->printLcd(3, 1, "BOMBA ESPLOSA!");
                _hardware->printLcd(0, 2, "Vince " + team1_name + "!"); // Colore Dinamico del nome (T)
                _hardware->printOled1("ESCI", 2, 35, 25);
                _hardware->printOled2("ESCI", 2, 35, 25);

                for(int i=0; i<3; i++) {
                    _hardware->setBrightness(255);
                    _hardware->setStripColor(255, 255, 255); _hardware->playTone(2000, 50);
                    _hardware->setStripColor(255, 100, 0); _hardware->playTone(1000, 80);
                    _hardware->setStripColor(255, 0, 0); _hardware->playTone(400, 100);
                }
                
                _hardware->setStripColor(team1_color[0], team1_color[1], team1_color[2]); // Colore Dinamico T
                _hardware->playToneAsync(150, 3000);
            }
            sendTelemetry();
            return;
        }
    }

    // Seconda parte: macchina a stati per le azioni del giocatore
    switch (_currentState) {
        case ModeState::IN_GAME_CONFIRM:    // case IN_GAME_CONFIRM: gestisce la schermata "Iniziare la partita?"
            if (btn1_was_pressed) { 
                _currentState = ModeState::MODE_SUB_MENU; displaySubMenu();
                //_network->sendEvent("ROUND_RESET"); 
            }
            if (btn2_was_pressed) { 
                _network->sendEvent("GAME_START");
                _hardware->playTone(1500, 150);

                // Salva il momento di inizio partita ---
                _gameMatchStartTime = _hardware->getRTCTime();
                _gameIsActive = true;  // Il timer partita parte
                _bombIsActive = false; // La bomba è ferma!

                _currentState = ModeState::IN_GAME_AWAIT_ARM; 
                displayAwaitArmScreen();
            }
            break;
        case ModeState::IN_GAME_AWAIT_ARM:  // case IN_GAME_AWAIT_ARM: la partita è iniziata, il dispositivo attende che la squadra T inneschi la bomba.
            _hardware->updateBreathingEffect(120, 120, 120);
            if (btn1_is_pressed) {
                _network->sendEvent("ARM_START");
                _currentState = ModeState::IN_GAME_IS_ARMING; 
                _armingStartTime = millis(); 
                _armingSoundLastUpdate = 0;
                _hardware->clearLcd(); 
                _hardware->printLcd(2, 1, "INNESCO IN CORSO");
            }
            break;
        case ModeState::IN_GAME_IS_ARMING: {    // case IN_GAME_IS_ARMING: un giocatore T sta tenendo premuto il pulsante di innesco.
            unsigned long armTime = _settings->getArmingTime() * 1000;
            unsigned long elapsed = millis() - _armingStartTime;
            if (!btn1_is_pressed) {
                _network->sendEvent("ARM_CANCEL");
                _currentState = ModeState::IN_GAME_AWAIT_ARM; 
                displayAwaitArmScreen(); 
                _hardware->noTone();
                return;
            }
            if (elapsed >= armTime) {
                _hardware->noTone();
                if (_settings->getUseArmingPin()) {
                    _currentState = ModeState::IN_GAME_ENTER_ARM_PIN;   // case IN_GAME_ENTER_ARM_PIN: gestisce l'inserimento del PIN di innesco.
                    _currentInputBuffer = "";
                    displayEnterPinScreen("INSERIRE PIN INNESCO");
                } else {
                    _currentState = ModeState::IN_GAME_ARMED;   // case IN_GAME_ARMED: stato transitorio dopo l'innesco, prima che parta il timer.
                    _hardware->clearLcd(); 
                    _hardware->printLcd(2, 1, "BOMBA INNESCATA!");
                    _hardware->setStripColor(255, 0, 0); 
                    _hardware->playTone(1000, 80); 
                    _hardware->playTone(1200, 80); 
                    _hardware->playTone(1500, 100);
                    _stateChangeTime = millis();
                }
                return;
            }
            displayArmingScreen(elapsed);
            if (millis() - _armingSoundLastUpdate > 50) {
                _armingSoundLastUpdate = millis();
                int freq = map(elapsed, 0, armTime, 400, 1200);
                _hardware->updateTone(freq);
            }
            break;
        }
        case ModeState::IN_GAME_ENTER_ARM_PIN: {
            if (btn1_was_pressed) { 
                _currentState = ModeState::IN_GAME_AWAIT_ARM; 
                displayAwaitArmScreen(); 
                return; 
            }
            bool needsUpdate = false;
            if (isalnum(key)) { 
                _hardware->playTone(700, 30); 
                _currentInputBuffer += key; 
                needsUpdate = true;
            } else if (key == '*') { 
                _hardware->playTone(700, 30); 
                if (_currentInputBuffer.length() > 0) { 
                    _currentInputBuffer.remove(_currentInputBuffer.length() - 1); 
                } 
                needsUpdate = true; 
            }
            if (needsUpdate) { 
                displayEnterPinScreen("INSERIRE PIN INNESCO"); 
            }
            if (_currentInputBuffer.length() >= _settings->getArmingPin().length()) {
                if (_currentInputBuffer == _settings->getArmingPin()) {
                    _currentState = ModeState::IN_GAME_ARMED; 
                    _hardware->clearLcd(); 
                    _hardware->printLcd(2, 1, "BOMBA INNESCATA!");
                    _hardware->setStripColor(255, 0, 0); 
                    _hardware->playTone(1000, 80); 
                    _hardware->playTone(1200, 80); 
                    _hardware->playTone(1500, 100);
                    _stateChangeTime = millis();
                } else {
                    _network->sendEvent("ARM_PIN_WRONG");
                    _hardware->clearLcd(); 
                    _hardware->printLcd(5, 1, "PIN ERRATO"); 
                    _hardware->printLcd(5, 2, "Riprovare"); 
                    _hardware->playTone(200, 500); 
                    delay(2000); 
                    _currentInputBuffer = "";
                    displayEnterPinScreen("INSERIRE PIN INNESCO");
                }
            }
            break;
        }
        case ModeState::IN_GAME_ARMED:
            if (millis() - _stateChangeTime > 1000) {
                _network->sendEvent("BOMB_ARMED");
                _currentState = ModeState::IN_GAME_COUNTDOWN; 
                _roundStartTime = _hardware->getRTCTime();
                _lastDisplayedSeconds = -1; 
                _bombIsActive = true; 
                displayCountdownLayout();
                sendTelemetry();
            }
            break;
        case ModeState::IN_GAME_COUNTDOWN:  // case IN_GAME_COUNTDOWN: la bomba è innescata, il timer scorre e si attende un disinnesco.
            if (btn2_is_pressed && btn2_was_pressed) {
                _network->sendEvent("DEFUSE_START");
                _currentState = ModeState::IN_GAME_IS_DEFUSING; 
                _defusingStartTime = millis(); 
                _armingSoundLastUpdate = 0;
                _hardware->clearLcd(); 
                displayCountdownLayout();
                _hardware->printLcd(5, 1, "DISINNESCO");
            }
            break;
        case ModeState::IN_GAME_IS_DEFUSING: {  // case IN_GAME_IS_DEFUSING: un giocatore CT sta tenendo premuto il pulsante di disinnesco.
            unsigned long defuseTime = _settings->getDefuseTime() * 1000;
            unsigned long elapsed = millis() - _defusingStartTime;
            if (!btn2_is_pressed) {
                _network->sendEvent("DEFUSE_CANCEL");
                _currentState = ModeState::IN_GAME_COUNTDOWN; 
                _hardware->noTone(); 
                displayCountdownLayout(); 
                return;
            }
            if (elapsed >= defuseTime) {
                _hardware->noTone();
                if (_settings->getUseDisarmingPin()) {
                    _currentState = ModeState::IN_GAME_ENTER_DEFUSE_PIN;    // case IN_GAME_ENTER_DEFUSE_PIN: gestisce l'inserimento del PIN di disinnesco.
                    _currentInputBuffer = "";
                    displayEnterPinScreen("INSERIRE PIN");
                } else {
                    // Sostituita vecchia logica hardcoded con la chiamata all'helper
                    triggerDefuse(); 
                }
                return;
            }
            displayDefusingScreen(elapsed);
            if (millis() - _armingSoundLastUpdate > 50) {
                _armingSoundLastUpdate = millis();
                int freq = map(elapsed, 0, defuseTime, 1200, 400);
                _hardware->updateTone(freq);
            }
            break;
        }
        case ModeState::IN_GAME_ENTER_DEFUSE_PIN: {
            if (btn1_was_pressed) { 
                _currentState = ModeState::IN_GAME_COUNTDOWN; 
                displayCountdownLayout(); 
                return; 
            }
            bool needsUpdate = false;
            if (isalnum(key)) { 
                _hardware->playTone(700, 30); 
                _currentInputBuffer += key; 
                needsUpdate = true;
            } else if (key == '*') { 
                _hardware->playTone(700, 30); 
                if (_currentInputBuffer.length() > 0) { 
                    _currentInputBuffer.remove(_currentInputBuffer.length()-1); 
                } 
                needsUpdate = true; 
            }
            if (needsUpdate) { 
                displayEnterPinScreen("INSERIRE PIN"); 
            }
            if(millis() % 1000 < 500) 
                _hardware->setStripColor(0,255,0); 
            else 
                _hardware->turnOffStrip();
            if (_currentInputBuffer.length() >= _settings->getDisarmingPin().length()) {
                if (_currentInputBuffer == _settings->getDisarmingPin()) {
                    // Sostituita vecchia logica hardcoded con la chiamata all'helper
                    triggerDefuse();
                } else {
                    _network->sendEvent("DEFUSE_PIN_WRONG");
                    _hardware->clearLcd(); 
                    _hardware->printLcd(5, 1, "PIN ERRATO");
                    _hardware->printLcd(5, 2, "Riprovare"); 
                    _hardware->playTone(200, 500);
                    delay(2000); 
                    _currentInputBuffer = ""; 
                    displayEnterPinScreen("INSERIRE PIN");
                }
            }
            break;
        }
        case ModeState::IN_GAME_DEFUSED:
            _hardware->printOled1("ESCI", 2, 35, 25);
            _hardware->printOled2("ESCI", 2, 35, 25);
            
            // Effetto vincita con colore CT dinamico
            _hardware->updateBreathingEffect(team2_color[0], team2_color[1], team2_color[2]);
            
            if (btn1_was_pressed || btn2_was_pressed || key != NO_KEY) {
            exit();
            *_appStatePtr = APP_STATE_MAIN_MENU;
            _mainMenuDisplayFunc();
            }
            break;
        case ModeState::IN_GAME_ENDED:  // case IN_GAME_ENDED: la partita è finita, i T hanno vinto.
            if (btn1_was_pressed || btn2_was_pressed || key != NO_KEY) {
                exit();
                *_appStatePtr = APP_STATE_MAIN_MENU;
                _mainMenuDisplayFunc();
            }
            break;
    }
}

void SearchDestroyMode::triggerDefuse() {
    _bombIsActive = false;
    _gameIsActive = false; 

    if (_isServerAuthoritative) {
        _hardware->clearLcd();
        _hardware->printLcd(1, 1, "BOMBA DISINNESCATA");
        _hardware->printLcd(0, 2, "Invio dati...");
        
        // FEEDBACK IMMEDIATO LOCALE: Suona la vittoria prima di chiamare il server
        _hardware->playTone(1500, 80); 
        _hardware->playTone(1800, 80); 
        _hardware->playTone(2200, 200);
        
        // --- PREPARA IL QoS APPLICATIVO ---
        _pendingServerEvent = "BOMB_DEFUSED";
        _lastServerRetryTime = 0; // Forza l'invio immediato
        _currentState = ModeState::IN_GAME_WAITING_SERVER;
        // ----------------------------------
        
        _hardware->printLcd(0, 2, "Attesa Arbitro... ");
        _currentState = ModeState::IN_GAME_WAITING_SERVER;
    } else {
        JsonDocument doc;
        doc["winner"] = "COUNTER_TERRORISTS";
        doc["reason"] = "BOMB_DEFUSED";
        _network->sendEvent("GAME_END", doc);

        _endGameStatus = "CT WINS"; 
        _currentState = ModeState::IN_GAME_DEFUSED; 
        
        _hardware->clearLcd(); 
        _hardware->printLcd(1, 1, "BOMBA DISINNESCATA");
        _hardware->printLcd(0, 2, "Vince " + team2_name + "!"); // Colore Dinamico del nome (CT)
        _hardware->playTone(1500, 80); 
        _hardware->playTone(1800, 80); 
        _hardware->playTone(2200, 100);
        
        _hardware->setStripColor(team2_color[0], team2_color[1], team2_color[2]); // Colore Dinamico CT
    }
    sendTelemetry();
}

void SearchDestroyMode::forceWin(String winnerTeam, String reason) {
    Serial.println("!!! COMANDO RICEVUTO: forceWin in Cerca & Distruggi !!!");
    
    if (_currentState == ModeState::IN_GAME_DEFUSED || _currentState == ModeState::IN_GAME_ENDED) {
        return;
    }

    // La valigetta si adatta al verdetto, indipendentemente dal nome della fazione.
    bool isAttackerWin = (reason == "BOMBA ESPLOSA!" || (winnerTeam != "" && reason == "SQUADRA ELIMINATA!" && _bombIsActive));
    
    if (reason == "BOMBA ESPLOSA!" || (reason == "SQUADRA ELIMINATA!" && winnerTeam.indexOf("ALPHA") != -1) && reason != "BOMBA DISINNESCATA!") {
         _endGameStatus = "T WINS"; 
         _currentState = ModeState::IN_GAME_ENDED; 
    } else {
         _endGameStatus = "CT WINS"; 
         _currentState = ModeState::IN_GAME_DEFUSED; 
    }
    
    _gameIsActive = false; 
    _bombIsActive = false; 
    _hardware->noTone();
    _hardware->clearLcd();
    
    if (reason.length() > 0) {
        _hardware->printLcd(0, 1, reason);
    } else {
        _hardware->printLcd(1, 1, "SQUADRA ELIMINATA!"); 
    }
    
    // Identifica e stampa il NOME VERO della squadra vincente
    String displayWinner = "Vince: " + (winnerTeam.indexOf("ALPHA") != -1 ? team1_name : team2_name);
    _hardware->printLcd(0, 2, displayWinner);
    
    if (_currentState == ModeState::IN_GAME_ENDED) {
        if (reason != "BOMBA ESPLOSA!") { 
            _hardware->playToneAsync(150, 3000);
        }
        _hardware->setStripColor(team1_color[0], team1_color[1], team1_color[2]); // Colore Dinamico T
    } else {
        if (reason != "BOMBA DISINNESCATA!") {
            _hardware->playToneAsync(1500, 80); 
        }
        _hardware->setStripColor(team2_color[0], team2_color[1], team2_color[2]); // Colore Dinamico CT
    }

    sendTelemetry();
}


// --- Funzioni di Visualizzazione (display...) ---
// Ognuna di queste funzioni è responsabile del disegno di una specifica schermata sull'LCD e sugli OLED.
// Vengono chiamate dalle funzioni di gestione dell'input per aggiornare l'interfaccia utente.

void SearchDestroyMode::displaySubMenu() {
    _hardware->clearLcd(); _hardware->printLcd(0, 0, "CERCA & DISTRUGGI");
    String menuItems[] = { "Inizia Partita", "Impostazioni" };
    for (int i = 0; i < 2; i++) {
        String prefix = (i == _subMenuIndex) ? "> " : "  ";
        _hardware->printLcd(0, i + 1, prefix + menuItems[i]);
    }
    _hardware->printOled1("INDIETRO", 2, 10, 25);
    _hardware->printOled2("CONFERMA", 2, 18, 25);
}
void SearchDestroyMode::displaySettingsMenu() {
    _hardware->clearLcd(); _hardware->printLcd(0, 0, "IMPOSTAZIONI S&D");
    String menuItems[] = { "Timer Bomba", "Durata Partita", "PIN Armamento", "PIN Disarmo", "Tempo Armamento", "Tempo Disarmo", "Usa PIN armamento", "Usa PIN disarmo" };
    int numItems = sizeof(menuItems) / sizeof(menuItems[0]);
    int maxRows = _hardware->getLcdRows() - 1;
    int startIdx = 0;
    if (_menuIndex >= maxRows) { startIdx = _menuIndex - maxRows + 1; }
    for (int i = 0; i < maxRows; i++) {
        int currentItem = startIdx + i;
        if (currentItem < numItems) {
            String prefix = (currentItem == _menuIndex) ? "> " : "  ";
            _hardware->printLcd(0, i + 1, prefix + menuItems[currentItem]);
        }
    }
    _hardware->printLcd(19, 0, " "); _hardware->printLcd(19, 3, " ");
    if (startIdx > 0) _hardware->printLcd(19, 0, "^");
    if (startIdx + maxRows < numItems) _hardware->printLcd(19, 3, "v");

    _hardware->printOled1("INDIETRO", 2, 10, 25);
    _hardware->printOled2("CONFERMA", 2, 18, 25);
}
void SearchDestroyMode::displayEditScreen(const String& title, const String& currentValue, const String& unit) {
    _hardware->clearLcd(); _hardware->printLcd(0, 0, title);
    _hardware->printLcd(0, 1, "Attuale: " + currentValue + unit);
    _hardware->printLcd(0, 2, "Nuovo: " + _currentInputBuffer + "_");
    _hardware->printLcd(0, 3, "P1:Annulla | P2:OK"); _hardware->setStripColor(0, 0, 255);

    _hardware->printOled1("ANNULLA", 2, 22, 25);
    _hardware->printOled2("CONFERMA", 2, 18, 25);
}
void SearchDestroyMode::displayBooleanEditScreen(const String& title, bool currentSelection) {
    _hardware->clearLcd(); _hardware->printLcd(0, 0, title);
    _hardware->printLcd(0, 1, (currentSelection ? "> Si" : "  Si"));
    _hardware->printLcd(0, 2, (!currentSelection ? "> No" : "  No"));
    _hardware->printLcd(0, 3, "P1:Annulla | P2:OK"); _hardware->setStripColor(0, 0, 255);

    _hardware->printOled1("ANNULLA", 2, 22, 25);
    _hardware->printOled2("CONFERMA", 2, 18, 25);
}
void SearchDestroyMode::displayConfirmScreen() {
    _hardware->clearLcd(); _hardware->printLcd(0, 1, "INIZIARE LA PARTITA?");
    _hardware->printLcd(0, 3, "P1:Annulla | P2:OK");

    _hardware->printOled1("ANNULLA", 2, 22, 25);
    _hardware->printOled2("INIZIA", 2, 28, 25);
}
void SearchDestroyMode::displayAwaitArmScreen() {
    _hardware->clearLcd(); _hardware->printLcd(2, 0, "PIANTA LA BOMBA");
    _hardware->printLcd(0, 2, "Tieni premuto ROSSO");
    String secondsText = "per " + String(_settings->getArmingTime()) + " secondi";
    _hardware->printLcd(0, 3, secondsText);
    
    _hardware->printOled1("INNESCA", 2, 22, 25);
    _hardware->clearOled2();
}
void SearchDestroyMode::displayArmingScreen(unsigned long progress) {
    unsigned long totalDuration = _settings->getArmingTime() * 1000;
    if (totalDuration == 0) totalDuration = 1;
    int ledsToShow = map(progress, 0, totalDuration, 0, _hardware->getStripLedCount());
    for(int i=0; i < _hardware->getStripLedCount(); i++){ _hardware->setPixelColor(i, (i < ledsToShow) ? 255 : 0, 0, 0); }
    _hardware->showStrip(); 
    int barWidthChars = 16;
    int totalPixels = barWidthChars * 5;
    int progressPixels = map(progress, 0, totalDuration, 0, totalPixels);
    int fullChars = progressPixels / 5;
    int partialPixels = progressPixels % 5;
    _hardware->printLcd(2, 2, "");
    for (int i = 0; i < fullChars; i++) _hardware->writeCustomChar(4);
    if (partialPixels > 0 && fullChars < barWidthChars) _hardware->writeCustomChar(partialPixels - 1);
    for (int i = fullChars + (partialPixels > 0 ? 1 : 0); i < barWidthChars; i++) _hardware->printLcd(i + 2, 2, " ");
}
void SearchDestroyMode::displayEnterPinScreen(const String& title) {
    _hardware->clearLcd();
    _hardware->printLcd(0, 0, title);
    int lcdWidth = 20;
    int pinLength = _currentInputBuffer.length();
    int startCol = (lcdWidth - pinLength) / 2;
    if (startCol < 0) startCol = 0;
    _hardware->printLcd(startCol, 2, _currentInputBuffer);

    _hardware->printOled1("ANNULLA", 2, 22, 25);
    _hardware->clearOled2();
}
void SearchDestroyMode::displayCountdownLayout() {
    _hardware->clearLcd();
    _hardware->printLcd(2, 0, "BOMBA INNESCATA!");

    _hardware->clearOled1();
    _hardware->printOled2("DISINNESCA", 2, 4, 30);
}
void SearchDestroyMode::updateCountdownDisplay(long remainingSeconds) {
    int minutes = remainingSeconds / 60;
    int seconds = remainingSeconds % 60;
    char timeBuffer[10];
    sprintf(timeBuffer, "%02d : %02d", minutes, seconds);
    _hardware->printLcd(6, 2, timeBuffer);
}

/**
 * @brief Funzione helper chiamata quando si deve aggiornare una schermata di modifica.
 * @details Controlla lo stato attuale e chiama la funzione di `display` corretta
 * con i parametri giusti (titolo, valore attuale, unità di misura).
 */
void SearchDestroyMode::updateDisplayForCurrentState() {
    switch (_currentState) {
        case ModeState::EDIT_BOMB_TIME: displayEditScreen("Mod. Timer Bomba", String(_settings->getBombTime()), "min"); break;
        case ModeState::EDIT_GAME_DURATION: displayEditScreen("Mod. Durata Partita", String(_settings->getGameDuration()), "min"); break;
        case ModeState::EDIT_ARM_PIN: displayEditScreen("Mod. PIN Armamento", _settings->getArmingPin(), ""); break;
        case ModeState::EDIT_DISARM_PIN: displayEditScreen("Mod. PIN Disarmo", _settings->getDisarmingPin(), ""); break;
        case ModeState::EDIT_ARM_TIME: displayEditScreen("Mod. Tempo Armamento", String(_settings->getArmingTime()), "s"); break;
        case ModeState::EDIT_DEFUSE_TIME: displayEditScreen("Mod. Tempo Disarmo", String(_settings->getDefuseTime()), "s"); break;
        case ModeState::EDIT_USE_ARM_PIN: displayBooleanEditScreen("Usare PIN armamento?", _tempBoolSelection); break;
        case ModeState::EDIT_USE_DISARM_PIN: displayBooleanEditScreen("Usare PIN disinnesco", _tempBoolSelection); break;
        default: break;
    }
}
void SearchDestroyMode::displayDefusingScreen(unsigned long progress) {
    _hardware->printLcd(5, 1, "DISINNESCO      ");
    unsigned long totalDuration = _settings->getDefuseTime() * 1000;
    if (totalDuration == 0) totalDuration = 1;
    
    int greenLeds = map(progress, 0, totalDuration, 0, _hardware->getStripLedCount());
    for(int i=0; i < _hardware->getStripLedCount(); i++){
        if (i < greenLeds) _hardware->setPixelColor(i, 0, 255, 0);
        else _hardware->setPixelColor(i, 255, 0, 0);
    }
    _hardware->showStrip(); 
    
    int barWidthChars = 16;
    int totalPixels = barWidthChars * 5;
    int progressPixels = map(progress, 0, totalDuration, 0, totalPixels);
    int fullChars = progressPixels / 5;
    int partialPixels = progressPixels % 5;

    _hardware->printLcd(2, 2, "");
    for (int i = 0; i < fullChars; i++) _hardware->writeCustomChar(4);
    if (partialPixels > 0 && fullChars < barWidthChars) _hardware->writeCustomChar(partialPixels - 1);
    for (int i = fullChars + (partialPixels > 0 ? 1 : 0); i < barWidthChars; i++) _hardware->printLcd(i + 2, 2, " ");
}

/**
 * @brief Costruisce e invia un messaggio UDP JSON con tutte le impostazioni correnti.
 */
void SearchDestroyMode::sendSettingsStatus() {
    JsonDocument doc;
    doc["bomb_time"] = _settings->getBombTime();
    doc["game_duration"] = _settings->getGameDuration();
    doc["arm_pin"] = _settings->getArmingPin();
    doc["disarm_pin"] = _settings->getDisarmingPin();
    doc["arm_time"] = _settings->getArmingTime();
    doc["defuse_time"] = _settings->getDefuseTime();
    doc["use_arm_pin"] = _settings->getUseArmingPin();
    doc["use_disarm_pin"] = _settings->getUseDisarmingPin();
    
    _network->sendEvent("SETTINGS_UPDATE", doc);
}

/**
 * @brief Termina forzatamente la partita in corso, assegnando la vittoria ai CT.
 * @details Chiamata quando viene ricevuto il comando di rete corrispondente.
 */
void SearchDestroyMode::forceEndGame() {
    Serial.println("!!! COMANDO RICEVUTO: forceEndGame in Cerca & Distruggi !!!");
    
    // Non fare nulla se la partita è GIA' finita
    if (_currentState == ModeState::IN_GAME_DEFUSED || _currentState == ModeState::IN_GAME_ENDED) {
        return;
    }

    JsonDocument doc;
    doc["winner"] = "COUNTER_TERRORISTS";
    doc["reason"] = "FORCED_BY_ADMIN";
    _network->sendEvent("GAME_END", doc);
    
    _endGameStatus = "CT WINS";
    _currentState = ModeState::IN_GAME_DEFUSED; 
    _gameIsActive = false; 
    _bombIsActive = false; 
    _hardware->noTone();

    _hardware->clearLcd();
    _hardware->printLcd(1, 1, "PARTITA TERMINATA"); 
    _hardware->printLcd(0, 2, "Vince " + team2_name + "!");
    
    _hardware->playToneAsync(1500, 80); 

    sendTelemetry();
}

void SearchDestroyMode::sendTelemetry() {
    JsonDocument doc;
    
    // 1. STATO TESTUALE 
    if (_currentState == ModeState::IN_GAME_ENDED || _currentState == ModeState::IN_GAME_DEFUSED) {
        doc["state"] = (_endGameStatus.length() > 0) ? _endGameStatus : "GAME OVER";
    }
    else if (_currentState == ModeState::IN_GAME_WAITING_SERVER) {
        doc["state"] = "AWAITING ARBITER";
    }
    else if (_currentState == ModeState::IN_GAME_IS_ARMING || _currentState == ModeState::IN_GAME_ENTER_ARM_PIN) {
        doc["state"] = "ARMING...";
    }
    else if (_currentState == ModeState::IN_GAME_IS_DEFUSING || _currentState == ModeState::IN_GAME_ENTER_DEFUSE_PIN) {
        doc["state"] = "DEFUSING...";
    }
    else if (_currentState == ModeState::IN_GAME_COUNTDOWN || _currentState == ModeState::IN_GAME_ARMED) {
        doc["state"] = "ARMED";
    }
    else if (!_gameIsActive && !_bombIsActive) {
        doc["state"] = "STANDBY";
    }
    else {
        doc["state"] = "SAFE";
    }

    // 2. Percentuali Barre (MODIFICATO QUI)
    
    // --- GESTIONE BARRA ARMAMENTO ---
    if (_currentState == ModeState::IN_GAME_IS_ARMING) {
        unsigned long total = _settings->getArmingTime() * 1000;
        unsigned long elapsed = millis() - _armingStartTime;
        doc["arm_prog"] = (total > 0) ? (elapsed * 100 / total) : 0;
    } 
    // NUOVO: Se siamo nella fase di inserimento PIN, invia 100% fisso
    else if (_currentState == ModeState::IN_GAME_ENTER_ARM_PIN) {
        doc["arm_prog"] = 100;
    }
    else {
        doc["arm_prog"] = 0;
    }
    
    // --- GESTIONE BARRA DISINNESCO ---
    if (_currentState == ModeState::IN_GAME_IS_DEFUSING) {
        unsigned long total = _settings->getDefuseTime() * 1000;
        unsigned long elapsed = millis() - _defusingStartTime;
        doc["def_prog"] = (total > 0) ? (elapsed * 100 / total) : 0;
    } 
    // NUOVO: Se siamo nella fase di inserimento PIN, invia 100% fisso
    else if (_currentState == ModeState::IN_GAME_ENTER_DEFUSE_PIN) {
        doc["def_prog"] = 100;
    }
    else {
        doc["def_prog"] = 0;
    }
    
    // 3. TIMER BOMBA (INVARIATO)
    if (_bombIsActive) {
        long totalSeconds = _settings->getBombTime() * 60;
        TimeSpan elapsed = _hardware->getRTCTime() - _roundStartTime;
        long remaining = totalSeconds - elapsed.totalseconds();
        doc["bomb_time"] = (remaining > 0) ? remaining : 0;
    } else {
        doc["bomb_time"] = 0;
    }

    // 4. TIMER PARTITA (INVARIATO)
    if (_gameIsActive) { 
        long totalMatch = _settings->getGameDuration() * 60;
        TimeSpan matchElapsed = _hardware->getRTCTime() - _gameMatchStartTime;
        long remainingMatch = totalMatch - matchElapsed.totalseconds();
        doc["game_time"] = (remainingMatch > 0) ? remainingMatch : 0;
    } else {
        doc["game_time"] = 0;
    }

    _network->sendEvent("SD_UPDATE", doc);
}

void SearchDestroyMode::resumeGame(JsonDocument& doc) {
    Serial.println("=== ESECUZIONE DISASTER RECOVERY ===");
    
    // 1. Allineamento Autorità
    _isServerAuthoritative = true;
    _gameIsActive = true;
    
    // 2. Viaggio nel tempo del Timer Globale
    long globalTimeLeft = doc["global_time_left"] | (_settings->getGameDuration() * 60);
    long totalMatchSeconds = _settings->getGameDuration() * 60;
    long elapsedMatch = totalMatchSeconds - globalTimeLeft;
    
    TimeSpan tsElapsedMatch(elapsedMatch);
    _gameMatchStartTime = _hardware->getRTCTime() - tsElapsedMatch;

    // 3. Ripristino dello Stato della Bomba
    String state = doc["sd_state"] | "SAFE";
    _bombIsActive = false;
    _hardware->noTone();
    _endGameStatus = "";

    if (state == "ARMING..." || state == "SAFE") {
        Serial.println("Stato Ripristinato: SAFE / IN ATTESA DI INNESCO");
        _currentState = ModeState::IN_GAME_AWAIT_ARM;
        displayAwaitArmScreen();
    }
    else if (state == "ARMED" || state == "DEFUSING...") {
        Serial.println("Stato Ripristinato: BOMBA ARMATA");
        _currentState = ModeState::IN_GAME_COUNTDOWN;
        _bombIsActive = true;
        
        // Viaggio nel tempo del Timer Bomba
        long bombTimeLeft = doc["bomb_time_left"] | (_settings->getBombTime() * 60);
        long totalBombSeconds = _settings->getBombTime() * 60;
        long elapsedBomb = totalBombSeconds - bombTimeLeft;
        
        TimeSpan tsElapsedBomb(elapsedBomb);
        _roundStartTime = _hardware->getRTCTime() - tsElapsedBomb;
        
        displayCountdownLayout();
    }
    else if (state.indexOf("WINS") != -1 || state == "GAME OVER" || state == "AWAITING ARBITER") {
        // Se la partita era già finita (o se il server stava arbitrando), 
        // ci affidiamo all'arbitro per chiuderla in pace.
        Serial.println("Stato Ripristinato: PARTITA GIA' TERMINATA");
        forceWin("", "RIPRISTINO POST-CRASH");
    }

    sendTelemetry(); // Aggiorna subito il monitor web
}

void SearchDestroyMode::syncTime(long globalTimeLeft) {
    // Sincronizziamo solo se la partita è in corso
    if (!_gameIsActive) return;
    
    long totalMatchSeconds = _settings->getGameDuration() * 60;
    long elapsedMatch = totalMatchSeconds - globalTimeLeft;
    
    // Evita calcoli negativi dovuti a lag
    if (elapsedMatch < 0) elapsedMatch = 0;
    
    TimeSpan tsElapsedMatch(elapsedMatch);
    
    // Corregge retroattivamente il momento di inizio partita per annullare ogni drift
    _gameMatchStartTime = _hardware->getRTCTime() - tsElapsedMatch;
    
    // Opzionale: decommenta questa riga per debug se vuoi vedere l'allineamento sul monitor seriale del RAT
    // Serial.printf("SYNC TIME: Orologio allineato al server. Secondi globali rimanenti: %ld\n", globalTimeLeft);
}