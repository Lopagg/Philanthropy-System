// src/NetworkManager.cpp

#include "NetworkManager.h"
#include "app_common.h"
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <esp_wifi.h>
#include <TelnetStream.h>
#include <WiFiManager.h>

String deviceId = "";   // Variabile globale per il MAC Address
extern NetworkManager networkManager; // Referenza all'istanza creata in main.cpp per la callback

// Il tuo server DDNS
const char* SERVER_HOSTNAME = "zuluserver.ddns.net";
const int MQTT_PORT = 1883;

// --- CALLBACK MQTT GLOBALE ---
// Viene richiamata in automatico ogni volta che arriva un comando dal server sul topic ascoltato
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (length > 511) return; // Prevenzione Buffer Overflow
    
    char incomingBuffer[512];
    memcpy(incomingBuffer, payload, length);
    incomingBuffer[length] = '\0';
    
    // Passa il messaggio alla nostra classe NetworkManager
    networkManager.setReceivedMessage(String(incomingBuffer));
}

// Costruttore
NetworkManager::NetworkManager() : _hardware(nullptr) {
    _mqttClient.setClient(_espClient);
}

void NetworkManager::initialize(HardwareManager* hardware) {
    // Salviamo il riferimento all'hardware per usarlo anche in update() (es. per il Reset)
    _hardware = hardware;

    Serial.println("--- Inizializzazione Rete (MQTT) ---");
    Serial.print("Firmware Version: ");
    Serial.println(FIRMWARE_VERSION);

    hardware->clearLcd();
    hardware->printLcd(0, 0, "Connessione WiFi...");

    // 1. Accendi l'antenna in modalità Station
    WiFi.mode(WIFI_STA);
    WiFi.begin();

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(250);
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        hardware->clearLcd();
        hardware->printLcd(0, 0, "Rete non trovata!");
        hardware->printLcd(0, 1, "P1: Gioca Offline");
        hardware->printLcd(0, 2, "P2: Configura Rete");

        hardware->clearOled1();
        hardware->printOled1("OFFLINE", 2, 22, 25);
        hardware->clearOled2();
        hardware->printOled2("CONFIG", 2, 28, 25);

        bool choice = false;
        bool startAP = false;
        while (!choice) {
            hardware->updateButtons();
            if (hardware->wasButton1Pressed()) {
                startAP = false;
                choice = true;
                hardware->playTone(300, 70);
            }
            if (hardware->wasButton2Pressed()) {
                startAP = true;
                choice = true;
                hardware->playTone(1200, 100);
            }
            delay(50);
        }

        hardware->clearOled1();
        hardware->clearOled2();

        if (startAP) {
            hardware->clearLcd();
            hardware->printLcd(0, 0, "Avvio Access Point");
            hardware->printLcd(0, 1, "Rete: RAT-V1");
            hardware->printLcd(0, 2, "IP: 192.168.4.1");
            
            WiFiManager wm;
            wm.setConfigPortalTimeout(180); 

            if (!wm.startConfigPortal("RAT-V1")) {
                hardware->clearLcd();
                hardware->printLcd(0, 0, "Timeout Setup!");
                hardware->printLcd(0, 1, "Avvio offline...");
                delay(3000);
            }
        }
    }

    deviceId = WiFi.macAddress();

    if (WiFi.status() == WL_CONNECTED) {
        TelnetStream.begin();
        TelnetStream.println("\n\n=== LOG DI RETE ATTIVATI ===");
        TelnetStream.printf("Dispositivo connesso: %s\n", WiFi.localIP().toString().c_str());

        // --- STAMPA IP VISIBILE ---
        hardware->clearLcd();
        hardware->printLcd(0, 0, "WiFi Connesso!");
        
        String ssid = WiFi.SSID();
        if (ssid.length() > 20) ssid = ssid.substring(0, 20);
        hardware->printLcd(0, 1, ssid);
        
        hardware->printLcd(0, 2, "IP: " + WiFi.localIP().toString());
        Serial.printf("\nConnesso! IP: %s, MAC: %s\n", WiFi.localIP().toString().c_str(), deviceId.c_str());
        
        delay(2000);

        // --- SEZIONE NTP ---
        hardware->clearLcd();
        hardware->printLcd(0, 0, "WiFi Connesso!");
        hardware->printLcd(0, 1, "Sync Orario...");
        configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
        hardware->syncWithNTP();
        
        // --- CONFIGURAZIONE MQTT ---
        // Impostiamo un timeout molto basso (2 secondi) per evitare freeze prolungati
        _mqttClient.setServer(SERVER_HOSTNAME, MQTT_PORT);
        _mqttClient.setCallback(mqttCallback);
        _mqttClient.setBufferSize(512);

        // --- AVVIO OTA SOLO SE CONNESSO ---
        ArduinoOTA.setHostname("RAT-V1");
        ArduinoOTA.begin();
    } else {
        hardware->clearLcd();
        hardware->printLcd(0, 0, "Modalita' Offline");
        hardware->printLcd(0, 1, "Rete disabilitata");
        delay(2000);
    }
}

void NetworkManager::reconnectMQTT() {
    Serial.print("Tentativo di connessione MQTT...");
    
    // Creazione dell'LWT (Last Will and Testament)
    char willTopic[64];
    snprintf(willTopic, sizeof(willTopic), "zulu/telemetry/%s", deviceId.c_str());
    
    char willPayload[128];
    snprintf(willPayload, sizeof(willPayload), "{\"id\":\"%s\", \"type\":\"MODE_EXIT\", \"payload\":{\"mode\":\"OFFLINE\"}}", deviceId.c_str());
    
    if (_mqttClient.connect(deviceId.c_str(), "admin", "admin", willTopic, 1, true, willPayload)) {
        Serial.println("Connesso al Broker!");
        
        // Si iscrive al topic generico e a quello specifico per questo nodo
        _mqttClient.subscribe("zulu/cmd/broadcast");
        
        char myTopic[64];
        snprintf(myTopic, sizeof(myTopic), "zulu/cmd/%s", deviceId.c_str());
        _mqttClient.subscribe(myTopic);
        
        // Invia evento di BOOT
        JsonDocument bootDoc;
        bootDoc["mode"] = "MAIN MENU"; 
        bootDoc["version"] = FIRMWARE_VERSION;
        sendEvent("DEVICE_ONLINE", bootDoc);
        
    } else {
        Serial.print("Fallito, rc=");
        Serial.print(_mqttClient.state());
        Serial.println(" riprova al prossimo giro.");
    }
}

void NetworkManager::update() {
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();

        // Mantiene viva la connessione MQTT in modo NON BLOCCANTE
        if (!_mqttClient.connected()) {
            static unsigned long lastReconnectAttempt = 0;
            // Tenta la riconnessione SOLO una volta ogni 5 secondi
            if (millis() - lastReconnectAttempt > 5000) {
                lastReconnectAttempt = millis();
                reconnectMQTT();
            }
        } else {
            _mqttClient.loop(); // Gestisce la ricezione dei pacchetti se connesso
        }
    }

    if (_lastMessage != "") {
        Serial.printf("RX MQTT: %s\n", _lastMessage.c_str());

        // --- INTERCETTAZIONE COMANDI GLOBALI (SYSTEM LEVEL) ---
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, _lastMessage);

        if (!error) {
            const char* cmd = doc["cmd"];
            if (cmd && strcmp(cmd, "RESET") == 0) {
                Serial.println("!!! GLOBAL SYSTEM RESET RECEIVED !!!");
                
                if (_hardware) {
                    _hardware->clearLcd();
                    _hardware->printLcd(0, 0, "SYSTEM RESET");
                    _hardware->printLcd(0, 1, "Riavvio...");
                }
                delay(1000); 
                ESP.restart();
            }
        }
    }
}

void NetworkManager::sendEvent(const String& eventType, const JsonDocument& data) {
    if (!isConnected() || !_mqttClient.connected()) {
        return; 
    }

    JsonDocument doc;
    doc["id"] = deviceId;
    doc["type"] = eventType;
    doc["payload"] = data;

    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);

    char topicBuffer[64];
    snprintf(topicBuffer, sizeof(topicBuffer), "zulu/telemetry/%s", deviceId.c_str());

    _mqttClient.publish(topicBuffer, jsonBuffer);
}

void NetworkManager::sendEvent(const String& eventType) {
    JsonDocument emptyDoc;
    sendEvent(eventType, emptyDoc);
}

void NetworkManager::setReceivedMessage(String msg) {
    _lastMessage = msg;
}

String NetworkManager::getReceivedMessage() {
    if (_lastMessage != "") {
        String msg = _lastMessage;
        _lastMessage = ""; 
        return msg;
    }
    return "";
}

bool NetworkManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}