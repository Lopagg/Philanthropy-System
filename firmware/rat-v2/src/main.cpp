/**
 * @file main.cpp
 * @brief Entry point e orchestratore FreeRTOS del terminale tattico RAT-V2.
 *
 * Architettura Multitasking:
 * - Core 0 (inputTask)  : Scansione continua a bassa latenza di MCP23017 (pulsanti, chiavi, keypad) e RFID PN532.
 * - Core 1 (audioTask)  : Sintesi digitale I2S e streaming MP3 da Micro-SD ad alta priorità.
 * - Core 1 (networkTask): Gestione Wi-Fi, sync NTP/RTC, ArduinoOTA e client MQTT.
 * - Core 1 (loop)       : Schermata Standby ("REMOTE ACCESS TERMINAL"), diagnostica e delega alla GameMode attiva.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include "HardwareManager.h"
#include "NetworkManager.h"
#include "events.h"
#include "GameModes/SearchDestroyMode.h"
#include "GameModes/HardwareTestMode.h"

// ============================================================================
// CONFIGURAZIONE NODO E VARIABILI GLOBALI
// ============================================================================

const char* MQTT_SERVER = "philanthropy-dev.duckdns.org";
const char* DEVICE_ID   = "RAT-V2";

HardwareManager hardware;
NetworkManager network;

GameMode* currentGameMode = nullptr;
bool gameIsRunning = false;
bool inHardwareTest = false;
bool showingNetInfoOverlay = false;
unsigned long netInfoOverlayStartMs = 0;

TaskHandle_t InputTaskHandle;
TaskHandle_t AudioTaskHandle;
TaskHandle_t NetworkTaskHandle;
QueueHandle_t eventQueue;

// ============================================================================
// TASK FREERTOS DEDICATI
// ============================================================================

void inputTask(void * pvParameters) {
    bool lastK1 = hardware.isKey1Inserted();
    bool lastK2 = hardware.isKey2Inserted();
    unsigned long lastRfidCheck = 0;
    String lastScannedUid = "";
    unsigned long lastScanTime = 0;

    for (;;) {
        hardware.updateInputs();

        if (hardware.wasButton1Pressed()) {
            SystemEvent ev = {EV_BTN_1_PRESSED, '\0', false, ""};
            xQueueSend(eventQueue, &ev, 0);
        }
        if (hardware.wasButton2Pressed()) {
            SystemEvent ev = {EV_BTN_2_PRESSED, '\0', false, ""};
            xQueueSend(eventQueue, &ev, 0);
        }

        bool currentK1 = hardware.isKey1Inserted();
        if (currentK1 != lastK1) {
            lastK1 = currentK1;
            SystemEvent ev = {EV_KEY_1_STATE_CHANGED, '\0', currentK1, ""};
            xQueueSend(eventQueue, &ev, 0);
        }

        bool currentK2 = hardware.isKey2Inserted();
        if (currentK2 != lastK2) {
            lastK2 = currentK2;
            SystemEvent ev = {EV_KEY_2_STATE_CHANGED, '\0', currentK2, ""};
            xQueueSend(eventQueue, &ev, 0);
        }

        char key = hardware.getKeyPressed();
        if (key != '\0') {
            SystemEvent ev = {EV_KEYPAD_PRESSED, key, false, ""};
            xQueueSend(eventQueue, &ev, 0);
        }

        if (millis() - lastRfidCheck > 250) {
            lastRfidCheck = millis();
            String uid = hardware.readRFID(20);
            if (uid.length() > 0) {
                if (uid != lastScannedUid || (millis() - lastScanTime > 1200)) {
                    lastScannedUid = uid;
                    lastScanTime = millis();
                    SystemEvent ev = {EV_RFID_SCANNED, '\0', false, ""};
                    uid.toCharArray(ev.stringData, sizeof(ev.stringData));
                    xQueueSend(eventQueue, &ev, 0);
                }
            } else {
                lastScannedUid = "";
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void audioTask(void * pvParameters) {
    for (;;) {
        hardware.audioLoop();
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}

void networkTask(void * pvParameters) {
    network.begin(DEVICE_ID, MQTT_SERVER, 1883, eventQueue);
    
    for (;;) {
        network.loop();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// SCHERMATE DI STANDBY E INFO RETE / SISTEMA
// ============================================================================

void showStandbyScreen() {
    showingNetInfoOverlay = false;
    hardware.noTone();
    hardware.clearLcd();
    hardware.printLcdLine(0, "REMOTE ACCESS", true);
    hardware.printLcdLine(1, "TERMINAL", true);
    hardware.printLcdLine(2, "FIRMWARE " + String(FIRMWARE_VERSION), true);
    hardware.printLcdLine(3, "In attesa server...", true);

    // Etichette rigorose per i pulsanti P1 e P2 in Standby
    hardware.setOledLabels("TEST HW", "INFO RETE");
    hardware.setLedsColor(0, 0, 0);
    network.setCurrentModeName("STANDBY");
}

void showNetInfoScreen() {
    showingNetInfoOverlay = true;
    netInfoOverlayStartMs = millis();

    DateTime now = hardware.getRTCTime();
    char timeLine[21];
    snprintf(timeLine, sizeof(timeLine), "RTC: %02d/%02d %02d:%02d:%02d",
             now.day(), now.month(), now.hour(), now.minute(), now.second());

    hardware.clearLcd();
    hardware.printLcdLine(0, "RETE: " + WiFi.SSID(), false);
    hardware.printLcdLine(1, "IP: " + WiFi.localIP().toString(), false);
    hardware.printLcdLine(2, String(timeLine), false);
    hardware.printLcdLine(3, "FW: " + String(FIRMWARE_VERSION), true);
    hardware.setOledLabels("TEST HW", "INDIETRO");
}

void startHardwareTestMode() {
    if (currentGameMode) {
        delete currentGameMode;
        currentGameMode = nullptr;
    }
    gameIsRunning = true;
    inHardwareTest = true;
    showingNetInfoOverlay = false;

    currentGameMode = new HardwareTestMode();
    network.setCurrentModeName("HW_TEST");
    currentGameMode->begin();
}

// ============================================================================
// SETUP E LOOP PRINCIPALE
// ============================================================================

void setup() {
    Serial.begin(115200);
    unsigned long waitStart = millis();
    while (!Serial && (millis() - waitStart < 2500)) { delay(10); }
    delay(500); 

    Serial.println("\n--- AVVIO SISTEMA RAT V2 (" FIRMWARE_VERSION ") ---");

    eventQueue = xQueueCreate(10, sizeof(SystemEvent));
    hardware.initialize();

    hardware.clearLcd();
    hardware.printLcdLine(0, "REMOTE ACCESS", true);
    hardware.printLcdLine(1, "TERMINAL " + String(FIRMWARE_VERSION), true);
    hardware.printLcdLine(3, "Avvio sottosistemi..", true);

    xTaskCreatePinnedToCore(networkTask, "NetTask",   8192, NULL, 1, &NetworkTaskHandle, 1);
    xTaskCreatePinnedToCore(inputTask,   "InputTask", 4096, NULL, 1, &InputTaskHandle,   0);
    xTaskCreatePinnedToCore(audioTask,   "AudioTask", 8192, NULL, 2, &AudioTaskHandle,   1);
}

void loop() {
    SystemEvent ev;
    
    if (xQueueReceive(eventQueue, &ev, pdMS_TO_TICKS(20)) == pdTRUE) {
        
        // 1. Gestione Comandi Remoti dal Server Centrale
        if (ev.type == EV_SERVER_COMMAND) {
            JsonDocument doc;
            if (!deserializeJson(doc, ev.stringData)) {
                const char* cmd = doc["cmd"];
                if (cmd) {
                    if (strcmp(cmd, "SET_SD_SETTINGS") == 0) {
                        if (inHardwareTest && currentGameMode) {
                            delete currentGameMode;
                            currentGameMode = nullptr;
                            inHardwareTest = false;
                            gameIsRunning = false;
                        }
                        if (currentGameMode == nullptr) {
                            currentGameMode = new SearchDestroyMode();
                        }
                        currentGameMode->handleEvent(ev);
                        if (!gameIsRunning) {
                            hardware.playTone(1200, 80);
                            hardware.printLcdLine(3, "CONFIG. RICEVUTA!", true);
                        }
                        return;
                    }
                    else if (strcmp(cmd, "START_SD_GAME") == 0) {
                        Serial.println("[SYS] Avvio motore Search And Destroy");
                        if (inHardwareTest && currentGameMode) {
                            delete currentGameMode;
                            currentGameMode = nullptr;
                            inHardwareTest = false;
                        }
                        if (currentGameMode == nullptr) {
                            currentGameMode = new SearchDestroyMode();
                        }
                        currentGameMode->handleEvent(ev);
                        gameIsRunning = true;
                        inHardwareTest = false;
                        showingNetInfoOverlay = false;
                        network.setCurrentModeName("SEARCH_AND_DESTROY");
                        currentGameMode->begin();
                        return;
                    }
                    else if (strcmp(cmd, "START_HW_TEST") == 0) {
                        Serial.println("[SYS] Avvio modalità Hardware Test");
                        startHardwareTestMode();
                        return;
                    }
                    else if (strcmp(cmd, "RETURN_TO_MENU") == 0) {
                        gameIsRunning = false;
                        inHardwareTest = false;
                        if (currentGameMode) {
                            delete currentGameMode;
                            currentGameMode = nullptr;
                        }
                        showStandbyScreen();
                        return;
                    }
                    else if (strcmp(cmd, "NET_PING_REQ") == 0) {
                        // Avvia un ping misurato verso il server
                        uint32_t nowMs = millis();
                        String payload = "{\"id\":\"" + String(DEVICE_ID) + "\",\"type\":\"NET_PING\",\"payload\":{";
                        payload += "\"client_ts\":" + String(nowMs) + ",";
                        payload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
                        payload += "\"heap_kb\":" + String(ESP.getFreeHeap() / 1024) + ",";
                        payload += "\"ip\":\"" + WiFi.localIP().toString() + "\"}}";
                        network.sendTelemetry(payload);
                        return;
                    }
                    else if (strcmp(cmd, "NET_PONG") == 0 && !inHardwareTest) {
                        uint32_t sentTs = doc["client_ts"] | millis();
                        int32_t rtt = (int32_t)(millis() - sentTs);
                        if (rtt < 1) rtt = 1;
                        hardware.playTone(1800, 50);
                        String rep = "{\"id\":\"" + String(DEVICE_ID) + "\",\"type\":\"NET_DIAG_REPORT\",\"payload\":{";
                        rep += "\"rtt_ms\":" + String(rtt) + ",";
                        rep += "\"rssi\":" + String(WiFi.RSSI()) + ",";
                        rep += "\"heap_kb\":" + String(ESP.getFreeHeap() / 1024) + "}}";
                        network.sendTelemetry(rep);
                        return;
                    }
                    else if (strcmp(cmd, "OTA_HTTP") == 0) {
                     const char* url = doc["url"] | "";
                     if (strlen(url) > 0) {
                         network.performHttpOTA(String(url));
                         showStandbyScreen();
                     }
                     return;
                    }
                    else if (strcmp(cmd, "RESET") == 0) {
                        ESP.restart();
                    }
                }
            }
        }

        // 2. Delega Eventi alla Modalità Attiva oppure Gestione Standby
        if (gameIsRunning && currentGameMode != nullptr) {
            currentGameMode->handleEvent(ev);
        } else {
            if (ev.type == EV_WIFI_CONNECTED || ev.type == EV_MQTT_CONNECTED) {
                showStandbyScreen();
            } else if (ev.type == EV_BTN_1_PRESSED) {
                // P1 ("TEST HW"): entra direttamente nella modalità di collaudo
                startHardwareTestMode();
            } else if (ev.type == EV_BTN_2_PRESSED) {
                // P2 ("INFO RETE" / "INDIETRO"): mostra o chiude il riepilogo rete e RTC
                hardware.playTone(900, 40);
                if (showingNetInfoOverlay) {
                    showStandbyScreen();
                } else {
                    showNetInfoScreen();
                }
            }
        }
    }

    // 3. Aggiornamento Ciclico Modalità Attiva o Timeout Schermata Info Rete
    if (gameIsRunning && currentGameMode != nullptr) {
        currentGameMode->update();

        // Se l'operatore ha premuto "ESCI TEST" (P2 sulla pagina 4), torna a Standby
        if (inHardwareTest) {
            HardwareTestMode* testMode = static_cast<HardwareTestMode*>(currentGameMode);
            if (testMode->isExitRequested()) {
                delete currentGameMode;
                currentGameMode = nullptr;
                gameIsRunning = false;
                inHardwareTest = false;
                showStandbyScreen();
            }
        }
    } else if (showingNetInfoOverlay && (millis() - netInfoOverlayStartMs >= 4000)) {
        showStandbyScreen();
    }
}