/**
 * @file NetworkManager.cpp
 * @brief Implementazione dello stack di rete Wi-Fi, sync NTP/RTC, ArduinoOTA e client MQTT.
 */

#include "NetworkManager.h"
#include "HardwareManager.h"

#include <HTTPClient.h>
#include <HTTPUpdate.h>

extern HardwareManager hardware;

NetworkManager* NetworkManager::_instance = nullptr;

NetworkManager::NetworkManager()
    : _currentModeName("STANDBY"),
      _lastReconnectAttempt(0),
      _lastHeartbeat(0) {
    _instance = this;
}

void NetworkManager::setCurrentModeName(const char* modeName) {
    _currentModeName = modeName ? String(modeName) : "STANDBY";
    publishHeartbeat();
}

void NetworkManager::setupOTA() {
    ArduinoOTA.setHostname(_deviceId.c_str());

    ArduinoOTA.onStart([]() {
        hardware.noTone();
        hardware.clearLcd();
        hardware.printLcdLine(0, "AGGIORNAMENTO OTA", true);
        hardware.printLcdLine(1, "Non spegnere...", true);
        hardware.clearOled();
        hardware.setLedsColor(0, 0, 255);
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t lastPct = 255;
        uint8_t pct = (total > 0) ? ((progress * 100) / total) : 0;
        
        if (pct != lastPct && (pct % 2 == 0 || pct == 100)) {
            lastPct = pct;
            hardware.drawProgressBar(2, progress, total);
            hardware.printLcdLine(3, String(pct) + "%", true);
        }
    });

    ArduinoOTA.onEnd([]() {
        hardware.clearLcd();
        hardware.printLcdLine(1, "OTA COMPLETATO!", true);
        hardware.printLcdLine(2, "Riavvio...", true);
        hardware.setLedsColor(0, 255, 0);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        hardware.clearLcd();
        hardware.printLcdLine(1, "ERRORE OTA!", true);
        hardware.setLedsColor(255, 0, 0);
    });

    ArduinoOTA.begin();
    Serial.println("[OTA] Pronto su " + _deviceId + ".local (" + WiFi.localIP().toString() + ")");
}

void NetworkManager::begin(const char* deviceId, const char* mqttServer, uint16_t mqttPort, QueueHandle_t mainQueue) {
    _deviceId = deviceId;
    _eventQueue = mainQueue;
    
    _telemetryTopic = "zulu/telemetry/" + _deviceId;
    _cmdTopic = "zulu/cmd/" + _deviceId;

    hardware.clearLcd();
    hardware.printLcdLine(0, "CONNESSIONE WIFI...", true);
    hardware.printLcdLine(1, "Ricerca rete nota", true);
    hardware.printLcdLine(2, "o AP: " + _deviceId, true);
    hardware.printLcdLine(3, "FW: " + String(FIRMWARE_VERSION), true);

    Serial.println("[NET] Avvio WiFiManager...");
    WiFiManager wm;
    
    if (!wm.autoConnect(_deviceId.c_str(), "philanthropy")) {
        Serial.println("[NET] Errore connessione WiFi, riavvio...");
        ESP.restart();
    }

    WiFi.setSleep(false);
    
    String ssid = WiFi.SSID();
    String ip   = WiFi.localIP().toString();
    Serial.println("[NET] WiFi Connesso a '" + ssid + "'! IP: " + ip);

    // Schermata di riepilogo Rete + Sincronizzazione RTC su fuso orario italiano
    hardware.clearLcd();
    hardware.printLcdLine(0, "WIFI CONNESSO!", true);
    hardware.printLcdLine(1, "RETE: " + ssid, false);
    hardware.printLcdLine(2, "IP: " + ip, false);
    hardware.printLcdLine(3, "Sync RTC (NTP)...", true);

    bool ntpOk = hardware.syncRTCWithNTP(4000);
    DateTime now = hardware.getRTCTime();
    char timeBuf[21];
    snprintf(timeBuf, sizeof(timeBuf), "%s %02d:%02d:%02d",
             ntpOk ? "RTC SYNC:" : "RTC LOC:", now.hour(), now.minute(), now.second());
    hardware.printLcdLine(3, String(timeBuf), true);

    setupOTA();

    // Pausa di 2.5s per consentire la lettura di SSID, IP e orario all'operatore
    vTaskDelay(pdMS_TO_TICKS(2500));
    
    SystemEvent ev = {EV_WIFI_CONNECTED, '\0', false, ""};
    xQueueSend(_eventQueue, &ev, 0);

    _mqtt.setClient(_espClient);
    _mqtt.setServer(mqttServer, mqttPort);
    _mqtt.setCallback(NetworkManager::mqttCallback);
    _mqtt.setBufferSize(2048);
}

void NetworkManager::publishHeartbeat() {
    if (!_mqtt.connected()) return;
    float battV   = hardware.readBatteryVoltage();
    int8_t battPct = hardware.readBatteryPercentage();
    String hb = "{\"id\":\"" + _deviceId + "\", \"type\":\"HEARTBEAT\", \"version\":\"" + String(FIRMWARE_VERSION) + "\", \"mode\":\"" + _currentModeName + "\", \"ip\":\"" + WiFi.localIP().toString() + "\", \"batt_v\":" + String(battV, 2) + ", \"batt_pct\":" + String(battPct) + "}";
    _mqtt.publish(_telemetryTopic.c_str(), hb.c_str());
}

void NetworkManager::reconnectMQTT() {
    if (millis() - _lastReconnectAttempt > 5000) {
        _lastReconnectAttempt = millis();
        Serial.print("[NET] Tentativo connessione MQTT... ");
        
        if (_mqtt.connect(_deviceId.c_str())) {
            Serial.println("CONNESSO!");
            _mqtt.subscribe(_cmdTopic.c_str());
            _mqtt.subscribe("zulu/cmd/broadcast");
            
            publishHeartbeat();

            SystemEvent ev = {EV_MQTT_CONNECTED, '\0', false, ""};
            xQueueSend(_eventQueue, &ev, 0);
        } else {
            Serial.print("Fallito, rc=");
            Serial.println(_mqtt.state());
        }
    }
}

void NetworkManager::loop() {
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();

        if (!_mqtt.connected()) {
            reconnectMQTT();
        } else {
            _mqtt.loop();
            
            if (millis() - _lastHeartbeat > 5000) {
                _lastHeartbeat = millis();
                publishHeartbeat();
            }
        }
    }
}

void NetworkManager::sendTelemetry(const String& payload) {
    if (_mqtt.connected()) {
        _mqtt.publish(_telemetryTopic.c_str(), payload.c_str());
    }
}

void NetworkManager::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (!_instance || !_instance->_eventQueue) return;

    String msg;
    msg.reserve(length + 1);
    for (unsigned int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }
    
    Serial.println("[NET] Ricevuto comando: " + msg);

    SystemEvent ev;
    ev.type = EV_SERVER_COMMAND;
    strncpy(ev.stringData, msg.c_str(), sizeof(ev.stringData) - 1);
    ev.stringData[sizeof(ev.stringData) - 1] = '\0';
    
    xQueueSend(_instance->_eventQueue, &ev, 0);
}

void NetworkManager::performHttpOTA(const String& url) {
    Serial.println("[OTA-HTTP] Avvio download da: " + url);
    hardware.noTone();
    hardware.clearLcd();
    hardware.printLcdLine(0, "OTA SERVER UPDATE", true);
    hardware.printLcdLine(1, "Download in corso...", true);
    hardware.clearOled();
    hardware.setLedsColor(0, 0, 255);

    WiFiClient client;
    httpUpdate.rebootOnUpdate(false);

    httpUpdate.onProgress([](int cur, int total) {
        static int lastPct = -1;
        int pct = (total > 0) ? ((cur * 100) / total) : 0;
        if (pct != lastPct && (pct % 2 == 0 || pct == 100)) {
            lastPct = pct;
            hardware.drawProgressBar(2, cur, total);
            hardware.printLcdLine(3, String(pct) + "%", true);
        }
    });

    t_httpUpdate_return ret = httpUpdate.update(client, url);

    if (ret == HTTP_UPDATE_OK) {
        Serial.println("[OTA-HTTP] Aggiornamento completato! Riavvio...");
        hardware.clearLcd();
        hardware.printLcdLine(1, "OTA COMPLETATO!", true);
        hardware.printLcdLine(2, "Riavvio...", true);
        hardware.setLedsColor(0, 255, 0);
        delay(1000);
        ESP.restart();
    } else {
        Serial.printf("[OTA-HTTP] Errore (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        hardware.clearLcd();
        hardware.printLcdLine(1, "ERRORE OTA HTTP!", true);
        hardware.printLcdLine(2, "Cod: " + String(httpUpdate.getLastError()), true);
        hardware.setLedsColor(255, 0, 0);
        hardware.playTone(250, 600);
        delay(2500);
    }
}