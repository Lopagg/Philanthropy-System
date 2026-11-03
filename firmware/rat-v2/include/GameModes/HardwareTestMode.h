/**
 * @file HardwareTestMode.h
 * @brief Modalità interattiva di diagnostica e collaudo hardware/rete per RAT-V2.
 *
 * Articolata in 5 pagine navigabili:
 * - Pagina 1 (INPUT & RFID) : Test pulsanti, chiavi K1/K2, tastierino 4x4 e lettore NFC PN532.
 * - Pagina 2 (DISPLAY & P2) : Test barra pixel-per-pixel LCD, OLED e verifica pulsante P2.
 * - Pagina 3 (AUDIO & LED)  : Test sweep I2S, riproduzione MP3 da Micro-SD e LED RGB WS2812.
 * - Pagina 4 (RTC, BAT & FX): Verifica orario RTC, tensione batteria e impulso 1s su Fumo/Relè 12V.
 * - Pagina 5 (RETE & PING)  : Potenza segnale Wi-Fi (RSSI), RAM Heap libera e test RTT Ping col server.
 */

#ifndef HARDWARE_TEST_MODE_H
#define HARDWARE_TEST_MODE_H

#include "GameMode.h"
#include "HardwareManager.h"
#include "NetworkManager.h"

class HardwareTestMode : public GameMode {
public:
    HardwareTestMode();
    ~HardwareTestMode() override;

    void begin() override;
    void handleEvent(const SystemEvent& event) override;
    void update() override;

    bool isExitRequested() const { return _exitRequested; }

private:
    uint8_t _page;              // Pagina attiva (0..4)
    bool _exitRequested;        // true quando l'operatore seleziona ESCI TEST

    char _lastKeyChar;          // Ultimo tasto premuto sulla matrice 4x4
    String _lastRfidUid;        // Ultimo UID letto dal PN532
    bool _lastK1, _lastK2;      // Stato precedente chiavi K1/K2
    bool _lastP1, _lastP2;      // Stato precedente pulsanti P1/P2
    uint32_t _p2PressCount;     // Contatore pressioni P2 nella pagina 2

    uint8_t _audioLedStep;      // Step corrente del test ciclico Audio/LED (Pagina 3)
    unsigned long _lastUiRefreshMs;

    bool _fxPulseActive;        // true durante l'impulso di test da 1s su Fumo + Relè
    unsigned long _fxPulseStartMs;

    // Stato Test di Rete (Pagina 5)
    bool _pingWaiting;
    unsigned long _pingSentMs;
    int32_t _lastPingRttMs;

    void renderPageHeader();
    void refreshPageContent(bool force);
    void runAudioLedCycleStep(uint8_t step);
    void sendNetworkPing();
    void stopAllTestOutputs();
};

#endif // HARDWARE_TEST_MODE_H