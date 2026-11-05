/**
 * @file DominationMode.h
 * @brief Modalità di gioco Dominio (Domination) per RAT-V2.
 *
 * Gestisce il controllo e la contesa di una zona tattica tra due fazioni:
 * - Pulsante P1 (OLED 1) associato alla conquista della Squadra ALPHA.
 * - Pulsante P2 (OLED 2) associato alla conquista della Squadra BRAVO.
 * - Due istanze indipendenti di InteractionSequence (_alphaSequence e _bravoSequence)
 *   con default impostato a HOLD 10 secondi sul rispettivo pulsante di squadra.
 */

#ifndef DOMINATION_MODE_H
#define DOMINATION_MODE_H

#include "GameMode.h"
#include "HardwareManager.h"
#include "NetworkManager.h"
#include "Interactions/InteractionSequence.h"
#include <ArduinoJson.h>

/**
 * @enum DomState
 * @brief Stati principali della macchina a stati Dominio.
 */
enum DomState {
    DOM_NEUTRAL,          // Zona neutra: entrambe le squadre possono avviare la conquista
    DOM_CAPTURING_ALPHA,  // Sequenza di conquista in corso da parte della squadra ALPHA
    DOM_CAPTURING_BRAVO,  // Sequenza di conquista in corso da parte della squadra BRAVO
    DOM_OWNED_ALPHA,      // Zona controllata da ALPHA (solo BRAVO può conquistarla)
    DOM_OWNED_BRAVO,      // Zona controllata da BRAVO (solo ALPHA può conquistarla)
    DOM_ENDED             // Partita terminata dal server (FORCE_WIN o FORCE_END_GAME)
};

/**
 * @enum DomOwner
 * @brief Fazione che detiene attualmente il possesso della zona (anche durante un tentativo di riconquista).
 */
enum DomOwner {
    OWNER_NONE,
    OWNER_ALPHA,
    OWNER_BRAVO
};

class DominationMode : public GameMode {
public:
    DominationMode();
    ~DominationMode() override;

    // --- Interfaccia GameMode ---
    void begin() override;
    void handleEvent(const SystemEvent& event) override;
    void update() override;

private:
    DomState _state;
    DomOwner _owner;

    unsigned long _lastTelemetryTime;
    unsigned long _lastScoreTickTime;

    uint32_t _scoreAlphaSec;
    uint32_t _scoreBravoSec;

    // Nomi e colori RGB delle due fazioni (aggiornabili da SET_FACTIONS)
    String _team1Name;
    String _team2Name;
    uint8_t _t1Color[3];
    uint8_t _t2Color[3];

    // Sequenze di conquista componibili (P1 per Alpha, P2 per Bravo)
    InteractionSequence _alphaSequence;
    InteractionSequence _bravoSequence;

    // Effetti fisici opzionali (impulso alla conquista)
    bool _fxSmoke;
    bool _fxRelay;
    bool _fxPulseActive;
    unsigned long _fxPulseStartTime;

    // Metodi privati di gestione stati, display e telemetria
    void processJsonConfig(const char* jsonStr);
    void enterCurrentOwnerIdleState(bool playCaptureSound);
    void startAlphaCapture();
    void startBravoCapture();
    void renderIdleScreen();
    void renderScoresLine();
    void stopAllEffects();
    void triggerCaptureEffects();

    void setOledLabels(const String& p1Label, const String& p2Label);
    void sendTelemetry();
    void sendCaptureEventToServer(const char* eventName);
};

#endif // DOMINATION_MODE_H