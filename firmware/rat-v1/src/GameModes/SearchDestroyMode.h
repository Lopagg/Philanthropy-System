// src/GameModes/SearchDestroyMode.h

/**
 * @file SearchDestroyMode.h
 * @brief Dichiarazione della classe per la modalità di gioco "Cerca e Distruggi".
 */

#ifndef SEARCH_DESTROY_MODE_H
#define SEARCH_DESTROY_MODE_H

#include "GameMode.h"
#include "HardwareManager.h"
#include "NetworkManager.h"
#include "SearchDestroySettings.h"
#include "app_common.h"

/**
 * @class SearchDestroyMode
 * @brief Implementa tutta la logica per la modalità "Cerca e Distruggi".
 * @details Questa classe gestisce i sottomenu, le impostazioni specifiche,
 * l'innesco della bomba, il disinnesco e il timer di gioco.
 */
class SearchDestroyMode : public GameMode {
public:

    /**
     * @brief Costruttore della modalità di gioco.
     * @param hardware Puntatore all'oggetto HardwareManager.
     * @param network Puntatore all'oggetto NetworkManager.
     * @param settings Puntatore all'oggetto con le impostazioni specifiche di questa modalità.
     * @param appState Puntatore allo stato globale dell'applicazione.
     * @param displayFunc Puntatore alla funzione per ridisegnare il menu principale.
     */
    SearchDestroyMode(HardwareManager* hardware, NetworkManager* network, SearchDestroySettings* settings, AppState* appState, MainMenuDisplayFunction displayFunc);

    void enter() override;
    void loop() override;
    void exit() override;

    // Funzione di utilità per inviare le impostazioni via rete
    void sendSettingsStatus();
    void enterInGame();
    void forceEndGame();
    void forceWin(String winnerTeam, String reason = "");
    void resumeGame(JsonDocument& doc);

    void syncTime(long globalTimeLeft);

    void sendTelemetry();

private:
    // Puntatori agli oggetti principali
    HardwareManager* _hardware;
    NetworkManager* _network;
    SearchDestroySettings* _settings;
    
    AppState* _appStatePtr;
    MainMenuDisplayFunction _mainMenuDisplayFunc;

    DateTime _gameMatchStartTime; // Momento in cui è iniziata la partita globale

    /**
     * @enum ModeState
     * @brief Definisce tutti i possibili stati interni della modalità di gioco.
     */
    enum class ModeState {
        // Stati dei Menu

        MODE_SUB_MENU,          // Sottomenu principale (Inizia Partita / Impostazioni)
        MENU_SETTINGS,          // Menu delle impostazioni
        EDIT_BOMB_TIME,         // Schermata di modifica del timer della bomba
        EDIT_GAME_DURATION,     // Schermata di modifica della durata della partita
        EDIT_ARM_PIN,           // Schermata di modifica del PIN di innesco
        EDIT_DISARM_PIN,        // Schermata di modifica del PIN di disinnesco
        EDIT_ARM_TIME,          // Schermata di modifica del tempo di innesco
        EDIT_DEFUSE_TIME,       // Schermata di modifica del tempo di disinnesco
        EDIT_USE_ARM_PIN,       // Schermata di scelta per l'uso del PIN di innesco
        EDIT_USE_DISARM_PIN,    // Schermata di scelta per l'uso del PIN di disinnesco
        
        // Stati di Gioco

        IN_GAME_CONFIRM,            // Schermata di conferma "Iniziare la partita?"
        IN_GAME_AWAIT_ARM,          // La partita è attiva, in attesa che un giocatore inneschi la bomba
        IN_GAME_IS_ARMING,          // Un giocatore sta tenendo premuto il pulsante per innescare
        IN_GAME_ENTER_ARM_PIN,      // Schermata di inserimento del PIN di innesco
        IN_GAME_ARMED,              // Stato transitorio che conferma l'innesco e avvia il timer
        IN_GAME_COUNTDOWN,          // La bomba è innescata e il timer sta scorrendo
        IN_GAME_IS_DEFUSING,        // Un giocatore sta tenendo premuto il pulsante per disinnescare
        IN_GAME_ENTER_DEFUSE_PIN,   // Schermata di inserimento del PIN di disinnesco
        IN_GAME_DEFUSED,            // La partita è finita perché la bomba è stata disinnescata
        IN_GAME_ENDED,              // La partita è finita perché il timer della bomba è scaduto
        IN_GAME_WAITING_SERVER      // La valigetta aspetta il verdetto del server
    };
    ModeState _currentState;

    // Variabili di stato per i menu e l'input
    String _currentInputBuffer; // Memorizza l'input dal tastierino
    int _menuIndex;             // Indice per il menu delle impostazioni
    int _subMenuIndex;          // Indice per il sottomenu principale
    bool _tempBoolSelection;    // Memorizza temporaneamente la scelta Sì/No
    
    // Variabili per i timer di gioco
    unsigned long _armingStartTime;
    unsigned long _armingSoundLastUpdate;
    unsigned long _defusingStartTime;
    
    DateTime _roundStartTime;
    unsigned long _stateChangeTime;
    int _lastDisplayedSeconds;

    bool _gameIsActive; // Indica se il round è in corso (Timer Globale)
    bool _bombIsActive; // Indica se la bomba è armata e ticchetta

    bool _isServerAuthoritative;

    String _endGameStatus;

    // --- Funzioni Private ---

    // Funzioni di visualizzazione per le varie schermate
    void displaySubMenu();
    void displaySettingsMenu();
    void displayEditScreen(const String& title, const String& currentValue, const String& unit);
    void displayBooleanEditScreen(const String& title, bool currentSelection);
    void displayConfirmScreen();
    void displayAwaitArmScreen();
    void displayArmingScreen(unsigned long progress);
    void displayEnterPinScreen(const String& title);
    void displayCountdownLayout();
    void updateCountdownDisplay(long remainingSeconds);
    void displayDefusingScreen(unsigned long progress);

    // Funzioni che contengono la logica per ogni stato
    void handleSubMenuInput(char key, bool btn1, bool btn2);
    void handleSettingsInput(char key, bool btn1, bool btn2);
    void handleEditInput(char key, bool btn1, bool btn2);
    void handleBooleanEditInput(char key, bool btn1, bool btn2);
    void handleInGame(char key, bool btn1_is_pressed, bool btn1_was_pressed, bool btn2_is_pressed, bool btn2_was_pressed);
    void handleArmedState();
    void handleCountdownState(char key, bool btn1_was_pressed, bool btn2_is_pressed, bool btn2_was_pressed);
    void handleIsDefusingInput(bool btn2_is_pressed);
    void handleEnterDefusePinInput(char key, bool btn1_was_pressed, bool btn2_was_pressed);
    
    // Funzione di utilità per aggiornare le schermate di modifica
    void updateDisplayForCurrentState();

    void triggerDefuse();

    unsigned long _lastTelemetryTime;

    String _pendingServerEvent;         // L'evento da martellare in QoS
    unsigned long _lastServerRetryTime; // Timer del retry
    
};

#endif // SEARCH_DESTROY_MODE_H