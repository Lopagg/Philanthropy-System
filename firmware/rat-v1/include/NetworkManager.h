// include/NetworkManager.h

/**
 * @file NetworkManager.h
 * @brief Dichiarazione della classe NetworkManager per la gestione della connettività WiFi e della comunicazione MQTT.
 * @details Questa classe astrae tutta la logica di rete. Si occupa di connettere
 * l'ESP32 a una rete WiFi e fornisce metodi semplici per inviare e ricevere
 * messaggi di stato tramite il protocollo MQTT (Publish/Subscribe).
 */

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h> 
#include <ArduinoJson.h>
#include "HardwareManager.h"

/**
 * @class NetworkManager
 * @brief Gestisce la connessione WiFi e la comunicazione MQTT.
 * @details Un unico oggetto di questa classe viene creato in main.cpp per gestire
 * tutte le operazioni di rete, assicurando che ci sia un solo punto di
 * controllo per la comunicazione.
 */
class NetworkManager {
public:
    /**
     * @brief Costruttore. Inizializza i parametri di rete di base.
     */
    NetworkManager();
    
    /**
     * @brief Inizializza la connessione WiFi e il servizio MQTT.
     * @details Tenta di connettersi alla rete WiFi specificata. Se ha successo,
     * configura e avvia il client MQTT. Questa funzione
     * è bloccante finché la connessione WiFi non viene stabilita.
     * Va chiamata una sola volta nel setup().
     */
    void initialize(HardwareManager* hardware);
    
    /**
     * @brief Aggiorna lo stato del listener di rete.
     * @details Da chiamare ad ogni ciclo del loop() principale. Controlla se sono
     * arrivati nuovi pacchetti dal Broker MQTT e gestisce il mantenimento della connessione.
     */
    void update();
    
    // Metodo generico per inviare QUALSIASI evento in formato JSON
    void sendEvent(const String& eventType, const JsonDocument& data);
    
    // Metodo semplificato per eventi senza dati extra (solo tipo)
    void sendEvent(const String& eventType);
    
    // Verifica se siamo connessi
    bool isConnected();

    // Getter per i messaggi ricevuti (ora ritorna un oggetto JSON, non stringa grezza)
    // Nota: Per semplicità per ora restituiamo ancora String, ma predisponiamoci.
    String getReceivedMessage();

    // Nuovo helper per permettere alla callback esterna di passare i messaggi MQTT
    void setReceivedMessage(String msg);

private:
    // Oggetti per la gestione del protocollo MQTT e della connessione socket.
    WiFiClient _espClient;
    PubSubClient _mqttClient;
    
    String _lastMessage;
    HardwareManager* _hardware;

    // Funzione interna per gestire la connessione al Broker
    void reconnectMQTT(); 
};

#endif // NETWORK_MANAGER_H