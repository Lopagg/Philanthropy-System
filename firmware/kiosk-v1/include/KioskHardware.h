/**
 * @file KioskHardware.h
 * @brief Gestore non-bloccante dell'hardware del Kiosk (LED WS2812, RC522, Buzzer, Sirena e Pulsanti).
 */

#ifndef KIOSK_HARDWARE_H
#define KIOSK_HARDWARE_H

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_NeoPixel.h>
#include "KioskConfig.h"

class KioskHardware {
public:
    KioskHardware();
    void initialize();
    void update(KioskState state, bool allPlayersIn, bool gameRunning, bool gameReady);

    // --- Lettura Input (Pulsanti e RFID) ---
    bool pollStartPressed();
    bool pollEndPressed();
    bool isSirenButtonActive();
    bool isEndButtonHeld();
    bool pollAlphaPressed();
    bool pollBravoPressed();
    bool readCardUID(char* outUidBuffer);
    bool isRfidHealthy() const { return _rfidHealthy; }

    // --- Buzzer e Sirena (100% Asincroni) ---
    void beepAsync(uint16_t durationMs);
    void startBeepSequence(const int* durations, int length);
    void triggerSiren(unsigned long durationMs);
    bool isSirenActive() const { return _sirenActive; }

    // --- Controllo Animazioni Striscia LED e Colori Fazione ---
    void setFactionColors(uint8_t aR, uint8_t aG, uint8_t aB, uint8_t bR, uint8_t bG, uint8_t bB);
    void startSwipeAnimation(const String& team, const String& direction);
    void setWinnerState(const String& winner);
    String getWinnerState() const { return _currentWinner; }

    // --- Feedback Visivo Speciale (OTA / Reset) ---
    void drawOtaProgress(uint8_t percentage);
    void showSolidColorAll(uint8_t r, uint8_t g, uint8_t b);

private:
    Adafruit_NeoPixel _strip;
    MFRC522 _rfid;
    bool _rfidHealthy;
    unsigned long _lastRfidPollMs;
    unsigned long _lastRfidRetryMs;

    // Colori dinamici
    uint32_t _colorAlpha;
    uint32_t _colorBravo;
    uint32_t _colorNeutral;
    uint32_t _colorWaiting;
    uint32_t _colorIdleReady;
    uint32_t _colorIdleWait;
    uint32_t _colorOff;

    // Stato animazioni LED
    String _lastAssignmentTeam;
    String _currentWinner;
    String _animType;
    bool _isAnimating;
    unsigned long _animStartMs;
    unsigned long _lastLedFrameMs;

    // Stato Buzzer e Sirena
    bool _sirenActive;
    unsigned long _sirenEndMs;
    bool _buzzerActive;
    unsigned long _buzzerEndMs;

    struct BeepSequence {
        int durations[10];
        int currentIndex;
        int length;
        unsigned long nextActionMs;
        bool isOn;
    } _beepSeq;

    // Debounce pulsanti Alpha/Bravo
    int _alphaState, _lastAlphaReading;
    unsigned long _lastAlphaDebounceMs;
    int _bravoState, _lastBravoReading;
    unsigned long _lastBravoDebounceMs;

    void setMainStripColor(uint32_t color);
    void extractRGB(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b);
    void updateBuzzer();
    void updateSiren();
    void renderMainStrip(KioskState state, bool allPlayersIn, bool gameRunning, bool gameReady);
    void renderButtonLeds(bool allPlayersIn, bool gameRunning, bool gameReady);
    void checkRfidHealth();
};

#endif // KIOSK_HARDWARE_H