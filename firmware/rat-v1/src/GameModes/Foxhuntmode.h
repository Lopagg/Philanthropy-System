#ifndef FOXHUNT_MODE_H
#define FOXHUNT_MODE_H

#include "GameMode.h"
#include "HardwareManager.h"
#include "NetworkManager.h"
#include "app_common.h"
#include <ArduinoJson.h>

enum class FoxhuntState {
    WAIT_NFC,
    MINIGAME_KEY_1,
    MINIGAME_KEY_2,
    WAIT_PIN,
    WAIT_DUAL_BTN,
    ARMED_COUNTDOWN,
    EXPLODED
};

class FoxhuntMode : public GameMode {
public:
    FoxhuntMode(HardwareManager* hardware, NetworkManager* network, AppState* appState, MainMenuDisplayFunction displayFunc);
    
    void enter() override;
    void loop() override;
    void exit() override;

private:
    HardwareManager* _hardware;
    NetworkManager* _network;
    AppState* _appStatePtr;
    MainMenuDisplayFunction _mainMenuDisplayFunc;
    
    FoxhuntState _currentState;
    
    // --- Configurazione NFC ---
    String _validTag1 = "04:AB:A0:BA:6F:61:80"; // <-- Modifica qui con il tuo vero UID
    String _validTag2 = "04:9A:62:B7:6F:61:80"; // <-- Modifica qui con il tuo vero UID

    // --- Variabili Minigioco Slider ---
    unsigned long _animStartTime;
    bool _prevKey1State;
    bool _prevKey2State;
    
    const unsigned long ANIM_DURATION = 1500;
    const unsigned long WINDOW_DURATION = 1000;
    
    // --- Variabili PIN e Timer ---
    String _targetCode;
    String _enteredCode;
    unsigned long _dualButtonStartTime;
    unsigned long _countdownStartTime;

    void sendTelemetry();
    void runSliderMinigame(int keyNumber, uint8_t r, uint8_t g, uint8_t b);
};

#endif // FOXHUNT_MODE_H