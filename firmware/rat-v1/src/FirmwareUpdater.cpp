// src/FirmwareUpdater.cpp

#include "FirmwareUpdater.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WiFiClientSecure.h>

// --- Funzione per il confronto intelligente delle versioni (SemVer) ---
int compareVersions(const String& v1, const String& v2) {
    int i = 0, j = 0;
    while (i < v1.length() || j < v2.length()) {
        int num1 = 0, num2 = 0;
        // Estrai il blocco numerico di v1 fino al punto
        while (i < v1.length() && v1[i] != '.') { 
            num1 = num1 * 10 + (v1[i] - '0'); 
            i++; 
        }
        // Estrai il blocco numerico di v2 fino al punto
        while (j < v2.length() && v2[j] != '.') { 
            num2 = num2 * 10 + (v2[j] - '0'); 
            j++; 
        }
        
        if (num1 > num2) return 1;
        if (num1 < num2) return -1;
        
        i++; j++;
    }
    return 0;
}
// ----------------------------------------------------------------------

FirmwareUpdater::FirmwareUpdater(HardwareManager* hardware) : _hardware(hardware) {}

void FirmwareUpdater::checkForUpdates() {
    _hardware->clearLcd();
    _hardware->printLcd(0, 1, "Controllo aggiorn...");
    
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    http.begin(client, _manifestUrl);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        http.end();
        _hardware->clearLcd();
        _hardware->printLcd(0, 1, "Errore Manifesto!");
        char errStr[20];
        sprintf(errStr, "Codice HTTP: %d", httpCode);
        _hardware->printLcd(0, 2, errStr);
        delay(4000);
        return;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) {
        _hardware->printLcd(0, 2, "Errore JSON!");
        delay(3000);
        return;
    }
    
    const char* serverVersion = doc["version"];
    Serial.printf("Versione corrente: %s, Versione server: %s\n", FIRMWARE_VERSION, serverVersion);

    // Usa la nuova funzione matematica invece di strcmp
    if (compareVersions(String(serverVersion), String(FIRMWARE_VERSION)) > 0) {
        _hardware->clearLcd();
        _hardware->printLcd(0, 1, "Nuova vers. trovata!");
        _hardware->printLcd(0, 2, "Download in corso...");
        
        const char* firmwareUrl = doc["url"];
        
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.begin(client, firmwareUrl);
        int firmwareHttpCode = http.GET();

        if (firmwareHttpCode != HTTP_CODE_OK) {
            http.end();
            _hardware->printLcd(0, 3, "Errore Download FW!");
            Serial.printf("Errore HTTP download: %d\n", firmwareHttpCode);
            delay(3000);
            return;
        }

        int contentLength = http.getSize();
        if (contentLength <= 0) {
            http.end();
            _hardware->printLcd(0, 3, "Errore: File vuoto!");
            delay(3000);
            return;
        }

        if (!Update.begin(contentLength)) {
            http.end();
            _hardware->clearLcd();
            _hardware->printLcd(0, 1, "ERRORE OTA!");
            _hardware->printLcd(0, 2, "Partizioni errate?");
            Serial.printf("Update.begin() fallito. Errore: %u\n", Update.getError());
            delay(5000);
            return;
        }
        
        WiFiClient* stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);

        if (written != contentLength) {
            http.end();
            Update.abort();
            _hardware->clearLcd();
            _hardware->printLcd(0, 1, "ERRORE SCRITTURA!");
            _hardware->printLcd(0, 2, "Download fallito.");
            Serial.printf("Scrittura fallita. Scritto %d di %d bytes\n", written, contentLength);
            delay(5000);
            return;
        }

        if (Update.end()) {
            if (Update.isFinished()) {
                _hardware->clearLcd();
                _hardware->printLcd(2, 1, "AGGIORNAMENTO OK!");
                _hardware->printLcd(4, 2, "Riavvio in corso...");
                Serial.println("Aggiornamento completato. Riavvio.");
                delay(2000);
                ESP.restart();
            }
        } else {
            unsigned int errCode = Update.getError();
            _hardware->clearLcd();
            _hardware->printLcd(0, 1, "ERRORE FINALE!");
            char errStr[20];
            sprintf(errStr, "Verifica fallita: #%u", errCode);
            _hardware->printLcd(0, 2, errStr);
            Serial.printf("Errore OTA durante Update.end(): %u\n", errCode);
            delay(5000);
        }
        
        http.end();

    } else {
        _hardware->printLcd(0, 2, "Nessun aggiornamento");
        delay(2000);
    }
}