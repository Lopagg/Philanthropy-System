/**
 * @file Interaction.h
 * @brief Interfaccia base astratta per tutti i "mattoncini" di interazione utente del RAT-V2.
 *
 * Ogni step elementare di una sequenza (pressione prolungata pulsante, inserimento PIN,
 * rotazione chiavi, scansione RFID) eredita da questa classe, esponendo un ciclo di vita
 * uniforme (begin -> handleEvent / update -> getResult / getProgress) gestibile in modo
 * polimorfico da InteractionSequence o da qualunque GameMode.
 */

#ifndef INTERACTION_H
#define INTERACTION_H

#include <Arduino.h>
#include "events.h"
#include "HardwareManager.h"

/**
 * @enum InteractionResult
 * @brief Possibili stati di avanzamento di una singola interazione o di un'intera sequenza.
 */
enum InteractionResult {
    INTERACTION_RUNNING,    // Interazione in corso di svolgimento
    INTERACTION_COMPLETED,  // Interazione completata con successo
    INTERACTION_CANCELLED,   // Interazione annullata dall'utente o interrotta per errore/violazione vincoli
    INTERACTION_FAILED
};

enum SequenceFailMode : uint8_t {
    SEQ_FAIL_RETRY_STEP  = 0, // Ripete lo step che è stato sbagliato
    SEQ_FAIL_RESTART_SEQ = 1, // Ripete l'intera sequenza da capo
    SEQ_FAIL_EXPLODE     = 2  // La bomba esplode immediatamente (solo Disinnesco)
};

class Interaction {
public:
    virtual ~Interaction() {}

    // --- Ciclo di Vita Principale ---

    /**
     * @brief Inizializza lo step, aggiorna le righe di prompt sull'LCD e le etichette OLED.
     */
    virtual void begin() = 0;

    /**
     * @brief Elabora un evento discreto proveniente dalla coda FreeRTOS (tasti, chiavi, RFID).
     * @param event Evento generato dall'inputTask.
     */
    virtual void handleEvent(const SystemEvent& event) = 0;

    /**
     * @brief Aggiornamento continuo (~50 Hz) per animazioni LCD, timer di timeout e controlli stato.
     */
    virtual void update() = 0;

    // --- Stato e Telemetria ---

    /**
     * @brief Restituisce l'esito corrente dello step (RUNNING, COMPLETED, CANCELLED).
     */
    virtual InteractionResult getResult() const = 0;

    /**
     * @brief Restituisce la percentuale di avanzamento (0..100) da inviare via MQTT al server.
     */
    virtual uint8_t getProgress() const = 0;

    // --- Vincoli Fisici e Introspezione Polimorfica ---

    /**
     * @brief Verifica se una condizione fisica richiesta dallo step (es. chiave mantenuta su ON)
     *        è ancora rispettata anche durante gli step successivi della sequenza.
     */
    virtual bool isConditionMaintained() const { return true; }

    /**
     * @brief Restituisce una maschera bit delle chiavi fisiche coinvolte dallo step:
     *        0x00 = nessuna chiave, 0x01 = K1, 0x02 = K2, 0x03 = K1 + K2.
     */
    virtual uint8_t getKeyMask() const { return 0; }

    /**
     * @brief Indica se lo step corrente è un'interazione basata su lettore RFID/NFC.
     */
    virtual bool isRfidStep() const { return false; }
};

#endif // INTERACTION_H