/**
 * @file HoldButtonInteraction.h
 * @brief Step di interazione basato sulla pressione prolungata di un pulsante (P1 o P2).
 *
 * Gestisce autonomamente:
 * - Il disegno incrementale pixel-per-pixel della barra di avanzamento sull'LCD 20x4.
 * - La generazione dello sweep audio continuo I2S (frequenza crescente o decrescente).
 * - Il lampeggio della striscia LED WS2812 nel colore associato all'azione.
 * - L'aggiornamento dinamico delle etichette sugli OLED 1 e 2.
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
     * @param actionLabel Etichetta mostrata sull'OLED del pulsante (es. "INNESCA" o "ALPHA\nCONQUISTA").
     */
    HoldButtonInteraction(uint8_t buttonIndex, uint32_t durationSec,
                          uint16_t startFreq, uint16_t endFreq,
                          uint8_t ledR, uint8_t ledG, uint8_t ledB,
                          EventType cancelEvent = EV_NONE,
                          const String& actionLabel = "");
    ~HoldButtonInteraction() override;

    // --- Interfaccia Interaction ---
    void begin() override;
    void handleEvent(const SystemEvent& event) override;
    void update() override;

    InteractionResult getResult() const override;
    uint8_t getProgress() const override;

    void setActionLabel(const String& label);

    void setLedColors(uint8_t fgR, uint8_t fgG, uint8_t fgB,
                  uint8_t bgR, uint8_t bgG, uint8_t bgB);

private:
    uint8_t _buttonIndex;       // Pulsante monitorato: 1 = P1, 2 = P2
    uint32_t _durationMs;       // Durata richiesta in millisecondi
    uint16_t _startFreq;        // Frequenza di partenza dello sweep (Hz)
    uint16_t _endFreq;          // Frequenza finale dello sweep (Hz)
    uint8_t _ledR, _ledG, _ledB;// Colore lampeggio LED
    uint8_t _bgR, _bgG, _bgB;
    EventType _cancelEvent;     // Evento per annullare prima dell'inizio pressione
    String _actionLabel;        // Etichetta OLED associata all'azione

    bool _holdingStarted;       // true se l'utente ha iniziato a tenere premuto il pulsante
    unsigned long _startTime;   // Timestamp (millis) di inizio pressione
    InteractionResult _result;  // Stato corrente dello step

    bool isTargetButtonPressed() const;
    void applyOledLabels(bool isHolding) const;
};

#endif // HOLD_BUTTON_INTERACTION_H