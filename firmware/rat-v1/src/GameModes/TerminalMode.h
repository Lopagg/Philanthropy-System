// src/GameModes/TerminalMode.h

#ifndef TERMINAL_MODE_H
#define TERMINAL_MODE_H

#include "GameMode.h"
#include "HardwareManager.h"
#include "NetworkManager.h"
#include "app_common.h"
#include <ArduinoJson.h>

class TerminalMode : public GameMode {
public:
    TerminalMode(HardwareManager* hardware, NetworkManager* network, AppState* appState, MainMenuDisplayFunction displayFunc);

    void enter() override;
    void loop() override;
    void exit() override;

    // Spostato in public per renderlo accessibile da main.cpp
    void parseCommand(JsonDocument& doc);

private:
    HardwareManager* _hardware;
    NetworkManager* _network;
    AppState* _appStatePtr;
    MainMenuDisplayFunction _mainMenuDisplayFunc;

    unsigned long _lastInputTime;

    void handleInput();
};

#endif // TERMINAL_MODE_H