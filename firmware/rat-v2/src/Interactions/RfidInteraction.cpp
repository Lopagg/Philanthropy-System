/**
 * @file RfidInteraction.cpp
 * @brief Implementazione della validazione tag NFC/RFID (PN532) e invio telemetria MQTT.
 */

#include "Interactions/RfidInteraction.h"
#include "NetworkManager.h"

extern HardwareManager hardware;
extern NetworkManager network;
extern const char* DEVICE_ID;

// ============================================================================
// COSTRUTTORE E METODI STATICI DI UTILITÀ / TELEMETRIA
// ============================================================================

RfidInteraction::RfidInteraction(const char* allowedUids, EventType cancelEvent)
    : _allowedUids(allowedUids ? String(allowedUids) : "*"),
      _cancelEvent(cancelEvent),
      _result(INTERACTION_RUNNING),
      _showingError(false),
      _errorTime(0) {
    _allowedUids.trim();
}

String RfidInteraction::normalizeUid(const String& raw) {
    String out = "";
    for (unsigned int i = 0; i < raw.length(); i++) {
        char c = raw[i];
        if (isalnum(c)) {
            out += (char)toupper(c);
        }
    }
    return out;
}

void RfidInteraction::sendRfidTelemetry(const String& uid, bool accepted, const char* context) {
    String cleanUid = normalizeUid(uid);
    String payload = "{\"id\":\"" + String(DEVICE_ID) + "\",\"type\":\"RFID_SCAN\",\"payload\":{";
    payload += "\"uid\":\"" + cleanUid + "\",";
    payload += "\"accepted\":" + String(accepted ? "true" : "false") + ",";
    payload += "\"context\":\"" + String(context ? context : "STEP") + "\"}}";
    network.sendTelemetry(payload);
}

// ============================================================================
// LOGICA DI VALIDAZIONE UID E CICLO DI VITA
// ============================================================================

bool RfidInteraction::isUidAllowed(const String& rawUid) const {
    String cleanUid = normalizeUid(rawUid);
    if (cleanUid.length() == 0) return false;

    // Wildcard "*" o stringa vuota: accetta qualsiasi tessera valida
    if (_allowedUids.length() == 0 || _allowedUids == "*") {
        return true;
    }

    // Sentinella inviata dal server quando la squadra o la lista giocatori è vuota
    if (_allowedUids.startsWith("__EMPTY")) {
        return false;
    }

    // Ricerca dell'UID nella lista separata da virgole (es. "04863EB76F6180,04112233445566")
    int start = 0;
    while (start < (int)_allowedUids.length()) {
        int comma = _allowedUids.indexOf(',', start);
        if (comma == -1) comma = _allowedUids.length();
        String token = normalizeUid(_allowedUids.substring(start, comma));
        if (token.length() > 0 && token == cleanUid) {
            return true;
        }
        start = comma + 1;
    }
    return false;
}

void RfidInteraction::begin() {
    _result = INTERACTION_RUNNING;
    _showingError = false;
    hardware.noTone();
    if (_cancelEvent == EV_BTN_1_PRESSED) {
        hardware.setOledLabels("ANNULLA", "");
    } else if (_cancelEvent == EV_BTN_2_PRESSED) {
        hardware.setOledLabels("", "ANNULLA");
    } else {
        hardware.setOledLabels("", "");
    }
    hardware.printLcdLine(1, "PASSA TESSERA RFID", true);
    hardware.printLcdLine(2, "", false);
}

void RfidInteraction::handleEvent(const SystemEvent& event) {
    if (_result != INTERACTION_RUNNING) return;

    if (_cancelEvent != EV_NONE && event.type == _cancelEvent) {
        _result = INTERACTION_CANCELLED;
        return;
    }

    if (event.type == EV_RFID_SCANNED) {
        String scanned = String(event.stringData);
        bool allowed = isUidAllowed(scanned);

        // Invia sempre al server l'UID scansionato e l'esito della verifica
        sendRfidTelemetry(scanned, allowed, "STEP");

        if (allowed) {
            hardware.playTone(1500, 120);
            _result = INTERACTION_COMPLETED;
        } else {
            if (_allowedUids == "__EMPTY_TEAM__") {
                hardware.printLcdLine(2, "SQUADRA VUOTA!", true);
            } else {
                hardware.printLcdLine(2, "TESSERA ERRATA!", true);
            }
            hardware.playTone(250, 400);
            hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
            _showingError = true;
            _errorTime = millis();
            _result = INTERACTION_FAILED;
        }
    }
}

void RfidInteraction::update() {
    if (_result != INTERACTION_RUNNING) return;

    if (_showingError && (millis() - _errorTime >= 1200)) {
        _showingError = false;
        hardware.printLcdLine(2, "", false);
    }
}

InteractionResult RfidInteraction::getResult() const {
    return _result;
}

uint8_t RfidInteraction::getProgress() const {
    return 100;
}