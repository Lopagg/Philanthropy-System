/**
 * @file PinInteraction.h
 * @brief Step di interazione per l'inserimento di un codice PIN tramite tastierino 4x4.
 *
 * Gestisce autonomamente:
 * - L'acquisizione dei caratteri alfanumerici e la visualizzazione centrata sull'LCD.
 * - La cancellazione dell'ultimo carattere inserito tramite il tasto '*'.
 * - Il feedback sonoro su ogni pressione tasto (700 Hz), cancellazione (500 Hz),
 *   codice corretto (1500 Hz) e codice errato (250 Hz).
 * - Il ripristino automatico non-bloccante della riga LCD dopo il messaggio "PIN ERRATO!".
 */

#ifndef PIN_INTERACTION_H
#define PIN_INTERACTION_H

#include "Interactions/Interaction.h"

class PinInteraction : public Interaction {
public:
    /**
     * @brief Costruttore dello step di verifica PIN.
     * @param targetPin   Stringa contenente il codice PIN richiesto (max 15 caratteri).
     * @param cancelEvent Evento pulsante per annullare l'operazione (default: EV_BTN_1_PRESSED).
     */
    PinInteraction(const char* targetPin, EventType cancelEvent = EV_BTN_1_PRESSED);
    ~PinInteraction() override {}

    // --- Interfaccia Interaction ---
    void begin() override;
    void handleEvent(const SystemEvent& event) override;
    void update() override;

    InteractionResult getResult() const override;
    uint8_t getProgress() const override;

private:
    char _targetPin[16];        // Codice PIN atteso
    char _inputBuffer[16];      // Buffer dei caratteri digitati finora dall'utente
    EventType _cancelEvent;     // Evento di annullamento (es. P1 = ANNULLA)

    bool _showingError;         // true mentre la scritta "PIN ERRATO!" è visibile sull'LCD
    unsigned long _errorTime;   // Timestamp (millis) di inizio visualizzazione errore
    InteractionResult _result;  // Stato corrente dello step

    /**
     * @brief Svuota il buffer dei caratteri digitati.
     */
    void resetBuffer();
};

#endif // PIN_INTERACTION_H