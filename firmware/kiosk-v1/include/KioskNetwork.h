/**
 * @file KioskNetwork.h
 * @brief Gestore di rete del Kiosk: Wi-Fi, MQTT, Diagnostica RTT/RSSI, ArduinoOTA e HTTP OTA.
 */

#ifndef KIOSK_NETWORK_H
#define KIOSK_NETWORK_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "KioskConfig.h"
#include "KioskHardware.h"

typedef void (*ServerCommandCallback)(const JsonDocument& doc);

class KioskNetwork {
public:
    KioskNetwork(KioskHardware& hw);

    void begin(ServerCommandCallback callback);
    void loop();

    void sendTagScanned(const char* uid);
    void sendTagAssign(const char* uid, const char* team);
    void sendKioskCommand(const char* cmd);
    void sendHeartbeat();
    void performHttpOTA(const String& url);

private:
    KioskHardware& _hw;
    WiFiClient _espClient;
    PubSubClient _mqtt;
    ServerCommandCallback _cmdCallback;

    String _telemetryTopic;
    String _cmdTopic;
    unsigned long _lastReconnectAttemptMs;
    unsigned long _lastHeartbeatMs;

    void setupOTA();
    void reconnectMQTT();
    void handleInternalDiagnostics(const JsonDocument& doc);
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static KioskNetwork* _instance;
};

#endif // KIOSK_NETWORK_H