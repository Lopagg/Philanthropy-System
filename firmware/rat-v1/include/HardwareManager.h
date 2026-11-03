// include/HardwareManager.h

/**
 * @file HardwareManager.h
 * @brief Dichiarazione della classe HardwareManager, il punto di controllo centrale per tutto l'hardware.
 * @details Questa classe astrae la complessità dei singoli componenti hardware,
 * fornendo un'interfaccia semplice e unificata per il resto del programma.
 * Gestisce display, pulsanti, LED, suoni e l'orologio di sistema.
 */

#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

// Inclusione delle librerie necessarie per i componenti hardware
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "Button.h" // Classe definita nel file per la logica debouncing pulsanti
#include <Adafruit_NeoPixel.h> // Striscia LED
#include "RTClib.h"
#include <Adafruit_SSD1306.h> // Schermi OLED
#include <PN532_I2C.h>
#include <PN532.h>

/**
 * @class HardwareManager
 * @brief Gestisce tutte le interazioni con i componenti hardware fisici.
 * @details Un unico oggetto di questa classe viene creato nel main.cpp per garantire
 * che ci sia un solo "padrone" dell'hardware, evitando conflitti.
 */
class HardwareManager {
public:
    /**
     * @brief Costruttore. Prepara gli oggetti dei componenti con i loro parametri base.
     */
    HardwareManager();
    /**
     * @brief Inizializza tutto l'hardware. Chiamata una sola volta nel setup().
     */
    void initialize();

    // --- Funzioni di Output (Display, LED, Suoni) ---

    // Funzioni LCD
    /** @brief Stampa del testo sull'LCD a coordinate specifiche. */
    void printLcd(int col, int row, const String& text);
    /** @brief Pulisce completamente lo schermo LCD. */
    void clearLcd();
    /** @brief Ritorna il numero di righe dell'LCD */
    int getLcdRows();
    /** @brief Ritorna il numero di colonne dell'LCD */
    int getLcdCols();
    /** @brief Crea i caratteri personalizzati per la barra di avanzamento sull'LCD. */
    void createProgressBarChars();
    /** @brief Stampa uno dei caratteri personalizzati sull'LCD. */
    void writeCustomChar(uint8_t charIndex);

    // Funzioni schermi OLED
    /** @brief Pulisce l'OLED 1. */
    void clearOled1();
    /** @brief Stampa testo sull'OLED 1 (associato al pulsante 1). */
    void printOled1(const String& text, int size = 1, int x = 0, int y = 0);
    /** @brief Pulisce l'OLED 2. */
    void clearOled2();
    /** @brief Stampa testo sull'OLED 2 (associato al pulsante 2). */
    void printOled2(const String& text, int size = 1, int x = 0, int y = 0);

    // Funzioni Tastiera e Pulsanti
    /** @brief Legge un carattere dal tastierino numerico. */
    char getKey();
    /** @brief Aggiorna lo stato di tutti i pulsanti. Da chiamare nel loop(). */
    void updateButtons();
    /** @brief Controlla se il pulsante 1 è stato appena premuto (evento singolo). */
    bool wasButton1Pressed();
    /** @brief Controlla se il pulsante 2 è stato appena premuto (evento singolo). */
    bool wasButton2Pressed();
    /** @brief Controlla se il pulsante 1 è attualmente tenuto premuto (stato continuo). */
    bool isButton1Pressed();
    /** @brief Controlla se il pulsante 2 è attualmente tenuto premuto (stato continuo). */
    bool isButton2Pressed();
    /** @brief Controlla se la chiave 1 è inserita e girata (stato continuo). */
    bool isKey1Turned();
    /** @brief Controlla se la chiave 2 è inserita e girata (stato continuo). */
    bool isKey2Turned();

    // Funzioni Striscia LED
    /** @brief Imposta tutti i LED della striscia su un colore uniforme. */
    void setStripColor(uint8_t r, uint8_t g, uint8_t b);
    /** @brief Imposta il colore di un singolo LED della striscia. */
    void setPixelColor(uint16_t pixel, uint8_t r, uint8_t g, uint8_t b); 
    /** @brief Aggiorna la striscia LED con i colori impostati nel buffer. */
    void showStrip();
    /** @brief Spegne la striscia a LED. */
    void turnOffStrip();
    /** @brief Aggiorna l'animazione "arcobaleno". Usata nei menu. */
    void updateRainbowEffect();
    /** @brief Aggiorna l'animazione "respirante". Usata durante il gioco. */
    void updateBreathingEffect(uint8_t r, uint8_t g, uint8_t b);
    /** @brief Cambia la luminosità globale della striscia a LED. */
    void setBrightness(uint8_t brightness);
    /** @brief Ritorna il numero di LED nella striscia. */
    int getStripLedCount();
    /** @brief Esegue un flash a massima luminosità del colore attuale. Usato per eventi di gioco. */
    void flashCurrentColor(int count, int duration);
    /** @brief Esegue un'animazione "a onda". Usata a fine partita. */
    void updateWinnerWaveEffect(uint8_t r, uint8_t g, uint8_t b, float base_brightness, float peak_brightness, int wave_width);

    // Funzione RTC
    /** @brief Ritorna l'ora corrente dall'RTC. */
    DateTime getRTCTime();
    void syncWithNTP();

    // Funzioni Buzzer
    /** @brief Riproduce un suono semplice per una data durata. Usato per i feedback dei menu. */
    void playTone(unsigned int frequency, unsigned long duration);
    void playToneAsync(unsigned int frequency, unsigned long duration);
    void updateAsyncBuzzer();
    /** @brief Ferma qualsiasi suono. */
    void noTone();
    /** @brief Avvia o aggiorna un suono continuo. Usato per suoni di avanzamento. */
    void updateTone(unsigned int frequency);
    /** @brief Avvia la riproduzione non bloccante di una melodia. */
    void playMidiTune(const int notes[][3], int length);
    /** @brief Aggiorna lo stato della melodia in esecuzione. Da chiamare nel loop(). */
    void updateMidiTune();
    /** @brief Ferma la melodia in esecuzione. */
    void stopMidiTune();
    /** @brief Ritorna 'true' se una melodia è in esecuzione. */
    bool isMidiTunePlaying();

    // Funzione RFID
    String readRFID(uint16_t timeout = 1000);

    /** @brief Ritorna l'ID univoco del chip (MAC Address). */
    String getChipId();

private:

    // Oggetti che rappresentano i componenti hardware fisici.
    LiquidCrystal_I2C _lcd;
    Keypad _keypad;
    Button _button1;
    Button _button2;
    Button _key1;
    Button _key2;
    Adafruit_NeoPixel _strip;
    RTC_DS3231 _rtc;
    Adafruit_SSD1306 _oled1;
    Adafruit_SSD1306 _oled2;
    TwoWire _i2c_2;
    PN532_I2C* _nfc_i2c;
    PN532* _nfc;  

    // Variabili private per gestire lo stato interno delle animazioni e dei suoni.
    unsigned long _rainbowLastUpdate;
    uint16_t _rainbowFirstPixelHue;
    unsigned long _breathingLastUpdate;
    float _breathingBrightness;
    bool _breathingUp;
    unsigned long _waveLastUpdate;
    int _waveCenter;

    int _buzzerPin;
    int _buzzerChannel;

    const int (*_currentMidiTune)[3]; // Puntatore a un array di 3 int
    int _midiTuneLength;
    int _currentMidiNoteIndex;
    unsigned long _midiNoteStartTime;
    bool _isMidiNotePlaying;

    int _lcdRows, _lcdCols;
};

#endif // HARDWARE_MANAGER_H