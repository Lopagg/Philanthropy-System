/**
 * @file RfidInteraction.h
 * @brief Step di interazione per la scansione e validazione di tessere NFC/RFID (PN532).
 *
 * Supporta:
 * - Validazione di qualsiasi tessera ("*"), di una lista di UID separati da virgola
 *   (squadra o giocatori specifici risolti dal server) o di un singolo UID custom.
 * - Invio sistematico al server del pacchetto di telemetria RFID_SCAN ad ogni lettura,
 *   indicando l'UID normalizzato, l'esito (accepted) e il contesto della scansione.
 */

#ifndef RFID_INTERACTION_H
#define RFID_INTERACTION_H

#include "Interactions/Interaction.h"

class RfidInteraction : public Interaction {
public:
    /**
     * @brief Costruttore dello step RFID.
     * @param allowedUids Stringa contenente "*" (qualsiasi) oppure una lista di UID autorizzati
     *                    separati da virgola (es. "04863EB76F6180,04112233445566").
     * @param cancelEvent Evento pulsante per annullare lo step (default: EV_BTN_1_PRESSED).
     */
    RfidInteraction(const char* allowedUids = "*", EventType cancelEvent = EV_BTN_1_PRESSED);
    ~RfidInteraction() override {}

    // --- Interfaccia Interaction ---
    void begin() override;
    void handleEvent(const SystemEvent& event) override;
    void update() override;

    InteractionResult getResult() const override;
    uint8_t getProgress() const override;
    bool isRfidStep() const override { return true; }

    // --- Validazione e Telemetria ---

    /**
     * @brief Verifica se l'UID letto rientra tra quelli autorizzati per questo step.
     */
    bool isUidAllowed(const String& rawUid) const;

    /**
     * @brief Rimuove eventuali separatori (es. ':') e converte l'UID in maiuscolo.
     */
    static String normalizeUid(const String& raw);

    /**
     * @brief Invia al server via MQTT il pacchetto RFID_SCAN per statistiche e log live.
     * @param uid      UID della tessera scansionata.
     * @param accepted true se la tessera è stata accettata dallo step, false altrimenti.
     * @param context  Stringa descrittiva della fase (es. "STEP", "ARM_START", "SAFE_IDLE").
     */
    static void sendRfidTelemetry(const String& uid, bool accepted, const char* context);

private:
    String _allowedUids;        // Lista di UID ammessi separati da virgola (o "*")
    EventType _cancelEvent;     // Evento di annullamento
    InteractionResult _result;  // Stato corrente dello step

    bool _showingError;         // true mentre "TESSERA ERRATA!" è visibile sull'LCD
    unsigned long _errorTime;   // Timestamp (millis) per la pulizia automatica dell'errore
};

#endif // RFID_INTERACTION_H