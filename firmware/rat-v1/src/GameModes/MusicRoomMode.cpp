#include "GameModes/MusicRoomMode.h"

// Lista delle melodie disponibili
const Tune tunes[] = {
    {"Erika", ERIKA, ERIKA_LENGTH},
    {"Faccina", FACCINA, FACCINA_LENGTH}
};
const int numTunes = sizeof(tunes) / sizeof(tunes[0]);

// Costruttore
MusicRoomMode::MusicRoomMode(HardwareManager* hardware, AppState* appState, MainMenuDisplayFunction displayFunc)
    : _hardware(hardware),
      _appStatePtr(appState),
      _mainMenuDisplayFunc(displayFunc),
      _menuIndex(0),
      _currentlyPlayingIndex(-1) {
}

void MusicRoomMode::enter() {
    Serial.println("Entrato in Stanza dei Suoni");
    _menuIndex = 0;
    _currentlyPlayingIndex = -1;
    _hardware->noTone();
    displayMenu();
    _hardware->setStripColor(255, 0, 255); // Colore magenta statico all'ingresso
}

void MusicRoomMode::loop() {
    char key = _hardware->getKey();
    bool btn1_was_pressed = _hardware->wasButton1Pressed();
    bool btn2_was_pressed = _hardware->wasButton2Pressed();
    
    // Controlla se la melodia è finita
    if (_currentlyPlayingIndex != -1 && !_hardware->isMidiTunePlaying()) {
        _currentlyPlayingIndex = -1;
        displayMenu();
        _hardware->setStripColor(255, 0, 255); // Ripristina il colore statico a fine melodia
    }

    // Gestisce l'animazione dei LED
    if (_hardware->isMidiTunePlaying()) {
        _hardware->updateWinnerWaveEffect(255, 0, 255, 0.1, 1.0, 10);
    }

    handleInput(key, btn1_was_pressed, btn2_was_pressed);
}

void MusicRoomMode::exit() {
    Serial.println("Uscito da Stanza dei Suoni");
    _hardware->stopMidiTune();
    _hardware->turnOffStrip();
    _hardware->clearOled1();
    _hardware->clearOled2();
}

void MusicRoomMode::displayMenu() {
    _hardware->clearLcd();
    _hardware->printLcd(0, 0, "LA STANZA DEI SUONI");
    
    int maxRows = _hardware->getLcdRows() - 1;
    int startIdx = 0;
    if (_menuIndex >= maxRows) {
        startIdx = _menuIndex - maxRows + 1;
    }

    for (int i = 0; i < maxRows; i++) {
        int currentItemIndex = startIdx + i;
        if (currentItemIndex < numTunes) {
            String prefix = (currentItemIndex == _menuIndex) ? "> " : "  ";
            String line = prefix + tunes[currentItemIndex].name;

            // *** MODIFICA: Costruisce l'intera stringa di 20 caratteri prima di stamparla ***
            if (currentItemIndex == _currentlyPlayingIndex) {
                while(line.length() < 19) { 
                    line += " "; 
                }
                line += ">"; 
            }
            _hardware->printLcd(0, i + 1, line);
        }
    }

    _hardware->printLcd(19, 1, " "); 
    _hardware->printLcd(19, 3, " ");
    if (startIdx > 0) {
        _hardware->printLcd(19, 1, "^");
    }
    if (startIdx + maxRows < numTunes) {
        _hardware->printLcd(19, 3, "v");
    }
    
    _hardware->printOled1("INDIETRO", 2, 10, 25);
    if (_menuIndex == _currentlyPlayingIndex) {
        _hardware->printOled2("FERMA", 2, 35, 25);
    } else {
        _hardware->printOled2("SUONA", 2, 35, 25);
    }
}

void MusicRoomMode::handleInput(char key, bool btn1, bool btn2) {
    if (key == '2') {
        _hardware->playTone(800, 50);
        _menuIndex = (_menuIndex - 1 + numTunes) % numTunes;
        displayMenu();
    } else if (key == '8') {
        _hardware->playTone(600, 50);
        _menuIndex = (_menuIndex + 1) % numTunes;
        displayMenu();
    }

    if (btn1) {
        _hardware->playTone(300, 70);
        exit(); 
        *_appStatePtr = APP_STATE_MAIN_MENU;
        _mainMenuDisplayFunc();
        return;
    }

    if (btn2) {
        if (_menuIndex == _currentlyPlayingIndex) {
            _hardware->stopMidiTune();
            _currentlyPlayingIndex = -1;
            _hardware->playTone(500, 100);
            _hardware->setStripColor(255, 0, 255); // Ripristina il colore statico
        } else {
            const Tune& selectedTune = tunes[_menuIndex];
            _hardware->playMidiTune(selectedTune.melody, selectedTune.length);
            _currentlyPlayingIndex = _menuIndex;
        }
        displayMenu();
    }
}