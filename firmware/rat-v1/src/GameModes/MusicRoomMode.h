// src/GameModes/MusicRoomMode.h

#ifndef MUSIC_ROOM_MODE_H
#define MUSIC_ROOM_MODE_H

#include "GameMode.h"
#include "HardwareManager.h"
#include "app_common.h"
#include "melodies.h" // Includiamo le melodie

// Struttura per rappresentare una melodia nel menu
struct Tune {
    const char* name;
    const int (*melody)[3];
    int length;
};

class MusicRoomMode : public GameMode {
public:
    MusicRoomMode(HardwareManager* hardware, AppState* appState, MainMenuDisplayFunction displayFunc);

    void enter() override;
    void loop() override;
    void exit() override;

private:
    HardwareManager* _hardware;
    AppState* _appStatePtr;
    MainMenuDisplayFunction _mainMenuDisplayFunc;

    int _menuIndex;
    int _currentlyPlayingIndex; // -1 se nessuna melodia è in riproduzione

    void displayMenu();
    void handleInput(char key, bool btn1, bool btn2);
};

#endif // MUSIC_ROOM_MODE_H