/**
 * @file HardwareTestMode.cpp
 * @brief Implementazione della modalità di collaudo hardware e diagnostica di rete per RAT-V2.
 */

#include "GameModes/HardwareTestMode.h"
#include <ArduinoJson.h>
#include <WiFi.h>

extern HardwareManager hardware;
extern NetworkManager network;
extern const char* DEVICE_ID;

HardwareTestMode::HardwareTestMode()
    : _page(0),
      _exitRequested(false),
      _lastKeyChar('-'),
      _lastRfidUid("In attesa..."),
      _lastK1(false),
      _lastK2(false),
      _lastP1(false),
      _lastP2(false),
      _p2PressCount(0),
      _audioLedStep(0),
      _lastUiRefreshMs(0),
      _fxPulseActive(false),
      _fxPulseStartMs(0),
      _pingWaiting(false),
      _pingSentMs(0),
      _lastPingRttMs(-1) {}

HardwareTestMode::~HardwareTestMode() {
    stopAllTestOutputs();
}

void HardwareTestMode::stopAllTestOutputs() {
    _fxPulseActive = false;
    hardware.noTone();
    hardware.setLedsColor(0, 0, 0);
    hardware.triggerSmoke(0);
    hardware.setRelay(false);
}

void HardwareTestMode::begin() {
    _page = 0;
    _exitRequested = false;
    _lastKeyChar = '-';
    _lastRfidUid = hardware.isNfcInitialized() ? "Passa tag..." : "PN532 ASSENTE";
    _p2PressCount = 0;
    _audioLedStep = 0;
    _pingWaiting = false;
    _lastPingRttMs = -1;

    stopAllTestOutputs();
    hardware.playTone(1200, 80);
    renderPageHeader();
    refreshPageContent(true);
}

void HardwareTestMode::renderPageHeader() {
    stopAllTestOutputs();
    hardware.clearLcd();

    switch (_page) {
        case 0:
            hardware.printLcdLine(0, "TEST 1/5: INPUT&RFID", true);
            hardware.setOledLabels("TEST P1", "PAG. 2/5");
            break;
        case 1:
            hardware.printLcdLine(0, "TEST 2/5: DISPLAY&P2", true);
            hardware.printLcdLine(1, "Barra LCD + Test P2", true);
            hardware.setOledLabels("PAG. 3/5", "TEST P2");
            break;
        case 2:
            hardware.printLcdLine(0, "TEST 3/5: AUDIO&LED", true);
            hardware.printLcdLine(1, "Stato: In attesa", true);
            hardware.printLcdLine(2, "1:Sweep 2:MP3 3:LED", true);
            hardware.printLcdLine(3, "P1:Cicla      0:Stop", true);
            hardware.setOledLabels("PROVA ORA", "PAG. 4/5");
            break;
        case 3:
            hardware.printLcdLine(0, "TEST 4/5: RTC & FX", true);
            hardware.setOledLabels("IMPULSO FX", "PAG. 5/5");
            break;
        case 4:
            hardware.printLcdLine(0, "TEST 5/5: RETE&PING", true);
            hardware.setOledLabels("PING SRV", "ESCI TEST");
            sendNetworkPing();
            break;
    }
}

void HardwareTestMode::sendNetworkPing() {
    _pingWaiting = true;
    _pingSentMs = millis();
    int32_t rssi = WiFi.RSSI();
    uint32_t freeHeap = ESP.getFreeHeap() / 1024;

    String payload = "{\"id\":\"" + String(DEVICE_ID) + "\",\"type\":\"NET_PING\",\"payload\":{";
    payload += "\"client_ts\":" + String(_pingSentMs) + ",";
    payload += "\"rssi\":" + String(rssi) + ",";
    payload += "\"heap_kb\":" + String(freeHeap) + ",";
    payload += "\"ip\":\"" + WiFi.localIP().toString() + "\"}}";
    network.sendTelemetry(payload);

    if (_page == 4) {
        hardware.printLcdLine(3, "PING: Invio...", false);
    }
}

void HardwareTestMode::runAudioLedCycleStep(uint8_t step) {
    switch (step % 4) {
        case 0:
            hardware.printLcdLine(1, "Sweep + LED ROSSO", true);
            hardware.setLedsColor(255, 0, 0);
            hardware.startToneSweep(400, 1400, 1500);
            break;
        case 1:
            hardware.printLcdLine(1, "MP3 SD + LED VERDE", true);
            hardware.setLedsColor(0, 255, 0);
            hardware.playAudio("/audio/common/match_start.mp3");
            break;
        case 2:
            hardware.printLcdLine(1, "Beep + LED BLU", true);
            hardware.setLedsColor(0, 0, 255);
            hardware.playTone(1000, 400);
            break;
        case 3:
            hardware.printLcdLine(1, "Stato: Tutto OFF", true);
            stopAllTestOutputs();
            break;
    }
}

void HardwareTestMode::handleEvent(const SystemEvent& event) {
    // Risposta PONG dal server per il calcolo dell'RTT
    if (event.type == EV_SERVER_COMMAND) {
        JsonDocument doc;
        if (!deserializeJson(doc, event.stringData)) {
            const char* cmd = doc["cmd"];
            if (cmd && strcmp(cmd, "NET_PONG") == 0) {
                uint32_t sentTs = doc["client_ts"] | _pingSentMs;
                _lastPingRttMs = (int32_t)(millis() - sentTs);
                if (_lastPingRttMs < 1) _lastPingRttMs = 1;
                _pingWaiting = false;
                hardware.playTone(1800, 60);
                if (_page == 4) refreshPageContent(true);

                // Invia al server il risultato RTT calcolato per aggiornarlo nell'Inspector
                String rep = "{\"id\":\"" + String(DEVICE_ID) + "\",\"type\":\"NET_DIAG_REPORT\",\"payload\":{";
                rep += "\"rtt_ms\":" + String(_lastPingRttMs) + ",";
                rep += "\"rssi\":" + String(WiFi.RSSI()) + ",";
                rep += "\"heap_kb\":" + String(ESP.getFreeHeap() / 1024) + "}}";
                network.sendTelemetry(rep);
                return;
            }
        }
    }

    if (_page == 0) {
        if (event.type == EV_BTN_1_PRESSED) {
            hardware.playTone(1200, 80);
            refreshPageContent(true);
        } else if (event.type == EV_BTN_2_PRESSED) {
            hardware.playTone(900, 50);
            _page = 1;
            renderPageHeader();
            refreshPageContent(true);
            return;
        }
    } else if (_page == 1) {
        if (event.type == EV_BTN_1_PRESSED) {
            hardware.playTone(900, 50);
            _page = 2;
            renderPageHeader();
            refreshPageContent(true);
            return;
        } else if (event.type == EV_BTN_2_PRESSED) {
            _p2PressCount++;
            hardware.playTone(1500, 80);
            refreshPageContent(true);
        }
    } else if (_page == 2) {
        if (event.type == EV_BTN_1_PRESSED) {
            runAudioLedCycleStep(_audioLedStep++);
            return;
        } else if (event.type == EV_BTN_2_PRESSED) {
            hardware.playTone(900, 50);
            _page = 3;
            renderPageHeader();
            refreshPageContent(true);
            return;
        } else if (event.type == EV_KEYPAD_PRESSED) {
            if (event.charValue == '1') runAudioLedCycleStep(0);
            else if (event.charValue == '2') runAudioLedCycleStep(1);
            else if (event.charValue == '3') runAudioLedCycleStep(2);
            else if (event.charValue == '0') runAudioLedCycleStep(3);
        }
    } else if (_page == 3) {
        if (event.type == EV_BTN_1_PRESSED) {
            _fxPulseActive = true;
            _fxPulseStartMs = millis();
            hardware.triggerSmoke(255);
            hardware.setRelay(true);
            hardware.setLedsColor(255, 150, 0);
            hardware.playTone(800, 150);
            refreshPageContent(true);
            return;
        } else if (event.type == EV_BTN_2_PRESSED) {
            hardware.playTone(900, 50);
            _page = 4;
            renderPageHeader();
            refreshPageContent(true);
            return;
        }
    } else if (_page == 4) {
        if (event.type == EV_BTN_1_PRESSED) {
            hardware.playTone(1000, 40);
            sendNetworkPing();
            return;
        } else if (event.type == EV_BTN_2_PRESSED) {
            hardware.playTone(600, 100);
            stopAllTestOutputs();
            _exitRequested = true;
            return;
        }
    }

    if (event.type == EV_KEYPAD_PRESSED) {
        _lastKeyChar = event.charValue;
        hardware.playTone(700, 35);
        if (_page == 0) refreshPageContent(true);
    } else if (event.type == EV_KEY_1_STATE_CHANGED || event.type == EV_KEY_2_STATE_CHANGED) {
        hardware.playTone(event.boolValue ? 1300 : 500, 60);
        if (_page == 0) refreshPageContent(true);
    } else if (event.type == EV_RFID_SCANNED) {
        _lastRfidUid = String(event.stringData);
        hardware.playTone(1600, 120);
        if (_page == 0) refreshPageContent(true);
    }
}

void HardwareTestMode::refreshPageContent(bool force) {
    if (_page == 0) {
        bool k1 = hardware.isKey1Inserted();
        bool k2 = hardware.isKey2Inserted();
        bool p1 = hardware.isButton1Pressed();
        bool p2 = hardware.isButton2Pressed();

        if (!force && k1 == _lastK1 && k2 == _lastK2 && p1 == _lastP1 && p2 == _lastP2) {
            return;
        }
        _lastK1 = k1; _lastK2 = k2; _lastP1 = p1; _lastP2 = p2;

        char line1[21];
        snprintf(line1, sizeof(line1), "K1:%-3s K2:%-3s Key:%c",
                 k1 ? "ON" : "OFF", k2 ? "ON" : "OFF", _lastKeyChar);
        hardware.printLcdLine(1, String(line1), false);

        char line2[21];
        snprintf(line2, sizeof(line2), "P1:%-3s P2:%-3s SD:%s",
                 p1 ? "ON" : "OFF", p2 ? "ON" : "OFF", hardware.isSdInitialized() ? "OK" : "NO");
        hardware.printLcdLine(2, String(line2), false);

        hardware.printLcdLine(3, "NFC:" + _lastRfidUid.substring(0, 16), false);
    } else if (_page == 1) {
        uint32_t anim = (millis() % 2000);
        hardware.drawProgressBar(2, anim, 2000);
        if (force) {
            String p2Line = "P2 Premuto: " + String(_p2PressCount) + " volte";
            hardware.printLcdLine(3, p2Line, true);
        }
    } else if (_page == 3) {
        DateTime now = hardware.getRTCTime();
        char line1[21];
        snprintf(line1, sizeof(line1), "ORA: %02d:%02d:%02d (%s)",
                 now.hour(), now.minute(), now.second(),
                 hardware.isRtcInitialized() ? "RTC" : "LOC");
        hardware.printLcdLine(1, String(line1), false);

        float battV = hardware.readBatteryVoltage();
        char line2[21];
        if (battV > 1.0f) {
            snprintf(line2, sizeof(line2), "BAT: %.2fV (%d%%)", battV, hardware.readBatteryPercentage());
        } else {
            snprintf(line2, sizeof(line2), "BAT: Non collegata");
        }
        hardware.printLcdLine(2, String(line2), false);

        hardware.printLcdLine(3, _fxPulseActive ? "FX Fumo+Rele: ON!" : "FX Fumo+Rele: OFF", true);
    } else if (_page == 4) {
        int32_t rssi = WiFi.RSSI();
        int quality = constrain(2 * (rssi + 100), 0, 100);
        char line1[21];
        snprintf(line1, sizeof(line1), "RSSI:%lddBm (%d%%)", (long)rssi, quality);
        hardware.printLcdLine(1, String(line1), false);

        char line2[21];
        snprintf(line2, sizeof(line2), "RAM Libera: %u KB", (uint32_t)(ESP.getFreeHeap() / 1024));
        hardware.printLcdLine(2, String(line2), false);

        char line3[21];
        if (_pingWaiting) {
            snprintf(line3, sizeof(line3), "PING: Attesa pong..");
        } else if (_lastPingRttMs >= 0) {
            snprintf(line3, sizeof(line3), "PING RTT: %ld ms OK", (long)_lastPingRttMs);
        } else {
            snprintf(line3, sizeof(line3), "Premi P1 per Ping");
        }
        hardware.printLcdLine(3, String(line3), false);
    }
}

void HardwareTestMode::update() {
    if (_fxPulseActive && (millis() - _fxPulseStartMs >= 1000)) {
        stopAllTestOutputs();
        if (_page == 3) refreshPageContent(true);
    }

    if (_pingWaiting && (millis() - _pingSentMs >= 3000)) {
        _pingWaiting = false;
        _lastPingRttMs = -1;
        hardware.playTone(250, 300);
        if (_page == 4) {
            hardware.printLcdLine(3, "PING: TIMEOUT!", false);
        }
    }

    if (millis() - _lastUiRefreshMs >= 250) {
        _lastUiRefreshMs = millis();
        refreshPageContent(false);
    }
}