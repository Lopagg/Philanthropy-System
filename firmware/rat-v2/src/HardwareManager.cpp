/**
 * @file HardwareManager.cpp
 * @brief Implementazione dei driver hardware, delega al mixer AudioEngine I2S e gestione bus I2C/SPI.
 */

#include "HardwareManager.h"
#include <math.h>

#define DEBUG_I2C_TIMING 1

// Stato interno per gli effetti dinamici della striscia LED WS2812
static unsigned long s_breathingLastMs = 0;
static float         s_breathingBrightness = 0.15f;
static bool          s_breathingUp = true;

static unsigned long s_rainbowLastMs = 0;
static uint32_t      s_rainbowFirstPixelHue = 0;

static int           s_lastProgressLitLeds = -1;
static uint32_t      s_lastProgressFg = 0;
static uint32_t      s_lastProgressBg = 0;

// Stato per i flash prioritari non-bloccanti (conferma step, pressione tasto, errore)
static bool          s_flashActive = false;
static unsigned long s_flashStartMs = 0;
static uint8_t       s_flashR = 0, s_flashG = 0, s_flashB = 0;
static uint16_t      s_flashOnMs = 100;
static uint16_t      s_flashOffMs = 100;
static uint8_t       s_flashCount = 1;

// ============================================================================
// COSTRUTTORE E INIZIALIZZAZIONE HARDWARE
// ============================================================================

HardwareManager::HardwareManager() :
    _lcd(I2C_ADDR_LCD, 20, 4),
    _oled1(128, 64, &Wire, -1, 400000UL, 100000UL),
    _oled2(128, 64, &Wire, -1, 400000UL, 100000UL),
    _leds(NUM_LEDS, PIN_LEDS, NEO_GRB + NEO_KHZ800)
{
    _nfc_i2c = nullptr;
    _nfc = nullptr;
    _i2cMutex = xSemaphoreCreateMutex();

    _lcdInitialized = false;
    _oled1Initialized = false;
    _oled2Initialized = false;
    _rtcInitialized = false;
    _mcpInitialized = false;
    _nfcInitialized = false;
    _sdInitialized = false;

    _lastMcpState = 0xFFFF;
    _lastDebounceTime = 0;
    _p1State = false; _p1WasPressed = false;
    _p2State = false; _p2WasPressed = false;
    _k1State = false; _k2State = false;
    _lastRawKeyPressed = '\0';
    _debouncedKeyPressed = '\0';
    _keyPressed = '\0';

    _filteredBattMv = 0.0f;

    maxMutexWaitTime = 0;
    maxI2cReadTime = 0;
    missedReads = 0;

    // Mappatura matrice tastierino 4x4 su Port A dell'MCP23017
    _colPins[0] = 4; _colPins[1] = 5; _colPins[2] = 6; _colPins[3] = 7;
    _rowPins[0] = 0; _rowPins[1] = 1; _rowPins[2] = 2; _rowPins[3] = 3;

    const char layout[4][4] = {
        {'1','2','3','A'},
        {'4','5','6','B'},
        {'7','8','9','C'},
        {'*','0','#','D'}
    };
    memcpy(_keymap, layout, sizeof(_keymap));

    _lastLedR = -1; _lastLedG = -1; _lastLedB = -1;
    _lastBarPixels = -1;
    _lastOled1Text = "__INIT__";
    _lastOled2Text = "__INIT__";

    for (int r = 0; r < 4; r++) {
        memset(_lcdShadow[r], ' ', 20);
        _lcdShadow[r][20] = '\0';
    }
}

/**
 * @brief Verifica fisicamente se un dispositivo risponde con ACK all'indirizzo I2C specificato.
 * Deve essere chiamata mentre si detiene già _i2cMutex.
 */
bool HardwareManager::probeI2CDevice(uint8_t address) {
    Wire.beginTransmission(address);
    return (Wire.endTransmission() == 0);
}

void HardwareManager::initialize() {
    Serial.println("--- Inizializzazione Hardware RAT-V2 ---");
    
    // 1. Inizializzazione Bus I2C a 100 kHz (pull-up interni disabilitati perché presenti su PCB)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
    gpio_pullup_dis((gpio_num_t)PIN_I2C_SDA);
    gpio_pullup_dis((gpio_num_t)PIN_I2C_SCL);

    // 2. Inizializzazione GPIO diretti (non bloccanti anche se scollegati)
    pinMode(PIN_MCP_INT, INPUT_PULLUP);
    pinMode(PIN_BATT_SENSE, INPUT_PULLDOWN);

    pinMode(PIN_EXP_CTRL_1, OUTPUT);
    digitalWrite(PIN_EXP_CTRL_1, LOW);

    pinMode(PIN_SMOKE, OUTPUT);
    analogWrite(PIN_SMOKE, 0);

    pinMode(PIN_AUDIO_EN, OUTPUT);
    digitalWrite(PIN_AUDIO_EN, LOW);

    _leds.begin();
    _leds.clear();
    _leds.show();

    // 3. Rilevamento e configurazione periferiche sul bus I2C
    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        // Display LCD 20x4 (0x27)
        if (probeI2CDevice(I2C_ADDR_LCD)) {
            _lcd.init();
            _lcd.backlight();
            uint8_t barChars[5][8] = {
                {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},
                {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
                {0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C},
                {0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E},
                {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}
            };
            for (uint8_t i = 0; i < 5; i++) {
                _lcd.createChar(i, barChars[i]);
            }
            _lcd.clear();
            _lcdInitialized = true;
            Serial.println("[I2C] LCD 20x4 (0x27): OK");
        } else {
            Serial.println("[I2C] LCD 20x4 (0x27): NON RILEVATO");
        }

        // Display OLED 1 (0x3C - Etichetta Pulsante P1)
        if (probeI2CDevice(I2C_ADDR_OLED_1) && _oled1.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_OLED_1)) {
            _oled1.clearDisplay();
            _oled1.display();
            _oled1Initialized = true;
            Serial.println("[I2C] OLED 1 (0x3C): OK");
        } else {
            Serial.println("[I2C] OLED 1 (0x3C): NON RILEVATO");
        }

        // Display OLED 2 (0x3D - Etichetta Pulsante P2)
        if (probeI2CDevice(I2C_ADDR_OLED_2) && _oled2.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_OLED_2)) {
            _oled2.clearDisplay();
            _oled2.display();
            _oled2Initialized = true;
            Serial.println("[I2C] OLED 2 (0x3D): OK");
        } else {
            Serial.println("[I2C] OLED 2 (0x3D): NON RILEVATO");
        }

        // Real-Time Clock DS3231 (0x68)
        if (probeI2CDevice(I2C_ADDR_RTC) && _rtc.begin()) {
            _rtcInitialized = true;
            Serial.println("[I2C] RTC DS3231 (0x68): OK");
        } else {
            Serial.println("[I2C] RTC DS3231 (0x68): NON RILEVATO (attivo fallback millis)");
        }
        
        // Port Expander MCP23017 (0x20)
        if (probeI2CDevice(I2C_ADDR_MCP) && _mcp.begin_I2C(I2C_ADDR_MCP)) {
            _mcp.pinMode(MCP_PIN_P1, INPUT_PULLUP);
            _mcp.pinMode(MCP_PIN_P2, INPUT_PULLUP);
            _mcp.pinMode(MCP_PIN_K1, INPUT_PULLUP);
            _mcp.pinMode(MCP_PIN_K2, INPUT_PULLUP);
            for (int i = 0; i < 4; i++) {
                _mcp.pinMode(_colPins[i], OUTPUT);
                _mcp.digitalWrite(_colPins[i], LOW);
                _mcp.pinMode(_rowPins[i], INPUT_PULLUP);
            }
            _mcpInitialized = true;
            Serial.println("[I2C] MCP23017 (0x20): OK");
        } else {
            Serial.println("[I2C] MCP23017 (0x20): NON RILEVATO");
        }

        // Modulo RFID/NFC PN532 (0x24)
        if (probeI2CDevice(I2C_ADDR_PN532)) {
            _nfc_i2c = new PN532_I2C(Wire);
            _nfc = new PN532(*_nfc_i2c);
            _nfc->begin();
            uint32_t versiondata = _nfc->getFirmwareVersion();
            if (versiondata) {
                _nfc->SAMConfig();
                _nfc->setRFField(0x00, 0x01);
                _nfc->writeRegister(0x6316, 0x78);
                _nfc->setPassiveActivationRetries(0x01);
                _nfcInitialized = true;
                Serial.println("[I2C] RFID PN532 (0x24): OK (RxGain: 48dB Max)");
            } else {
                Serial.println("[I2C] RFID PN532 (0x24): Errore versione firmware");
            }
        } else {
            Serial.println("[I2C] RFID PN532 (0x24): NON RILEVATO");
        }

        Wire.setClock(100000);
        xSemaphoreGive(_i2cMutex);

        if (_nfc_i2c) {
            _nfc_i2c->setMutex(_i2cMutex);
        }
    }

    // 4. Inizializzazione Bus SPI e Scheda Micro-SD
    Serial.print("[SPI] SD Card... ");
    delay(100);
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);
    if (!SD.begin(PIN_SPI_CS, SPI)) {
        Serial.println("NON RILEVATA (attivo fallback toni sintetizzati)");
        printLcdLine(3, "WARN: SD ASSENTE", true);
        _sdInitialized = false;
    } else {
        _sdInitialized = true;
        Serial.println("OK.");
        listSdFiles("/", 2);
    }

    // 5. Inizializzazione Mixer Polifonico I2S (MAX98357A)
    Serial.print("[I2S] Avvio AudioEngine Polifonico... ");
    _audioEngine.begin(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT, PIN_AUDIO_EN);
    _audioEngine.setMasterVolume(21);
    delay(50);
}

// ============================================================================
// GESTIONE AUDIO I2S (DELEGA AL MIXER POLIFONICO AUDIOENGINE)
// ============================================================================

void HardwareManager::listSdFiles(const char* dirname, uint8_t levels) {
    if (!_sdInitialized) return;
    File root = SD.open(dirname);
    if (!root || !root.isDirectory()) return;

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.printf("[SD-SCAN] DIR : %s\n", file.path());
            if (levels) {
                listSdFiles(file.path(), levels - 1);
            }
        } else {
            Serial.printf("[SD-SCAN] FILE: %-32s (%u byte)\n", file.path(), (uint32_t)file.size());
        }
        file = root.openNextFile();
    }
}

void HardwareManager::playAudio(const String& filename, AudioChannel channel) {
    if (_sdInitialized && _audioEngine.playFile(filename, channel)) {
        return;
    }
    if (filename.indexOf("match_start") != -1)      playTone(1000, 200);
    else if (filename.indexOf("match_end") != -1)   playTone(600, 350);
    else if (filename.indexOf("armed") != -1)       playTone(1400, 250);
    else if (filename.indexOf("defused") != -1)     playTone(1600, 300);
    else if (filename.indexOf("explo") != -1)       playTone(200, 600);
    else if (filename.indexOf("min_") != -1)        playTone(1500, 150);
    else if (filename.indexOf("sec_30") != -1)      playTone(1650, 220);
    else if (filename.indexOf("sec_10") != -1)      playTone(1800, 120);
}

void HardwareManager::playAudioUrl(const String& url, AudioChannel channel) {
    _audioEngine.playUrl(url, channel);
}

void HardwareManager::startToneSweep(uint16_t startFreq, uint16_t endFreq, uint32_t durationMs) {
    _audioEngine.startToneSweep(startFreq, endFreq, durationMs);
}

void HardwareManager::playTone(uint16_t freq, uint32_t durationMs) {
    _audioEngine.playTone(freq, durationMs);
}

void HardwareManager::noTone() {
    _audioEngine.stopToneSweep();
}

void HardwareManager::audioLoop() {
    _audioEngine.loop();
}

void HardwareManager::setVolume(uint8_t vol) {
    _audioEngine.setMasterVolume(vol);
}

bool HardwareManager::isAudioPlaying() {
    return _audioEngine.isPlaying();
}

// ============================================================================
// SCANSIONE INPUT FISICI (MCP23017: PULSANTI, CHIAVI E MATRICE TASTIERINO)
// ============================================================================

void HardwareManager::updateInputs() {
    if (!_mcpInitialized) return;

    uint16_t currentState = 0xFFFF;
    char detectedKey = '\0';
    unsigned long waitStart = micros();
    
    if (xSemaphoreTake(_i2cMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        uint32_t waitTime = micros() - waitStart;
        if (waitTime > maxMutexWaitTime) maxMutexWaitTime = waitTime;

        unsigned long readStart = micros();
        currentState = _mcp.readGPIOAB(); 
        
        bool matrixPressed = false;
        for (int i = 0; i < 4; i++) {
            if (!((currentState >> _rowPins[i]) & 0x01)) {
                matrixPressed = true;
                break;
            }
        }

        if (matrixPressed) {
            for (int c = 0; c < 4; c++) {
                for (int i = 0; i < 4; i++) _mcp.digitalWrite(_colPins[i], HIGH);
                _mcp.digitalWrite(_colPins[c], LOW);
                
                uint16_t scanState = _mcp.readGPIOAB();
                for (int r = 0; r < 4; r++) {
                    if (!((scanState >> _rowPins[r]) & 0x01)) {
                        detectedKey = _keymap[r][c]; 
                    }
                }
            }
            
            for (int i = 0; i < 4; i++) _mcp.digitalWrite(_colPins[i], LOW);
            currentState = _mcp.readGPIOAB(); 
        }

        uint32_t readTime = micros() - readStart;
        if (readTime > maxI2cReadTime) maxI2cReadTime = readTime;
        
        xSemaphoreGive(_i2cMutex);
    } else {
        missedReads++;
        return;
    }

    if (currentState != _lastMcpState || detectedKey != _lastRawKeyPressed) {
        _lastDebounceTime = millis();
    }

    if ((millis() - _lastDebounceTime) > 15) { 
        bool currentP1 = !((currentState >> MCP_PIN_P1) & 0x01);
        bool currentP2 = !((currentState >> MCP_PIN_P2) & 0x01);

        if (currentP1 != _p1State) { _p1State = currentP1; if (_p1State) _p1WasPressed = true; }
        if (currentP2 != _p2State) { _p2State = currentP2; if (_p2State) _p2WasPressed = true; }

        _k1State = !((currentState >> MCP_PIN_K1) & 0x01);
        _k2State = !((currentState >> MCP_PIN_K2) & 0x01);

        if (detectedKey != '\0' && detectedKey != _debouncedKeyPressed) {
            _keyPressed = detectedKey; 
        }
        _debouncedKeyPressed = detectedKey;
    }
    
    _lastMcpState = currentState;
    _lastRawKeyPressed = detectedKey;
}

bool HardwareManager::isButton1Pressed() { return _p1State; }
bool HardwareManager::wasButton1Pressed() { if (_p1WasPressed) { _p1WasPressed = false; return true; } return false; }
bool HardwareManager::isButton2Pressed() { return _p2State; }
bool HardwareManager::wasButton2Pressed() { if (_p2WasPressed) { _p2WasPressed = false; return true; } return false; }
bool HardwareManager::isKey1Inserted() { return _k1State; }
bool HardwareManager::isKey2Inserted() { return _k2State; }
char HardwareManager::getKeyPressed() { if (_keyPressed != '\0') { char k = _keyPressed; _keyPressed = '\0'; return k; } return '\0'; }

// ============================================================================
// GESTIONE DISPLAY LCD 20x4 AD ALTA VELOCITÀ (I2C NIBBLE BURSTING + DIFFING)
// ============================================================================

#define PCF_RS  0x01
#define PCF_EN  0x04
#define PCF_BL  0x08

void HardwareManager::lcdSendByte(uint8_t val, uint8_t mode) {
    uint8_t hi = (val & 0xF0) | mode | PCF_BL;
    uint8_t lo = ((val << 4) & 0xF0) | mode | PCF_BL;

    Wire.beginTransmission(I2C_ADDR_LCD);
    Wire.write(hi | PCF_EN);
    Wire.write(hi);
    Wire.write(lo | PCF_EN);
    Wire.write(lo);
    Wire.endTransmission();
}

/**
 * @brief Posiziona il cursore e invia una sequenza di caratteri impacchettando fino a 7 caratteri
 *        (28 byte) per singola transazione I2C. Riduce il tempo di una riga da 33ms a ~7.8ms.
 */
void HardwareManager::lcdWriteBurst(uint8_t col, uint8_t row, const char* data, uint8_t len) {
    static const uint8_t rowOffsets[4] = { 0x00, 0x40, 0x14, 0x54 };
    if (row > 3 || len == 0) return;

    lcdSendByte(0x80 | (col + rowOffsets[row]), 0);

    uint8_t idx = 0;
    while (idx < len) {
        uint8_t chunk = (len - idx > 7) ? 7 : (len - idx);
        Wire.beginTransmission(I2C_ADDR_LCD);
        for (uint8_t i = 0; i < chunk; i++) {
            uint8_t c = (uint8_t)data[idx + i];
            uint8_t hi = (c & 0xF0) | PCF_RS | PCF_BL;
            uint8_t lo = ((c << 4) & 0xF0) | PCF_RS | PCF_BL;
            Wire.write(hi | PCF_EN);
            Wire.write(hi);
            Wire.write(lo | PCF_EN);
            Wire.write(lo);
        }
        Wire.endTransmission();
        idx += chunk;
    }
}

void HardwareManager::clearLcd() {
    if (!_lcdInitialized) return;
    for (int r = 0; r < 4; r++) {
        printLcdLine(r, "", false);
    }
    _lastBarPixels = -1;
}

void HardwareManager::printLcd(int col, int row, const String& text) {
    if (!_lcdInitialized || row < 0 || row > 3 || col < 0 || col >= 20) return;
    int len = text.length();
    if (col + len > 20) len = 20 - col;
    if (len <= 0) return;

    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        lcdWriteBurst((uint8_t)col, (uint8_t)row, text.c_str(), (uint8_t)len);
        for (int i = 0; i < len; i++) {
            _lcdShadow[row][col + i] = text[i];
        }
        xSemaphoreGive(_i2cMutex);
    }
}

void HardwareManager::printLcdLine(int row, const String& text, bool centered) {
    if (!_lcdInitialized || row < 0 || row > 3) return;
    char buf[21];
    memset(buf, ' ', 20);
    buf[20] = '\0';

    int len = text.length();
    if (len > 20) len = 20;
    int startCol = centered ? ((20 - len) / 2) : 0;
    if (startCol < 0) startCol = 0;

    for (int i = 0; i < len && (startCol + i) < 20; i++) {
        buf[startCol + i] = text[i];
    }

    int firstDiff = -1;
    int lastDiff  = -1;
    if (_lastBarPixels >= 0 && row == 2) {
        firstDiff = 0;
        lastDiff  = 19;
        _lastBarPixels = -1;
    } else {
        for (int c = 0; c < 20; c++) {
            if (buf[c] != _lcdShadow[row][c]) {
                if (firstDiff == -1) firstDiff = c;
                lastDiff = c;
            }
        }
    }

    if (firstDiff == -1) return;

    unsigned long tWait = micros();
    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        unsigned long tStart = micros();

        uint8_t spanLen = (uint8_t)(lastDiff - firstDiff + 1);
        lcdWriteBurst((uint8_t)firstDiff, (uint8_t)row, &buf[firstDiff], spanLen);
        memcpy(_lcdShadow[row], buf, 21);

        unsigned long tEnd = micros();
        xSemaphoreGive(_i2cMutex);

#if DEBUG_I2C_TIMING
        Serial.printf("[I2C-PROF] LCD R%d (%d char): %.2f ms (attesa mutex: %.2f ms)\n",
                      row, spanLen, (tEnd - tStart) / 1000.0f, (tStart - tWait) / 1000.0f);
#endif
    }
}

void HardwareManager::writeCustomChar(uint8_t charIndex) {
    if (!_lcdInitialized) return;
    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        lcdSendByte(charIndex, PCF_RS);
        xSemaphoreGive(_i2cMutex);
    }
}

void HardwareManager::drawProgressBar(int row, unsigned long progress, unsigned long totalDuration) {
    if (!_lcdInitialized) return;
    if (totalDuration == 0) totalDuration = 1;
    if (progress > totalDuration) progress = totalDuration;

    const int barWidthChars = 16;
    const int totalPixels = barWidthChars * 5;
    int progressPixels = (progress * totalPixels) / totalDuration;

    if (progressPixels == _lastBarPixels) return;

    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        char barBuf[16];
        if (_lastBarPixels < 0 || progressPixels < _lastBarPixels) {
            memset(barBuf, ' ', barWidthChars);
            lcdWriteBurst(2, (uint8_t)row, barBuf, barWidthChars);
            _lastBarPixels = 0;
        }

        int startChar = _lastBarPixels / 5;
        int endChar = progressPixels / 5;
        if (endChar >= barWidthChars) endChar = barWidthChars - 1;

        uint8_t spanLen = (uint8_t)(endChar - startChar + 1);
        for (int c = startChar; c <= endChar; c++) {
            int charPixels = progressPixels - (c * 5);
            if (charPixels >= 5)      barBuf[c - startChar] = (char)4;
            else if (charPixels > 0)  barBuf[c - startChar] = (char)(charPixels - 1);
            else                      barBuf[c - startChar] = ' ';
        }

        lcdWriteBurst((uint8_t)(2 + startChar), (uint8_t)row, barBuf, spanLen);
        _lastBarPixels = progressPixels;
        _lcdShadow[row][0] = '\x01';
        xSemaphoreGive(_i2cMutex);
    }
}

// ============================================================================
// GESTIONE DUAL OLED 128x64 (ETICHETTE DINAMICHE PULSANTI P1 E P2)
// ============================================================================

void HardwareManager::drawOledText(Adafruit_SSD1306& display, bool isInitialized, const String& text, int size, int x, int y) {
    if (!isInitialized) return;

    unsigned long tWait = micros();
    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        unsigned long tStart = micros();
        delayMicroseconds(1000);

        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextWrap(false);

        if (text.length() > 0) {
            int newlineIdx = text.indexOf('\n');
            if (newlineIdx >= 0 && x < 0 && y < 0) {
                String line1 = text.substring(0, newlineIdx);
                String line2 = text.substring(newlineIdx + 1);

                int size1 = (line1.length() > 10 && size >= 2) ? 1 : size;
                int size2 = (line2.length() > 10 && size >= 2) ? 1 : size;

                int16_t x1, y1;
                uint16_t w1, h1, w2, h2;

                display.setTextSize(size1);
                display.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
                int drawX1 = (128 - (int)w1) / 2;
                if (drawX1 < 0) drawX1 = 0;

                display.setTextSize(size2);
                display.getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
                int drawX2 = (128 - (int)w2) / 2;
                if (drawX2 < 0) drawX2 = 0;

                int gap = 8;
                int totalH = (int)h1 + gap + (int)h2;
                int startY = (64 - totalH) / 2;
                if (startY < 0) startY = 0;

                display.setTextSize(size1);
                display.setCursor(drawX1, startY);
                display.print(line1);

                display.setTextSize(size2);
                display.setCursor(drawX2, startY + (int)h1 + gap);
                display.print(line2);
            } else if (x < 0 || y < 0) {
                int effSize = (text.length() > 10 && size >= 2) ? 1 : size;
                display.setTextSize(effSize);

                int16_t x1, y1;
                uint16_t w, h;
                display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

                int drawX = (x < 0) ? ((128 - (int)w) / 2) : x;
                int drawY = (y < 0) ? ((64 - (int)h) / 2) : y;
                if (drawX < 0) drawX = 0;
                if (drawY < 0) drawY = 0;

                display.setCursor(drawX, drawY);
                display.print(text);
            } else {
                display.setTextSize(size);
                display.setCursor(x, y);
                display.print(text);
            }
        }

        display.ssd1306_command(SSD1306_MEMORYMODE);
        display.ssd1306_command(0x00);
        display.display();

        Wire.setClock(100000);
        delayMicroseconds(1000);
        unsigned long tEnd = micros();
        xSemaphoreGive(_i2cMutex);

#if DEBUG_I2C_TIMING
        Serial.printf("[I2C-PROF] OLED Flush: %.2f ms (attesa mutex: %.2f ms)\n",
                      (tEnd - tStart) / 1000.0f, (tStart - tWait) / 1000.0f);
#endif
    }
}

void HardwareManager::clearOled1() {
    printOled1("", 2, -1, -1);
}

void HardwareManager::printOled1(const String& text, int size, int x, int y) {
    String key = text + "|" + String(size);
    if (_lastOled1Text == key) return;
    _lastOled1Text = key;
    drawOledText(_oled1, _oled1Initialized, text, size, x, y);
}

void HardwareManager::clearOled2() {
    _lastOled2Text = "";
    drawOledText(_oled2, _oled2Initialized, "", 1, 0, 0);
}

void HardwareManager::printOled2(const String& text, int size, int x, int y) {
    String key = text + "|" + String(size);
    if (_lastOled2Text == key) return;
    _lastOled2Text = key;
    drawOledText(_oled2, _oled2Initialized, text, size, x, y);
}

void HardwareManager::setOledLabels(const String& p1Text, const String& p2Text) {
    printOled1(p1Text, 2, -1, -1);
    printOled2(p2Text, 2, -1, -1);
}

void HardwareManager::clearOled() {
    setOledLabels("", "");
}

void HardwareManager::printOled(const String& text, int size, int x, int y) {
    printOled1(text, size, x, y);
}

// ============================================================================
// RTC DS3231, LETTORE RFID PN532 E MONITORAGGIO BATTERIA
// ============================================================================

bool HardwareManager::syncRTCWithNTP(uint32_t timeoutMs) {
    configTzTime(TIMEZONE_ITALY, NTP_SERVER_1, NTP_SERVER_2);

    struct tm timeinfo;
    unsigned long startMs = millis();
    bool ntpSuccess = false;

    while ((millis() - startMs) < timeoutMs) {
        if (getLocalTime(&timeinfo, 200)) {
            ntpSuccess = true;
            break;
        }
    }

    if (!ntpSuccess) {
        Serial.println("[RTC] Timeout sincronizzazione NTP: mantengo orario locale.");
        return false;
    }

    DateTime ntpDateTime(
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    );

    if (_rtcInitialized && xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        _rtc.adjust(ntpDateTime);
        xSemaphoreGive(_i2cMutex);
        Serial.printf("[RTC] DS3231 sincronizzato su fuso IT: %02d/%02d/%04d %02d:%02d:%02d\n",
                      ntpDateTime.day(), ntpDateTime.month(), ntpDateTime.year(),
                      ntpDateTime.hour(), ntpDateTime.minute(), ntpDateTime.second());
    } else {
        Serial.printf("[RTC] Orologio interno sincronizzato via NTP: %02d:%02d:%02d\n",
                      ntpDateTime.hour(), ntpDateTime.minute(), ntpDateTime.second());
    }

    return true;
}

DateTime HardwareManager::getRTCTime() {
    if (_rtcInitialized) {
        DateTime now;
        if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
            now = _rtc.now();
            xSemaphoreGive(_i2cMutex);
        }
        if (now.year() >= 2024) {
            return now;
        }
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0) && (timeinfo.tm_year + 1900) >= 2024) {
        return DateTime(
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec
        );
    }

    return DateTime(2026, 1, 1, 0, 0, 0) + TimeSpan(millis() / 1000);
}

String HardwareManager::readRFID(uint16_t timeout) {
    if (!_nfcInitialized || !_nfc) return "";
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength = 0;

    unsigned long tStart = micros();
    bool success = _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, timeout);
    unsigned long tEnd = micros();

    if (success) {
        String uidString = "";
        for (uint8_t i = 0; i < uidLength; i++) {
            if (uid[i] < 0x10) uidString += "0";
            uidString += String(uid[i], HEX);
        }
        uidString.toUpperCase();
#if DEBUG_I2C_TIMING
        Serial.printf("[RFID-PROF] Tag Letto: %s (%u byte) in %.2f ms (I2C mutex libero durante RF)\n",
                      uidString.c_str(), uidLength, (tEnd - tStart) / 1000.0f);
#endif
        return uidString;
    }
    return "";
}

float HardwareManager::readBatteryVoltage() {
    uint32_t sumMv = 0;
    for (uint8_t i = 0; i < 8; i++) {
        sumMv += analogReadMilliVolts(PIN_BATT_SENSE);
    }
    float rawMv = sumMv / 8.0f;

    if (rawMv < 300.0f) {
        _filteredBattMv = 0.0f;
        return 0.0f;
    }

    if (_filteredBattMv < 300.0f) {
        _filteredBattMv = rawMv;
    } else {
        _filteredBattMv = (_filteredBattMv * 0.80f) + (rawMv * 0.20f);
    }

    float voltage = (_filteredBattMv * BATT_DIVIDER_RATIO * BATT_CALIBRATION_TRIM) / 1000.0f;
    if (voltage < 5.50f) {
        return 0.0f;
    }
    return voltage;
}

int8_t HardwareManager::readBatteryPercentage() {
    float v = readBatteryVoltage();
    if (v < 5.50f) return -1;

    const float vTable[6] = { 6.60f, 7.00f, 7.40f, 7.60f, 8.00f, 8.40f };
    const float pTable[6] = { 0.0f,  10.0f, 35.0f, 60.0f, 85.0f, 100.0f };

    if (v <= vTable[0]) return 0;
    if (v >= vTable[5]) return 100;

    for (uint8_t i = 0; i < 5; i++) {
        if (v >= vTable[i] && v <= vTable[i + 1]) {
            float ratio = (v - vTable[i]) / (vTable[i + 1] - vTable[i]);
            float pct = pTable[i] + ratio * (pTable[i + 1] - pTable[i]);
            return (int8_t)roundf(pct);
        }
    }
    return 0;
}

void HardwareManager::printI2CProfilerStats() {
    Serial.println("--- DIAGNOSTICA BUS I2C ---");
    Serial.print("Max attesa semaforo : "); Serial.print(maxMutexWaitTime); Serial.println(" us");
    Serial.print("Max tempo lettura   : "); Serial.print(maxI2cReadTime); Serial.println(" us");
    Serial.print("Letture saltate     : "); Serial.println(missedReads);
    maxMutexWaitTime = 0; maxI2cReadTime = 0; missedReads = 0;
}

// ============================================================================
// CONTROLLO LED WS2812 (FISSO, BREATHING, RAINBOW, PROGRESSO, FLASH), FUMO E RELÈ
// ============================================================================

void HardwareManager::setLedsColor(uint8_t r, uint8_t g, uint8_t b) {
    if (_lastLedR == r && _lastLedG == g && _lastLedB == b) return;
    _lastLedR = r; _lastLedG = g; _lastLedB = b;
    s_lastProgressLitLeds = -1;

    uint32_t col = _leds.Color(r, g, b);
    for (uint16_t i = 0; i < _leds.numPixels(); i++) {
        _leds.setPixelColor(i, col);
    }
    _leds.show();
}

void HardwareManager::updateBreathingEffect(uint8_t r, uint8_t g, uint8_t b) {
    if (updateLedFlash()) return;

    unsigned long now = millis();
    if (now - s_breathingLastMs < 25) return;
    s_breathingLastMs = now;

    // Escursione 6% <-> 42% (come il limite 80/255 del RAT-V1): rende il respiro
    // molto più evidente e lascia il 100% di potenza ai flash di minuto/30s/errore!
    if (s_breathingUp) {
        s_breathingBrightness += 0.012f;
        if (s_breathingBrightness >= 0.42f) {
            s_breathingBrightness = 0.42f;
            s_breathingUp = false;
        }
    } else {
        s_breathingBrightness -= 0.012f;
        if (s_breathingBrightness <= 0.06f) {
            s_breathingBrightness = 0.06f;
            s_breathingUp = true;
        }
    }

    uint8_t outR = (uint8_t)(r * s_breathingBrightness);
    uint8_t outG = (uint8_t)(g * s_breathingBrightness);
    uint8_t outB = (uint8_t)(b * s_breathingBrightness);
    setLedsColor(outR, outG, outB);
}

void HardwareManager::updateRainbowEffect() {
    if (updateLedFlash()) return;

    unsigned long now = millis();
    if (now - s_rainbowLastMs < 25) return;
    s_rainbowLastMs = now;

    uint16_t numPixels = _leds.numPixels();
    for (uint16_t i = 0; i < numPixels; i++) {
        uint16_t pixelHue = s_rainbowFirstPixelHue + (uint32_t)i * 65536UL / numPixels;
        uint32_t c = _leds.gamma32(_leds.ColorHSV(pixelHue, 255, 110));
        _leds.setPixelColor(i, c);
    }
    _leds.show();
    _lastLedR = -1; _lastLedG = -1; _lastLedB = -1;
    s_lastProgressLitLeds = -1;
    s_rainbowFirstPixelHue = (s_rainbowFirstPixelHue + 256) & 0xFFFF;
}

void HardwareManager::setLedsProgress(uint32_t progress, uint32_t totalDuration,
                                      uint8_t fgR, uint8_t fgG, uint8_t fgB,
                                      uint8_t bgR, uint8_t bgG, uint8_t bgB) {
    if (totalDuration == 0) totalDuration = 1;
    if (progress > totalDuration) progress = totalDuration;

    uint16_t numPixels = _leds.numPixels();
    int ledsToShow = (int)(((uint64_t)progress * numPixels) / totalDuration);

    uint32_t fgColor = _leds.Color(fgR, fgG, fgB);
    uint32_t bgColor = _leds.Color(bgR, bgG, bgB);

    if (ledsToShow == s_lastProgressLitLeds && fgColor == s_lastProgressFg && bgColor == s_lastProgressBg) {
        return;
    }

    s_lastProgressLitLeds = ledsToShow;
    s_lastProgressFg = fgColor;
    s_lastProgressBg = bgColor;
    _lastLedR = -1; _lastLedG = -1; _lastLedB = -1;

    for (uint16_t i = 0; i < numPixels; i++) {
        _leds.setPixelColor(i, (i < ledsToShow) ? fgColor : bgColor);
    }
    _leds.show();
}

void HardwareManager::triggerLedFlash(uint8_t r, uint8_t g, uint8_t b, uint16_t onMs, uint16_t offMs, uint8_t count) {
    s_flashR = r;
    s_flashG = g;
    s_flashB = b;
    s_flashOnMs = (onMs > 0) ? onMs : 80;
    s_flashOffMs = (offMs > 0) ? offMs : 80;
    s_flashCount = (count > 0) ? count : 1;
    s_flashStartMs = millis();
    s_flashActive = true;
    setLedsColor(r, g, b);
}

bool HardwareManager::updateLedFlash() {
    if (!s_flashActive) return false;

    uint32_t cycleMs = (uint32_t)s_flashOnMs + (uint32_t)s_flashOffMs;
    uint32_t totalMs = cycleMs * s_flashCount;
    unsigned long elapsed = millis() - s_flashStartMs;

    if (elapsed >= totalMs) {
        s_flashActive = false;
        return false;
    }

    uint32_t inCycle = elapsed % cycleMs;
    if (inCycle < s_flashOnMs) {
        setLedsColor(s_flashR, s_flashG, s_flashB);
    } else {
        setLedsColor(0, 0, 0);
    }
    return true;
}

void HardwareManager::triggerSmoke(uint8_t pwmValue) {
    analogWrite(PIN_SMOKE, pwmValue);
}

void HardwareManager::setRelay(bool state) {
    digitalWrite(PIN_EXP_CTRL_1, state ? HIGH : LOW);
}