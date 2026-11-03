/**
 * @file events.h
 * @brief Definizione degli eventi di sistema e della struttura dati per la coda FreeRTOS.
 *
 * Tutti gli input fisici (Core 0) e i comandi di rete MQTT (Core 1) vengono incapsulati
 * in oggetti SystemEvent e inviati alla coda principale (eventQueue) consumata dal loop().
 */

#ifndef EVENTS_H
#define EVENTS_H

#include <Arduino.h>

/**
 * @enum EventType
 * @brief Tipologie di eventi generabili dai task hardware e di rete.
 */
enum EventType {
    EV_NONE,
    EV_BTN_1_PRESSED,       // Fronte di salita pulsante principale P1
    EV_BTN_2_PRESSED,       // Fronte di salita pulsante principale P2
    EV_KEYPAD_PRESSED,      // Pressione di un tasto sulla matrice 4x4 (valore in charValue)
    EV_KEY_1_STATE_CHANGED, // Cambio stato chiave K1 (stato attuale ON/OFF in boolValue)
    EV_KEY_2_STATE_CHANGED, // Cambio stato chiave K2 (stato attuale ON/OFF in boolValue)
    EV_RFID_SCANNED,        // Scansione tag NFC PN532 (UID esadecimale in stringData)
    EV_WIFI_CONNECTED,      // Connessione Wi-Fi stabilita
    EV_MQTT_CONNECTED,      // Connessione al broker MQTT stabilita
    EV_SERVER_COMMAND       // Comando JSON ricevuto dal server via MQTT (payload in stringData)
};

/**
 * @struct SystemEvent
 * @brief Messaggio standardizzato trasmesso attraverso la Queue FreeRTOS.
 */
struct SystemEvent {
    EventType type;         // Tipo di evento
    char charValue;         // Carattere associato (es. tasto premuto sul keypad)
    bool boolValue;         // Valore booleano associato (es. stato chiave inserita/estratta)
    char stringData[1024];  // Payload testuale (es. UID RFID o JSON completo da MQTT)
};

#endif // EVENTS_H