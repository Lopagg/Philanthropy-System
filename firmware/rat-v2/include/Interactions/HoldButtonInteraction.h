/**
 * @file HoldButtonInteraction.h
 * @brief Step di interazione basato sulla pressione prolungata di un pulsante (P1 o P2).
 *
 * Gestisce autonomamente:
 * - Il disegno incrementale pixel-per-pixel della barra di avanzamento sull'LCD 20x4.
 * - La generazione dello sweep audio continuo I2S (frequenza crescente o decrescente).
 * - Il lampeggio della striscia LED WS2812 nel colore associato all'azione.
 * - Il supporto sia come primo step (pulsante già premuto all'ingresso) sia come step
 *   intermedio/finale (attesa che il giocatore prema il pulsante richiesto).
 */

#ifndef HOLD_BUTTON_INTERACTION_H
#define HOLD_BUTTON_INTERACTION_H

#include "Interactions/Interaction.h"

class HoldButtonInteraction : public Interaction {
public:
    /**
     * @brief Costruttore dello step di pressione prolungata.
     * @param buttonIndex Indice del pulsante richiesto (1 = P1, 2 = P2).
     * @param durationSec Durata della pressione richiesta in secondi.
     * @param startFreq   Frequenza iniziale dello sweep audio (Hz).
     * @param endFreq     Frequenza finale dello sweep audio (Hz).
     * @param ledR        Componente Rossa (0..255) del lampeggio LED durante la pressione.
     * @param ledG        Componente Verde (0..255) del lampeggio LED durante la pressione.
     * @param ledB        Componente Blu (0..255) del lampeggio LED durante la pressione.
     * @param cancelEvent Evento di annullamento se lo step è in attesa dell'inizio pressione.
     */
    HoldButtonInteraction(uint8_t buttonIndex, uint32_t durationSec,
                          uint16_t startFreq, uint16_t endFreq,
                          uint8_t ledR, uint8_t ledG, uint8_t ledB,
                          EventType cancelEvent = EV_NONE);
    ~HoldButtonInteraction() override;

    // --- Interfaccia Interaction ---
    void begin() override;
    void handleEvent(const SystemEvent& event) override;
    void update() override;

    InteractionResult getResult() const override;
    uint8_t getProgress() const override;

private:
    uint8_t _buttonIndex;       // Pulsante monitorato: 1 = P1, 2 = P2
    uint32_t _durationMs;       // Durata richiesta in millisecondi
    uint16_t _startFreq;        // Frequenza di partenza dello sweep (Hz)
    uint16_t _endFreq;          // Frequenza finale dello sweep (Hz)
    uint8_t _ledR, _ledG, _ledB;// Colore lampeggio LED
    EventType _cancelEvent;     // Evento per annullare prima dell'inizio pressione

    bool _holdingStarted;       // true se l'utente ha iniziato a tenere premuto il pulsante
    unsigned long _startTime;   // Timestamp (millis) di inizio pressione
    InteractionResult _result;  // Stato corrente dello step

    /**
     * @brief Legge lo stato fisico attuale del pulsante target tramite HardwareManager.
     */
    bool isTargetButtonPressed() const;
};

#endif // HOLD_BUTTON_INTERACTION_H