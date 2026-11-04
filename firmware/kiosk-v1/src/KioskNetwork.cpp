/**
 * @file KioskNetwork.cpp
 * @brief Implementazione dello stack Wi-Fi, MQTT, Diagnostica di Rete e HTTP OTA per il Kiosk.
 */

#include "KioskNetwork.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>

KioskNetwork* KioskNetwork::_instance = nullptr;

KioskNetwork::KioskNetwork(KioskHardware& hw)
    : _hw(hw),
      _mqtt(_espClient),
      _cmdCallback(nullptr),
      _lastReconnectAttemptMs(0),
      _lastHeartbeatMs(0) {
    _instance = this;
    _telemetryTopic = "zulu/telemetry/" + String(KIOSK_DEVICE_ID);
    _cmdTopic       = "zulu/cmd/" + String(KIOSK_DEVICE_ID);
}

void KioskNetwork::setupOTA() {
    ArduinoOTA.setHostname(KIOSK_OTA_HOSTNAME);

    ArduinoOTA.onStart([this]() {
        Serial.println("[OTA] Avvio aggiornamento firmware...");
        _hw.drawOtaProgress(0);
    });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        static uint8_t lastPct = 255;
        uint8_t pct = (total > 0) ? ((progress * 100) / total) : 0;
        if (pct != lastPct && (pct % 4 == 0 || pct == 100)) {
            lastPct = pct;
            _hw.drawOtaProgress(pct);
        }
    });

    ArduinoOTA.onEnd([this]() {
        Serial.println("\n[OTA] Completato! Riavvio...");
        _hw.showSolidColorAll(0, 255, 0);
    });

    ArduinoOTA.onError([this](ota_error_t error) {
        Serial.printf("[OTA] Errore [%u]\n", error);
        _hw.showSolidColorAll(255, 0, 0);
    });

    ArduinoOTA.begin();
}

void KioskNetwork::begin(ServerCommandCallback callback) {
    _cmdCallback = callback;

    // Segnale viola tenue durante la connessione Wi-Fi
    _hw.showSolidColorAll(40, 0, 60);

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    if (!wm.autoConnect("ZULU-KIOSK")) {
        Serial.println("[NET] Connessione Wi-Fi fallita, riavvio...");
        delay(2000);
        ESP.restart();
    }

    // Disabilita il risparmio energetico Wi-Fi per garantire risposta istantanea ai check-in RFID
    WiFi.setSleep(false);
    Serial.println("[NET] Wi-Fi Connesso! IP: " + WiFi.localIP().toString());

    setupOTA();

    _mqtt.setServer(DEFAULT_MQTT_SERVER, DEFAULT_MQTT_PORT);
    _mqtt.setCallback(KioskNetwork::mqttCallback);
    _mqtt.setBufferSize(1024);
}

void KioskNetwork::reconnectMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;
    Serial.print("[NET] Connessione MQTT Kiosk... ");

    String willPayload = "{\"id\":\"" + String(KIOSK_DEVICE_ID) + "\",\"type\":\"MODE_EXIT\",\"payload\":{\"mode\":\"OFFLINE\"}}";

    if (_mqtt.connect(KIOSK_DEVICE_ID, "admin", "admin", _telemetryTopic.c_str(), 1, true, willPayload.c_str())) {
        Serial.println("CONNESSO!");
        // Sottoscrive SOLO i topic di comando (NON zulu/telemetry/# per evitare traffico inutile!)
        _mqtt.subscribe(_cmdTopic.c_str());
        _mqtt.subscribe("zulu/cmd/broadcast");

        sendHeartbeat();
    } else {
        Serial.printf("Fallita (rc=%d)\n", _mqtt.state());
    }
}

void KioskNetwork::sendHeartbeat() {
    if (!_mqtt.connected()) return;

    JsonDocument doc;
    doc["id"]      = KIOSK_DEVICE_ID;
    doc["type"]    = "HEARTBEAT";
    doc["mode"]    = "KIOSK";
    doc["version"] = FIRMWARE_VERSION;
    doc["ip"]      = WiFi.localIP().toString();

    String out;
    serializeJson(doc, out);
    _mqtt.publish(_telemetryTopic.c_str(), out.c_str());
}

void KioskNetwork::sendTagScanned(const char* uid) {
    if (!_mqtt.connected()) return;

    JsonDocument doc;
    doc["id"]      = KIOSK_DEVICE_ID;
    doc["type"]    = "TAG_SCANNED";
    doc["mode"]    = "KIOSK";
    doc["version"] = FIRMWARE_VERSION;
    doc["ip"]      = WiFi.localIP().toString();
    doc["payload"]["uid"] = uid;

    String out;
    serializeJson(doc, out);
    _mqtt.publish(_telemetryTopic.c_str(), out.c_str());
}

void KioskNetwork::sendTagAssign(const char* uid, const char* team) {
    if (!_mqtt.connected()) return;

    JsonDocument doc;
    doc["id"]      = KIOSK_DEVICE_ID;
    doc["type"]    = "TAG_ASSIGN";
    doc["mode"]    = "KIOSK";
    doc["version"] = FIRMWARE_VERSION;
    doc["ip"]      = WiFi.localIP().toString();
    doc["payload"]["uid"]  = uid;
    doc["payload"]["team"] = team;

    String out;
    serializeJson(doc, out);
    _mqtt.publish(_telemetryTopic.c_str(), out.c_str());
}

void KioskNetwork::sendKioskCommand(const char* cmd) {
    if (!_mqtt.connected()) return;

    JsonDocument doc;
    doc["id"]      = KIOSK_DEVICE_ID;
    doc["type"]    = "KIOSK_CMD";
    doc["mode"]    = "KIOSK";
    doc["version"] = FIRMWARE_VERSION;
    doc["ip"]      = WiFi.localIP().toString();
    doc["payload"]["cmd"] = cmd;

    String out;
    serializeJson(doc, out);
    _mqtt.publish(_telemetryTopic.c_str(), out.c_str());
}

void KioskNetwork::performHttpOTA(const String& url) {
    Serial.println("[OTA-HTTP] Avvio aggiornamento da: " + url);
    _hw.beepAsync(150);
    _hw.drawOtaProgress(0);

    WiFiClient client;
    httpUpdate.rebootOnUpdate(false);

    httpUpdate.onProgress([this](int cur, int total) {
        static int lastPct = -1;
        int pct = (total > 0) ? ((cur * 100) / total) : 0;
        if (pct != lastPct && (pct % 4 == 0 || pct == 100)) {
            lastPct = pct;
            _hw.drawOtaProgress((uint8_t)pct);
        }
    });

    t_httpUpdate_return ret = httpUpdate.update(client, url);
    if (ret == HTTP_UPDATE_OK) {
        Serial.println("[OTA-HTTP] Completato! Riavvio...");
        _hw.showSolidColorAll(0, 255, 0);
        delay(800);
        ESP.restart();
    } else {
        Serial.printf("[OTA-HTTP] Fallito (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        _hw.showSolidColorAll(255, 0, 0);
        delay(1500);
    }
}

void KioskNetwork::handleInternalDiagnostics(const JsonDocument& doc) {
    const char* cmd = doc["cmd"] | "";

    if (strcmp(cmd, "NET_PING_REQ") == 0) {
        uint32_t nowMs = millis();
        String payload = "{\"id\":\"" + String(KIOSK_DEVICE_ID) + "\",\"type\":\"NET_PING\",\"payload\":{";
        payload += "\"client_ts\":" + String(nowMs) + ",";
        payload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        payload += "\"heap_kb\":" + String(ESP.getFreeHeap() / 1024) + ",";
        payload += "\"ip\":\"" + WiFi.localIP().toString() + "\"}}";
        _mqtt.publish(_telemetryTopic.c_str(), payload.c_str());
    } else if (strcmp(cmd, "NET_PONG") == 0) {
        uint32_t sentTs = doc["client_ts"] | millis();
        int32_t rtt = (int32_t)(millis() - sentTs);
        if (rtt < 1) rtt = 1;
        _hw.beepAsync(40);
        String rep = "{\"id\":\"" + String(KIOSK_DEVICE_ID) + "\",\"type\":\"NET_DIAG_REPORT\",\"payload\":{";
        rep += "\"rtt_ms\":" + String(rtt) + ",";
        rep += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        rep += "\"heap_kb\":" + String(ESP.getFreeHeap() / 1024) + "}}";
        _mqtt.publish(_telemetryTopic.c_str(), rep.c_str());
    } else if (strcmp(cmd, "OTA_HTTP") == 0) {
        const char* url = doc["url"] | "";
        if (strlen(url) > 0) {
            performHttpOTA(String(url));
        }
    } else if (strcmp(cmd, "RESET") == 0) {
        Serial.println("[SYS] Comando RESET ricevuto dal server.");
        _hw.showSolidColorAll(255, 0, 0);
        delay(600);
        ESP.restart();
    }
}

void KioskNetwork::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (!_instance || length > 1023) return;

    char buf[1024];
    memcpy(buf, payload, length);
    buf[length] = '\0';

    JsonDocument doc;
    if (!deserializeJson(doc, buf)) {
        _instance->handleInternalDiagnostics(doc);
        if (_instance->_cmdCallback) {
            _instance->_cmdCallback(doc);
        }
    }
}

void KioskNetwork::loop() {
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();

        if (!_mqtt.connected()) {
            if (millis() - _lastReconnectAttemptMs >= 5000) {
                _lastReconnectAttemptMs = millis();
                reconnectMQTT();
            }
        } else {
            _mqtt.loop();
            if (millis() - _lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
                _lastHeartbeatMs = millis();
                sendHeartbeat();
            }
        }
    }
}