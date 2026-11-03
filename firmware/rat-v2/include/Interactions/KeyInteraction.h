/**
 * @file KeyInteraction.h
 * @brief Step di interazione per i selettori a chiave fisica K1 e K2.
 *
 * Supporta 8 modalità operative:
 * - Attivazione/Estrazione singola chiave (K1_ON, K1_OFF, K2_ON, K2_OFF).
 * - Attivazione/Estrazione doppia chiave indipendente (BOTH_ON, BOTH_OFF).
 * - Attivazione/Estrazione doppia chiave sincronizzata "Two-Man Rule" entro 1.5s
 *   (BOTH_SYNC_ON, BOTH_SYNC_OFF).
 *
 * Fornisce inoltre il controllo di mantenimento continuo (isConditionMaintained) per
 * obbligare i giocatori a lasciare la chiave inserita durante gli step successivi.
 */

#ifndef KEY_INTERACTION_H
#define KEY_INTERACTION_H

#include "Interactions/Interaction.h"

/**
 * @enum KeyActionMode
 * @brief Modalità operative configurabili per i due selettori a chiave K1 e K2.
 */
enum KeyActionMode {
    KEY_MODE_K1_ON,         // Slot 1 -> ON  (Inserisci e gira K1)
    KEY_MODE_K1_OFF,        // Slot 1 -> OFF (Gira ed estrai K1)
    KEY_MODE_K2_ON,         // Slot 2 -> ON  (Inserisci e gira K2)
    KEY_MODE_K2_OFF,        // Slot 2 -> OFF (Gira ed estrai K2)
    KEY_MODE_BOTH_ON,       // K1 & K2 -> ON  (Entrambe inserite)
    KEY_MODE_BOTH_OFF,      // K1 & K2 -> OFF (Entrambe estratte)
    KEY_MODE_BOTH_SYNC_ON,  // K1 & K2 -> ON simultaneo (entro SYNC_WINDOW_MS)
    KEY_MODE_BOTH_SYNC_OFF  // K1 & K2 -> OFF simultaneo (entro SYNC_WINDOW_MS)
};

class KeyInteraction : public Interaction {
public:
    // Finestra temporale massima (ms) tra la prima e la seconda chiave nelle modalità SYNC
    static const uint32_t SYNC_WINDOW_MS = 1500;

    /**
     * @brief Costruttore dello step chiave.
     * @param mode        Modalità richiesta (default: KEY_MODE_K1_ON).
     * @param cancelEvent Evento pulsante per annullare lo step (default: EV_BTN_1_PRESSED).
     */
    KeyInteraction(KeyActionMode mode = KEY_MODE_K1_ON, EventType cancelEvent = EV_BTN_1_PRESSED);
    ~KeyInteraction() override {}

    // --- Interfaccia Interaction ---
    void begin() override;
    void handleEvent(const SystemEvent& event) override;
    void update() override;

    InteractionResult getResult() const override;
    uint8_t getProgress() const override;
    bool isConditionMaintained() const override;
    uint8_t getKeyMask() const override;

    // --- Metodi per Avvio Diretto e Verifica Stato Iniziale ---

    /**
     * @brief Avvia lo step direttamente a seguito della rotazione fisica della chiave
     *        (quando lo step KEY è il primo della sequenza).
     */
    void beginFromDirectEvent(const SystemEvent& event);

    /**
     * @brief Verifica se un evento di cambio stato chiave corrisponde all'azione di avvio
     *        di questo step.
     */
    bool matchesDirectStartEvent(const SystemEvent& event) const;

    /**
     * @brief Controlla se le chiavi coinvolte si trovano nella posizione di partenza corretta
     *        prima dell'inizio della sequenza; in caso contrario popola outHint con il messaggio per l'LCD.
     */
    bool isInitialStateReady(String& outHint) const;

    /**
     * @brief Converte la stringa ricevuta via JSON dal server nell'enum KeyActionMode corrispondente.
     */
    static KeyActionMode parseMode(const char* modeStr);

private:
    /**
     * @enum SyncPhase
     * @brief Sotto-stati della macchina a stati per la rotazione sincronizzata di K1 + K2.
     */
    enum SyncPhase {
        SYNC_WAIT_READY,     // Attesa che entrambe le chiavi siano nella posizione di partenza
        SYNC_ARMED,          // Pronte: in attesa che venga girata la prima chiave
        SYNC_FIRST_TURNED,   // Prima chiave girata: timer di 1.5s attivo per la seconda chiave
        SYNC_ERROR_COOLDOWN  // Timeout scaduto ("FUORI SINCRONIA!"): attesa 1.2s prima del reset
    };

    KeyActionMode _mode;        // Modalità operativa configurata
    EventType _cancelEvent;     // Evento di annullamento
    InteractionResult _result;  // Stato corrente dello step

    bool _needsResetFirst;      // true se la chiave era già in posizione target all'ingresso dello step
    SyncPhase _syncPhase;       // Fase corrente per le modalità BOTH_SYNC_ON / BOTH_SYNC_OFF
    unsigned long _syncTimerMs; // Timer per finestra di sincronia (1.5s) e cooldown errore (1.2s)
    bool _lastK1;               // Ultimo stato letto per K1
    bool _lastK2;               // Ultimo stato letto per K2

    bool isSyncMode() const;
    bool isTargetStateReached() const;
    void renderPrompt();
    void renderDualStatusLine();
};

#endif // KEY_INTERACTION_H