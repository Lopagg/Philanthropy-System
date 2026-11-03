// src/GameModes/DominationMode.h

#ifndef DOMINATION_MODE_H
#define DOMINATION_MODE_H

#include "GameMode.h"
#include "HardwareManager.h"
#include "NetworkManager.h"
#include "DominationSettings.h"
#include "app_common.h"

class DominationMode : public GameMode {
public:
    DominationMode(HardwareManager* hardware, NetworkManager* network, DominationSettings* settings, AppState* appState, MainMenuDisplayFunction displayFunc);

    void enter() override;
    void loop() override;
    void exit() override;
    void sendSettingsStatus();
    void enterInGame();
    void forceEndGame();
    void forceWin(String winnerTeam); // <-- NUOVO METODO AGGIUNTO
    void sendTelemetry();

private:
    HardwareManager* _hardware;
    NetworkManager* _network;
    DominationSettings* _settings;
    
    AppState* _appStatePtr;
    MainMenuDisplayFunction _mainMenuDisplayFunc;

    enum class ModeState {
        MODE_SUB_MENU,
        MENU_SETTINGS,
        EDIT_DURATION,
        EDIT_CAPTURE_TIME,
        EDIT_COUNTDOWN,
        IN_GAME_CONFIRM,
        IN_GAME_COUNTDOWN,
        IN_GAME_NEUTRAL,
        CAPTURING_TEAM1,
        CAPTURING_TEAM2,
        TEAM1_CAPTURED,
        TEAM2_CAPTURED,
        GAME_OVER
    };
    ModeState _currentState;
    ModeState _lastZoneState;

    int _subMenuIndex;
    int _menuIndex;
    String _currentInputBuffer;

    unsigned long _countdownStartTime;
    int _lastCountdownSecond;
    DateTime _gameStartTime;
    long _lastGameSecond;
    unsigned long _captureStartTime;
    unsigned long _captureSoundLastUpdate;

    unsigned long _team1PossessionTime;
    unsigned long _team2PossessionTime;
    unsigned long _lastPossessionUpdateTime;
    int _winner; // 0 = Pareggio, 1 = Squadra 1, 2 = Squadra 2

    String _endGameStatus;
    unsigned long _lastTelemetryTime;

    // Funzioni
    void displaySubMenu();
    void handleSubMenuInput(char key, bool btn1, bool btn2);
    void displaySettingsMenu();
    void handleSettingsInput(char key, bool btn1, bool btn2);
    void displayEditScreen(const String& title, const String& currentValue, const String& unit);
    void handleEditInput(char key, bool btn1, bool btn2);
    void updateDisplayForCurrentState();
    void displayConfirmScreen();
    void handleConfirmInput(bool btn1, bool btn2);
    void handleCountdown();
    void updateGameTimerOnRow(int row);
    void handleNeutralState(bool btn1_is_pressed, bool btn2_is_pressed);
    void displayCapturingScreen(int team);
    void handleCapturingState(bool btn1_is_pressed, bool btn2_is_pressed);
    void handleCapturedState(int team, bool btn1_is_pressed, bool btn2_is_pressed);
    void handleGameOverState(bool btn1_was_pressed, bool btn2_was_pressed, char key);
};

#endif // DOMINATION_MODE_H