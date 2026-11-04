/**
 * @file KioskConfig.h
 * @brief Configurazione pinout, costanti di rete e versione firmware per Tactical Kiosk V1.
 */

#ifndef KIOSK_CONFIG_H
#define KIOSK_CONFIG_H

#include <Arduino.h>

// --- Identificativo Nodo, Rete e Versione Firmware ---
#define KIOSK_DEVICE_ID      "NODE-KIOSK-1"
#define KIOSK_OTA_HOSTNAME   "PHIL-KIOSK"
#define FIRMWARE_VERSION     "v0.0.1"

#define DEFAULT_MQTT_SERVER  "philanthropy-dev.duckdns.org"
#define DEFAULT_MQTT_PORT    1883

// --- Pinout Pulsanti Operatore e Controllo Partita ---
#define PIN_BTN_ALPHA        14
#define PIN_BTN_BRAVO        27
#define PIN_BTN_START        32
#define PIN_BTN_END          15
#define PIN_BTN_SIREN        13

// --- Pinout Attuatori (Buzzer e Relè Sirena) ---
#define PIN_BUZZER           33
#define PIN_RELAY_SIREN      12

// --- Pinout Lettore RFID MFRC522 (SPI) ---
#define PIN_RFID_SS          5
#define PIN_RFID_RST         22

// --- Configurazione Striscia LED WS2812 ---
#define PIN_LED_STRIP        26
#define NUM_LEDS             28  // 0..2: LED Pulsanti | 3..27: Striscia di stato (25 LED)

#define LED_IDX_SIREN        0
#define LED_IDX_END          1
#define LED_IDX_START        2
#define STRIP_START_IDX      3
#define STRIP_END_IDX        27
#define STRIP_LENGTH         25

// --- Tempistiche Operative (ms) ---
#define HEARTBEAT_INTERVAL_MS 5000
#define SERVER_TIMEOUT_MS     3000
#define ASSIGN_TIMEOUT_MS     10000
#define SHOW_RESULT_MS        1800
#define COOLDOWN_MS           400
#define LED_FRAME_INTERVAL_MS 20   // Aggiornamento striscia LED a 50 FPS

/**
 * @enum KioskState
 * @brief Stati operativi della macchina a stati del Kiosk.
 */
enum KioskState {
    STATE_WAIT_TAG,         // In attesa di una tessera RFID sul lettore
    STATE_WAIT_SERVER,      // Tag inviato: in attesa di KIOSK_REPLY dal server
    STATE_WAIT_ASSIGNMENT,  // Tag non in roster: attesa pressione pulsante ALPHA o BRAVO
    STATE_SHOW_RESULT,      // Visualizzazione animazione IN/OUT (verde/rosso/fazione)
    STATE_ERROR_FLASH,      // Lampeggio rosso non-bloccante per errore/timeout server
    STATE_COOLDOWN          // Pausa anti-rimbalzo prima della lettura successiva
};

#endif // KIOSK_CONFIG_H