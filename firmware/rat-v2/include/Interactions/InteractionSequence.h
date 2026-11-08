/**
 * @file InteractionSequence.h
 * @brief Contenitore e orchestratore di una catena ordinata di step Interaction (max 5).
 *
 * Legge direttamente i JsonArray ("arm_seq" / "defuse_seq" / "alpha_seq" / "bravo_seq")
 * inviati dal server, istanzia dinamicamente gli step corrispondenti (HOLD, PIN, KEY, RFID),
 * aggiorna l'intestazione sull'LCD (es. "INNESCO [1/3]") e avanza automaticamente da uno step
 * al successivo verificando che i vincoli fisici precedenti (es. chiavi girate) restino attivi.
 */

#ifndef INTERACTION_SEQUENCE_H
#define INTERACTION_SEQUENCE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Interactions/Interaction.h"

class InteractionSequence {
public:
    static const uint8_t MAX_STEPS = 5;

    /**
     * @brief Costruttore della sequenza di interazione.
     * @param headerTitle     Titolo mostrato sulla riga 0 dell'LCD (es. "INNESCO" o "CONQUISTA A").
     * @param holdButtonIndex Indice del pulsante associato agli step HOLD (1 = P1, 2 = P2).
     * @param sweepStartFreq  Frequenza iniziale dello sweep per gli step HOLD (Hz).
     * @param sweepEndFreq    Frequenza finale dello sweep per gli step HOLD (Hz).
     * @param ledR            Componente Rossa LED per gli step HOLD.
     * @param ledG            Componente Verde LED per gli step HOLD.
     * @param ledB            Componente Blu LED per gli step HOLD.
     * @param cancelEvent     Evento pulsante per annullare la sequenza (default: EV_BTN_1_PRESSED).
     * @param holdActionLabel Etichetta mostrata sull'OLED durante gli step HOLD.
     */
    InteractionSequence(const char* headerTitle, uint8_t holdButtonIndex,
                        uint16_t sweepStartFreq, uint16_t sweepEndFreq,
                        uint8_t ledR, uint8_t ledG, uint8_t ledB,
                        EventType cancelEvent,
                        const char* holdActionLabel = nullptr);
    ~InteractionSequence();

    // --- Configurazione degli Step ---
    void clearSteps();
    void setHoldActionLabel(const String& label);
    void setDefaultHoldStep(uint32_t seconds);
    void loadFromJson(JsonArray arr, uint32_t fallbackHoldSec);

    void setFailMode(SequenceFailMode mode) { _failMode = mode; }
    SequenceFailMode getFailMode() const { return _failMode; }
    static SequenceFailMode parseFailMode(const char* str, bool allowExplode);

    void begin();
    bool canStartWithKeyEvent(const SystemEvent& event) const;
    void beginWithDirectKeyEvent(const SystemEvent& event);

    bool isFirstStepRfid() const;
    bool canStartWithRfidEvent(const SystemEvent& event) const;
    bool tryStartWithRfidEvent(const SystemEvent& event, const char* context);

    bool areInitialKeysReady(String& outHint) const;

    void handleEvent(const SystemEvent& event);
    void update();

    InteractionResult getResult() const;
    uint8_t getProgress() const;

private:
    const char* _headerTitle;
    uint8_t _holdButtonIndex;
    uint16_t _sweepStartFreq;
    uint16_t _sweepEndFreq;
    uint8_t _ledR, _ledG, _ledB;
    EventType _cancelEvent;
    String _holdActionLabel;

    Interaction* _steps[MAX_STEPS];
    uint8_t _stepTypeTags[MAX_STEPS];
    uint8_t _stepCount;
    uint8_t _currentStep;
    InteractionResult _result;

    SequenceFailMode _failMode;
    bool _waitingFailDelay;
    unsigned long _failDelayStartMs;

    void startCurrentStep();

    /**
     * @brief Verifica che tutte le condizioni fisiche degli step precedenti (es. chiavi lasciate
     *        inserite) siano ancora mantenute; in caso contrario annulla la sequenza con tono di errore.
     */
    bool checkPriorConditionsMaintained();
    void handleStepFailure(bool isHoldStep);
};

#endif