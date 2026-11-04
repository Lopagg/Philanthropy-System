/**
 * @file HardwareManager.h
 * @brief Gestore centralizzato e thread-safe dell'hardware del RAT-V2 (ESP32-S3).
 *
 * Astrae l'accesso a:
 * - Bus I2C (protetto da mutex): LCD 20x4, Dual OLED 128x64, MCP23017, RTC DS3231, RFID PN532.
 * - Bus SPI: Scheda Micro-SD per asset audio MP3.
 * - Bus I2S: Amplificatore digitale MAX98357A (riproduzione MP3 + sintetizzatore DDS).
 * - Effetti e Sensori: Striscia LED WS2812, MOSFET macchina del fumo, Relè 12V, Batteria LiPo 2S.
 *
 * Tutte le periferiche vengono verificate all'avvio: se un modulo è scollegato,
 * viene disabilitato senza bloccare l'esecuzione del sistema.
 */

#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_SSD1306.h>
#include "RTClib.h"
#include <Adafruit_MCP23X17.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <SD.h>
#include <Audio.h>
#include <time.h>

// ============================================================================
// VERSIONE FIRMWARE E FUSO ORARIO
// ============================================================================

#define FIRMWARE_VERSION "v0.0.3"
#define TIMEZONE_ITALY   "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1     "pool.ntp.org"
#define NTP_SERVER_2     "time.nist.gov"

// ============================================================================
// MAPPATURA PIN HARDWARE (SCHEDA MADRE RAT-V2 ESP32-S3)
// ============================================================================

// --- Alimentazione e Calibrazione Batteria LiPo 2S ---
#define PIN_BATT_SENSE        1      // GPIO 1: Partitore di tensione batteria
#define BATT_DIVIDER_RATIO    3.0f   // Rapporto partitore: (R_high + R_low) / R_low
#define BATT_CALIBRATION_TRIM 1.000f // Fattore di taratura fine (V_multimetro / V_letta)

// --- Bus I2C Principale (Sensori, Display, Expander) ---
#define PIN_I2C_SDA    8
#define PIN_I2C_SCL    9
#define PIN_MCP_INT    5   // Interrupt da MCP23017 (INTA)

// --- Bus SPI (Lettore Micro-SD) ---
#define PIN_SPI_MOSI   16
#define PIN_SPI_MISO   17
#define PIN_SPI_SCK    18
#define PIN_SPI_CS     15

// --- Bus I2S Audio Out (Amplificatore MAX98357A) ---
#define PIN_I2S_BCLK   10
#define PIN_I2S_LRC    11
#define PIN_I2S_DOUT   12
#define PIN_AUDIO_EN   13  // Pin SD/EN dell'amplificatore (HIGH = Attivo, LOW = Mute/Standby)

// --- Effetti Fisici ed Espansioni ---
#define PIN_LEDS       4   // Striscia LED WS2812 (tramite level shifter 5V)
#define NUM_LEDS       60
#define PIN_SMOKE      39  // Controllo PWM MOSFET macchina del fumo (AOD4184A)
#define PIN_EXP_CTRL_1 42  // Uscita controllo relè esterno 12V / carichi ausiliari

// --- Indirizzi Periferiche I2C ---
#define I2C_ADDR_MCP    0x20
#define I2C_ADDR_LCD    0x27
#define I2C_ADDR_OLED_1 0x3C // OLED Pulsante P1
#define I2C_ADDR_OLED_2 0x3D // OLED Pulsante P2
#define I2C_ADDR_RTC    0x68 // DS3231MZ
#define I2C_ADDR_PN532  0x24 // PN532 in modalità I2C

// --- Mappatura Pin Interni MCP23017 (Port B) ---
#define MCP_PIN_P1     8   // GPB0: Pulsante Principale 1
#define MCP_PIN_P2     9   // GPB1: Pulsante Principale 2
#define MCP_PIN_K1     10  // GPB2: Selettore a Chiave 1
#define MCP_PIN_K2     11  // GPB3: Selettore a Chiave 2

class HardwareManager {
public:
    HardwareManager();
    void initialize();

    // --- Gestione Input Fisici (MCP23017: Pulsanti, Chiavi, Tastierino 4x4) ---
    void updateInputs();
    bool isButton1Pressed();
    bool wasButton1Pressed();
    bool isButton2Pressed();
    bool wasButton2Pressed();
    bool isKey1Inserted();
    bool isKey2Inserted();
    char getKeyPressed();

    // --- Gestione Display LCD 20x4 ---
    void clearLcd();
    void printLcd(int col, int row, const String& text);
    void printLcdLine(int row, const String& text, bool centered = false);
    void writeCustomChar(uint8_t charIndex);
    void drawProgressBar(int row, unsigned long progress, unsigned long totalDuration);

    // --- Gestione Dual OLED 128x64 (Etichette Dinamiche Pulsanti P1 e P2) ---
    void clearOled();
    void printOled(const String& text, int size = 1, int x = 0, int y = 0);
    void clearOled1();
    void printOled1(const String& text, int size = 2, int x = -1, int y = -1);
    void clearOled2();
    void printOled2(const String& text, int size = 2, int x = -1, int y = -1);
    void setOledLabels(const String& p1Text, const String& p2Text);

    // --- Gestione RFID (PN532), RTC (DS3231), NTP e Diagnostica ---
    String readRFID(uint16_t timeout = 50);
    bool syncRTCWithNTP(uint32_t timeoutMs = 4000);
    DateTime getRTCTime();
    float readBatteryVoltage();
    int8_t readBatteryPercentage();
    void printI2CProfilerStats();
    void listSdFiles(const char* dirname, uint8_t levels = 2);

    bool isRtcInitialized() const  { return _rtcInitialized; }
    bool isSdInitialized() const   { return _sdInitialized; }
    bool isNfcInitialized() const  { return _nfcInitialized; }
    bool isMcpInitialized() const  { return _mcpInitialized; }

    // --- Gestione Audio I2S (MP3 da SD + Sintetizzatore DDS) ---
    void playAudio(const String& filename);
    void startToneSweep(uint16_t startFreq, uint16_t endFreq, uint32_t durationMs);
    void playTone(uint16_t freq, uint32_t durationMs);
    void noTone();
    void audioLoop();
    void setVolume(uint8_t vol);
    bool isAudioPlaying();

    // --- Controllo LED ed Effetti Esterni (Non bloccanti se scollegati) ---
    void setLedsColor(uint8_t r, uint8_t g, uint8_t b);
    void triggerSmoke(uint8_t pwmValue);
    void setRelay(bool state);

private:
    LiquidCrystal_I2C _lcd;
    Adafruit_SSD1306 _oled1;
    Adafruit_SSD1306 _oled2;
    RTC_DS3231 _rtc;
    Adafruit_MCP23X17 _mcp;
    PN532_I2C* _nfc_i2c;
    PN532* _nfc;
    Audio _audio;
    Adafruit_NeoPixel _leds;

    SemaphoreHandle_t _i2cMutex;
    SemaphoreHandle_t _audioMutex;

    bool _lcdInitialized;
    bool _oled1Initialized;
    bool _oled2Initialized;
    bool _rtcInitialized;
    bool _mcpInitialized;
    bool _nfcInitialized;
    bool _sdInitialized;

    uint8_t _colPins[4];
    uint8_t _rowPins[4];
    char _keymap[4][4];
    char _lastRawKeyPressed;
    char _debouncedKeyPressed;
    char _keyPressed;
    uint16_t _lastMcpState;
    unsigned long _lastDebounceTime;
    bool _p1State, _p1WasPressed;
    bool _p2State, _p2WasPressed;
    bool _k1State, _k2State;

    uint32_t maxMutexWaitTime;
    uint32_t maxI2cReadTime;
    uint32_t missedReads;

    int16_t _lastLedR, _lastLedG, _lastLedB;
    int _lastBarPixels;
    String _lastOled1Text;
    String _lastOled2Text;

    volatile bool _toneActive;
    volatile bool _toneIsSweep;
    volatile uint16_t _toneStartFreq;
    volatile uint16_t _toneEndFreq;
    volatile uint32_t _toneTotalSamples;
    volatile uint32_t _toneSampleIndex;
    volatile uint32_t _toneTailSamples;
    uint32_t _tonePhase;
    char _pendingMp3Path[64];
    unsigned long _ampTurnOffTimeMs;
    bool _waitingAmpTurnOff;

    float _filteredBattMv;

    bool probeI2CDevice(uint8_t address);
    bool isValidAudioFile(const String& path);
    void ensureMp3Padding(const String& path);
    void startToneInternal(uint16_t startFreq, uint16_t endFreq, uint32_t durationMs, bool isSweep);
    void drawOledText(Adafruit_SSD1306& display, bool isInitialized, const String& text, int size, int x, int y);
};

#endif // HARDWARE_MANAGER_H