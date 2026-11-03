/**
 * @file SearchDestroyMode.h
 * @brief Modalità di gioco Search & Destroy (Cerca e Distruggi) per RAT-V2.
 *
 * Funge da regista di partita componendo due istanze di InteractionSequence
 * (_armSequence e _defuseSequence) e un CountdownTimer basato su RTC (_bombTimer).
 * La determinazione ufficiale del vincitore è sempre delegata al server centrale.
 */

#ifndef SEARCH_DESTROY_MODE_H
#define SEARCH_DESTROY_MODE_H

#include "GameMode.h"
#include "HardwareManager.h"
#include "NetworkManager.h"
#include "Interactions/InteractionSequence.h"
#include "Interactions/CountdownTimer.h"
#include <ArduinoJson.h>

/**
 * @enum SDState
 * @brief Stati principali della macchina a stati Search & Destroy.
 */
enum SDState {
    SD_SAFE,            // Bomba non ancora innescata (in attesa degli attaccanti)
    SD_ARMING,          // Sequenza di innesco in corso
    SD_ARMED,           // Bomba innescata e conto alla rovescia attivo
    SD_DEFUSING,        // Sequenza di disinnesco in corso (conto alla rovescia ancora attivo)
    SD_WAITING_SERVER,  // Esplosione o disinnesco avvenuto: attesa verdetto FORCE_WIN dal server
    SD_ENDED            // Partita conclusa con verdetto mostrato a schermo
};

class SearchDestroyMode : public GameMode {
public:
    SearchDestroyMode();
    ~SearchDestroyMode() override;

    // --- Interfaccia GameMode ---
    void begin() override;
    void handleEvent(const SystemEvent& event) override;
    void update() override;

private:
    SDState _state;
    unsigned long _lastTelemetryTime;
    unsigned long _lastServerRetryTime;
    String _pendingServerEvent;

    // Moduli componibili per sequenze di interazione e timer bomba
    InteractionSequence _armSequence;
    InteractionSequence _defuseSequence;
    CountdownTimer _bombTimer;

    // Configurazione e gestione non-bloccante degli effetti fisici (Fumo / Relè 12V)
    bool _fxSmoke;
    bool _fxRelay;
    bool _fxPulseActive;
    unsigned long _fxPulseStartTime;

    // Metodi privati di gestione stati e rete
    void processJsonConfig(const char* jsonStr);
    void enterSafeState();
    void enterArmedState(bool playSound);
    void triggerExplosion();
    void triggerDefusal();
    void stopAllEffects();
    void sendMatchEventToServer();
    
    void setOledLabels(const char* p1Label, const char* p2Label);
    void sendTelemetry(const char* stateStr);
};

#endif // SEARCH_DESTROY_MODE_H