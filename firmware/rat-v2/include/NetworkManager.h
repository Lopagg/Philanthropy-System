/**
 * @file NetworkManager.h
 * @brief Gestore della connettività Wi-Fi (WiFiManager), aggiornamenti OTA (espota)
 *        e comunicazione bidirezionale MQTT con il server centrale Zulu.
 */

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "events.h"

class NetworkManager {
public:
    NetworkManager();
    
    void begin(const char* deviceId, const char* mqttServer, uint16_t mqttPort, QueueHandle_t mainQueue);
    void loop();
    void sendTelemetry(const String& payload);
    void setCurrentModeName(const char* modeName);

private:
    WiFiClient _espClient;
    PubSubClient _mqtt;
    String _deviceId;
    String _telemetryTopic;
    String _cmdTopic;
    String _currentModeName;
    QueueHandle_t _eventQueue;
    
    unsigned long _lastReconnectAttempt;
    unsigned long _lastHeartbeat;

    void setupOTA();
    void reconnectMQTT();
    void publishHeartbeat();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static NetworkManager* _instance; 
};

#endif // NETWORK_MANAGER_H