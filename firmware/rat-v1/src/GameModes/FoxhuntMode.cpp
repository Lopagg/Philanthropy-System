#include "Foxhuntmode.h"
#include <Keypad.h>

// Costruttore
FoxhuntMode::FoxhuntMode(HardwareManager* hardware, NetworkManager* network, AppState* appState, MainMenuDisplayFunction displayFunc)
    : _hardware(hardware), 
      _network(network), 
      _appStatePtr(appState), 
      _mainMenuDisplayFunc(displayFunc) {
}

void FoxhuntMode::enter() {
    Serial.println("Entrato in modalita' FOXHUNT");
    
    _currentState = FoxhuntState::WAIT_NFC;
    _enteredCode = "";
    _dualButtonStartTime = 0;
    
    _prevKey1State = _hardware->isKey1Turned(); 
    _prevKey2State = _hardware->isKey2Turned();

    _hardware->clearLcd();
    _hardware->printLcd(0, 0, "--- INFILTRAZIONE --");
    _hardware->printLcd(0, 1, "IDENTITA' RICHIESTA ");
    _hardware->printLcd(0, 2, " > PASSA TESSERA <  ");
    _hardware->setStripColor(0, 0, 255); 
    
    // SETUP OLED INIZIALE
    _hardware->printOled1("ESCI", 2, 35, 25);
    _hardware->clearOled2();
    
    sendTelemetry();
}

void FoxhuntMode::exit() {
    Serial.println("Uscito da FOXHUNT");
    _hardware->turnOffStrip();
    _hardware->clearLcd();
    
    // PULISCE GLI OLED ALL'USCITA DELLA MODALITÀ
    _hardware->clearOled1();
    _hardware->clearOled2();
}

void FoxhuntMode::loop() {
    bool btn1_pressed = _hardware->wasButton1Pressed();
    bool btn2_pressed = _hardware->wasButton2Pressed();
    bool is_btn1_held = _hardware->isButton1Pressed();
    bool is_btn2_held = _hardware->isButton2Pressed();

    // Ora che il loop non è più bloccato dall'NFC, l'uscita sarà istantanea
    if ((btn1_pressed || is_btn1_held) && _currentState == FoxhuntState::WAIT_NFC) {
        _hardware->playTone(300, 70);
        exit();
        *_appStatePtr = APP_STATE_MAIN_MENU;
        _mainMenuDisplayFunc();
        return;
    }

    switch (_currentState) {
        
        case FoxhuntState::WAIT_NFC: {
            // --- 1. EFFETTO SONAR (Suono) ---
            static unsigned long lastPing = 0;
            if (millis() - lastPing > 3000) {
                lastPing = millis();
                _hardware->playTone(2000, 50); 
            }

            // --- 2. EFFETTO SCANNER RADAR "COMETA" (LED) ---
            int totalLeds = _hardware->getStripLedCount();
            if (totalLeds > 0) {
                int speed = 60; 
                int maxPos = totalLeds - 1;
                int cycle = (millis() / speed) % (maxPos * 2);
                int pos = cycle;
                
                if (cycle > maxPos) {
                    pos = (maxPos * 2) - cycle; 
                }

                for(int i = 0; i < totalLeds; i++) {
                    int distance = abs(i - pos);
                    if (distance == 0) {
                        _hardware->setPixelColor(i, 0, 255, 255); 
                    } else if (distance == 1) {
                        _hardware->setPixelColor(i, 0, 100, 100); 
                    } else if (distance == 2) {
                        _hardware->setPixelColor(i, 0, 20, 20);   
                    } else {
                        _hardware->setPixelColor(i, 0, 0, 0);     
                    }
                }
                _hardware->showStrip();
            }

            // --- 3. LETTURA NFC (Non bloccante) ---
            static unsigned long lastNfcScan = 0;
            if (millis() - lastNfcScan > 150) {
                lastNfcScan = millis();
                
                String uid = _hardware->readRFID(30); 
                
                if (uid != "Nessuna card trovata" && uid != "") {
                    if (uid == _validTag1 || uid == _validTag2) {
                        
                        _hardware->clearOled1();
                        _hardware->clearOled2();
                        
                        // FEEDBACK VISIVO: STRISCIA VERDE
                        for(int i = 0; i < totalLeds; i++) _hardware->setPixelColor(i, 0, 255, 0);
                        _hardware->showStrip();
                        
                        _hardware->playTone(1500, 150);
                        _hardware->clearLcd();
                        _hardware->printLcd(0, 0, " ACCESSO CONSENTITO ");
                        _hardware->printLcd(0, 2, "    PREPARARSI A    ");
                        _hardware->printLcd(0, 3, "SBLOCCO CHIAVE 1... ");
                        
                        delay(2000); 
                        
                        _currentState = FoxhuntState::MINIGAME_KEY_1;
                        _animStartTime = millis(); 
                        _prevKey1State = _hardware->isKey1Turned(); 
                        sendTelemetry();
                    } else {
                        // FEEDBACK VISIVO: STRISCIA ROSSA
                        for(int i = 0; i < totalLeds; i++) _hardware->setPixelColor(i, 255, 0, 0);
                        _hardware->showStrip();
                        
                        _hardware->printLcd(0, 3, "TESSERA NON VALIDA! ");
                        _hardware->playTone(200, 500); 
                        delay(1500);
                        _hardware->printLcd(0, 2, " > PASSA TESSERA <  ");
                        _hardware->printLcd(0, 3, "                    "); 
                    }
                }
            }
            break;
        }

        case FoxhuntState::MINIGAME_KEY_1: {
            runSliderMinigame(1, 255, 0, 0);
            break;
        }
        
        case FoxhuntState::MINIGAME_KEY_2: {
            runSliderMinigame(2, 0, 255, 0);
            break;
        }

        case FoxhuntState::WAIT_PIN: {
            // --- ANIMAZIONE RESPIRO AZZURRO ---
            // Un'onda sinusoidale morbida per indicare che il sistema è sbloccato e in attesa
            float breath = (exp(sin(millis() / 2000.0 * PI)) - 0.36787944) * 108.0;
            int intensity = constrain(breath, 0, 255);
            int totalLeds = _hardware->getStripLedCount();
            for(int i = 0; i < totalLeds; i++) {
                _hardware->setPixelColor(i, 0, intensity / 2, intensity); // Azzurro soffuso
            }
            _hardware->showStrip();
            // -----------------------------------

            char key = _hardware->getKey();
            if (key != NO_KEY) {
                _hardware->playTone(700, 30); 
                
                if (key == '*' || key == '#') {
                    _enteredCode = ""; 
                } else {
                    _enteredCode += key;
                }
                
                String displayStr = "INPUT: " + _enteredCode;
                while (displayStr.length() < 20) { displayStr += " "; }
                _hardware->printLcd(0, 3, displayStr);

                if (_enteredCode.length() == 4) {
                    if (_enteredCode == _targetCode) {
                        
                        _hardware->printOled1("INNESCA", 2, 28, 25);
                        _hardware->printOled2("INNESCA", 2, 28, 25);
                        
                        _hardware->playTone(1000, 80); 
                        _hardware->playTone(1200, 80); 
                        _hardware->playTone(1500, 100);
                        
                        _hardware->clearLcd();
                        _hardware->printLcd(0, 0, "OVERRIDE ACCETTATO. ");
                        _hardware->printLcd(0, 2, "  PREMI ENTRAMBI I  ");
                        _hardware->printLcd(0, 3, "TASTI PER 5 SECONDI ");
                        _hardware->setStripColor(255, 100, 0); 
                        _currentState = FoxhuntState::WAIT_DUAL_BTN;
                        sendTelemetry();
                    } else {
                        _hardware->playTone(200, 500); 
                        _hardware->printLcd(0, 3, "ERRATO. RIPROVA.    ");
                        
                        // Lampeggio rosso per l'errore PIN
                        for(int i=0; i<totalLeds; i++) _hardware->setPixelColor(i, 255, 0, 0);
                        _hardware->showStrip();
                        
                        delay(1500);
                        _enteredCode = "";
                        _hardware->printLcd(0, 3, "INPUT:              ");
                    }
                }
            }
            break;
        }

        case FoxhuntState::WAIT_DUAL_BTN: {
            if (is_btn1_held && is_btn2_held) {
                if (_dualButtonStartTime == 0) {
                    _dualButtonStartTime = millis(); 
                }
                
                unsigned long elapsed = millis() - _dualButtonStartTime;
                int remaining = 5 - (elapsed / 1000);
                
                char buf[21];
                sprintf(buf, "INNESCANDO: %d sec   ", remaining);
                _hardware->printLcd(0, 3, String(buf));
                
                if ((millis() / 200) % 2 == 0) _hardware->setStripColor(255, 100, 0);
                else _hardware->turnOffStrip();
                
                // --- EFFETTO SONORO "CARICAMENTO ENERGIA" ---
                // La frequenza parte da un cupo 200 Hz e sale fino a un acuto 2500 Hz 
                // in proporzione esatta ai 5000 millisecondi di pressione.
                int currentFreq = map(elapsed, 0, 5000, 200, 2500);
                _hardware->updateTone(currentFreq);
                // --------------------------------------------
                
                if (elapsed >= 5000) { 
                    
                    _hardware->noTone(); // Ferma il suono di caricamento
                    
                    _hardware->clearOled1();
                    _hardware->clearOled2();
                    
                    // Un beep acuto e forte per confermare l'avvenuto innesco
                    _hardware->playTone(3000, 200);
                    
                    _currentState = FoxhuntState::ARMED_COUNTDOWN;
                    _countdownStartTime = millis();
                    _hardware->clearLcd();
                    sendTelemetry();
                }
            } else {
                if (_dualButtonStartTime != 0) {
                    _dualButtonStartTime = 0; 
                    
                    _hardware->noTone(); // Spegne immediatamente il suono se un tasto viene rilasciato
                    
                    _hardware->printLcd(0, 3, "SEQUENZA INTERROTTA ");
                    _hardware->playTone(200, 500); // Suono di disattivazione/errore
                    _hardware->setStripColor(255, 100, 0); 
                    delay(1500);
                    _hardware->printLcd(0, 3, "TASTI PER 5 SECONDI ");
                }
            }
            break;
        }

        case FoxhuntState::ARMED_COUNTDOWN: {
            unsigned long elapsed = millis() - _countdownStartTime;
            int timeLeft = 5 - (elapsed / 1000);
            
            char buf[21];
            sprintf(buf, "DETONAZIONE IN: %-2d  ", timeLeft);
            _hardware->printLcd(0, 1, " !!! INNESCATA !!!  ");
            _hardware->printLcd(0, 2, String(buf));
            
            if ((millis() / 100) % 2 == 0) {
                _hardware->setStripColor(255, 0, 0);
            } else {
                _hardware->turnOffStrip();
            }
            
            if (elapsed % 1000 < 100) {
                _hardware->updateTone(1500);
            } else {
                _hardware->noTone();
            }

            if (elapsed >= 5000) {
                for(int i=0; i<3; i++) {
                    _hardware->setBrightness(255);
                    _hardware->setStripColor(255, 255, 255); _hardware->playTone(2000, 50);
                    _hardware->setStripColor(255, 100, 0); _hardware->playTone(1000, 80);
                    _hardware->setStripColor(255, 0, 0); _hardware->playTone(400, 100);
                }
                _hardware->playTone(150, 3000); 
                
                _currentState = FoxhuntState::EXPLODED;
                
                _hardware->printOled1("ESCI", 2, 35, 25);
                
                _hardware->clearLcd();
                _hardware->printLcd(0, 1, "    *** BOOM *** ");
                _hardware->printLcd(0, 2, "INFILTRATI VINCITORI");
                _hardware->setStripColor(255, 0, 0); 
                sendTelemetry();
            }
            break;
        }

        case FoxhuntState::EXPLODED: {
            if (btn1_pressed || is_btn1_held) {
                _hardware->playTone(300, 70);
                exit();
                *_appStatePtr = APP_STATE_MAIN_MENU;
                _mainMenuDisplayFunc();
            }
            break;
        }
    }
}

void FoxhuntMode::runSliderMinigame(int keyNumber, uint8_t r, uint8_t g, uint8_t b) {
    unsigned long elapsed = millis() - _animStartTime;
    
    const unsigned long WINDOW_DURATION_FAST = 500; 
    unsigned long cycleTime = ANIM_DURATION + WINDOW_DURATION_FAST;
    
    bool currentKey = (keyNumber == 1) ? _hardware->isKey1Turned() : _hardware->isKey2Turned();
    bool prevKey = (keyNumber == 1) ? _prevKey1State : _prevKey2State;
    
    bool justTurned = (currentKey == true && prevKey == false);
    
    if (keyNumber == 1) _prevKey1State = currentKey;
    else _prevKey2State = currentKey;

    int totalLeds = _hardware->getStripLedCount();
    
    // --- CALCOLO SIMMETRICO DEL BERSAGLIO ---
    bool isEven = (totalLeds % 2 == 0);
    int centerLeft = (totalLeds / 2) - (isEven ? 1 : 0);
    int centerRight = totalLeds / 2;
    // ----------------------------------------
    
    for(int i = 0; i < totalLeds; i++) {
        _hardware->setPixelColor(i, 0, 0, 0);
    }
    
    // Disegna il bersaglio centrale (1 LED se dispari, 2 LED se pari)
    _hardware->setPixelColor(centerLeft, 0, 0, 255);
    _hardware->setPixelColor(centerRight, 0, 0, 255); 

    if (elapsed < ANIM_DURATION) {
        _hardware->printLcd(0, 1, (keyNumber == 1) ? "SBLOCCO CHIAVE 1... " : "SBLOCCO CHIAVE 2... ");
        _hardware->printLcd(0, 2, "ATTENDI IL SEGNALE! ");
        
        float progress = (float)elapsed / (float)ANIM_DURATION;
        
        // Calcoliamo i led da accendere partendo dai bordi verso il centro
        int ledsToLight = progress * (centerLeft + 1); 
        
        for (int i = 0; i < ledsToLight; i++) {
            _hardware->setPixelColor(i, r, g, b); // Onda da sinistra
            _hardware->setPixelColor((totalLeds - 1) - i, r, g, b); // Onda da destra
        }
        
        _hardware->showStrip(); 
        
        // --- SUONO DI CARICAMENTO TENSIONE ---
        int tickInterval = map(elapsed, 0, ANIM_DURATION, 200, 30);
        static unsigned long lastTick = 0;
        if (millis() - lastTick > tickInterval) {
            lastTick = millis();
            _hardware->playTone(1000, 10); 
        }
        // -------------------------------------
        
        if (justTurned) {
            _hardware->playTone(200, 500); 
            _hardware->printLcd(0, 2, "TROPPO PRESTO!      ");
            delay(1000); 
            _animStartTime = millis(); 
            
            if (keyNumber == 1) _prevKey1State = true; 
            else _prevKey2State = true;
        }
        
    } else if (elapsed >= ANIM_DURATION && elapsed <= cycleTime) {
        _hardware->printLcd(0, 2, " >>> GIRA ORA! <<<  ");
        
        for (int i = 0; i < totalLeds; i++) _hardware->setPixelColor(i, r, g, b);
        
        _hardware->showStrip();
        
        // --- SUONO DEL SEGNALE DI VIA ---
        if (elapsed - ANIM_DURATION < 100) {
             _hardware->playTone(2000, 50);
        }
        // --------------------------------
        
        if (justTurned) {
            _hardware->playTone(1200, 100); 
            _hardware->clearLcd();
            
            if (keyNumber == 1) {
                _hardware->printLcd(0, 0, " CHIAVE 1 ACCETTATA ");
                delay(1500);
                _currentState = FoxhuntState::MINIGAME_KEY_2;
                _animStartTime = millis();
                _prevKey2State = _hardware->isKey2Turned();
                sendTelemetry();
            } else {
                _hardware->printLcd(0, 0, " CHIAVE 2 ACCETTATA ");
                delay(1500);
                
                randomSeed(millis());
                _targetCode = String(random(1000, 10000));
                
                _currentState = FoxhuntState::WAIT_PIN;
                _hardware->clearLcd();
                _hardware->printLcd(0, 0, " BYPASS CHIAVI OK.  ");
                _hardware->printLcd(0, 1, "  CODICE SBLOCCO:   ");
                
                String targetStr = "> " + _targetCode + " <";
                int padding = (20 - targetStr.length()) / 2;
                String padStr = "";
                for(int i=0; i<padding; i++) padStr += " ";
                _hardware->printLcd(0, 2, padStr + targetStr + padStr);
                
                _hardware->printLcd(0, 3, "INPUT:              ");
                
                // Lasciamo la striscia spenta per un momento prima che inizi il "respiro"
                _hardware->turnOffStrip(); 
                sendTelemetry();
            }
        }
        
    } else {
        _hardware->playTone(200, 500); 
        _hardware->printLcd(0, 2, "TEMPO SCADUTO!      ");
        _hardware->turnOffStrip();
        delay(1000); 
        _animStartTime = millis(); 
    }
}

void FoxhuntMode::sendTelemetry() {
    JsonDocument doc;
    doc["mode"] = "FOXHUNT";
    
    switch(_currentState) {
        case FoxhuntState::WAIT_NFC: doc["state"] = "ATTESA TESSERA"; break;
        case FoxhuntState::MINIGAME_KEY_1: doc["state"] = "HACKING CHIAVE 1"; break;
        case FoxhuntState::MINIGAME_KEY_2: doc["state"] = "HACKING CHIAVE 2"; break;
        case FoxhuntState::WAIT_PIN: doc["state"] = "INSERIMENTO PIN"; break;
        case FoxhuntState::WAIT_DUAL_BTN: doc["state"] = "ATTESA INNESCO (5S)"; break;
        case FoxhuntState::ARMED_COUNTDOWN: doc["state"] = "DETONAZIONE IN CORSO"; break;
        case FoxhuntState::EXPLODED: doc["state"] = "BOMBA ESPLOSA"; break;
        default: doc["state"] = "INIT"; break;
    }
    
    _network->sendEvent("FH_UPDATE", doc);
}