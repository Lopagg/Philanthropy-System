/**
 * @file HardwareManager.cpp
 * @brief Implementazione dei driver hardware, sintesi audio DDS I2S e gestione bus I2C/SPI.
 */

#include "HardwareManager.h"
#include <driver/i2s.h>
#include <math.h>

// ============================================================================
// CALLBACK DIAGNOSTICHE LIBRERIA AUDIO I2S
// ============================================================================

void audio_info(const char *info) {
    Serial.printf("[AUDIO-LIB] %s\n", info);
}

void audio_eof_mp3(const char *info) {
    Serial.printf("[AUDIO-EOF] Fine traccia MP3: %s\n", info);
}

// ============================================================================
// COSTRUTTORE E INIZIALIZZAZIONE HARDWARE
// ============================================================================

HardwareManager::HardwareManager() :
    _lcd(I2C_ADDR_LCD, 20, 4),
    _oled1(128, 64, &Wire, -1),
    _oled2(128, 64, &Wire, -1),
    _leds(NUM_LEDS, PIN_LEDS, NEO_GRB + NEO_KHZ800)
{
    _nfc_i2c = nullptr;
    _nfc = nullptr;
    _i2cMutex = xSemaphoreCreateMutex();
    _audioMutex = xSemaphoreCreateMutex();

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

    _toneActive = false;
    _toneIsSweep = false;
    _toneStartFreq = 0;
    _toneEndFreq = 0;
    _toneTotalSamples = 0;
    _toneSampleIndex = 0;
    _toneTailSamples = 0;
    _tonePhase = 0;
    memset(_pendingMp3Path, 0, sizeof(_pendingMp3Path));
    _ampTurnOffTimeMs = 0;
    _waitingAmpTurnOff = false;
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
    
    // 1. Inizializzazione Bus I2C (pull-up interni disabilitati perché presenti su PCB)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
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
                _nfc->setPassiveActivationRetries(0x10);
                _nfcInitialized = true;
                Serial.println("[I2C] RFID PN532 (0x24): OK");
            } else {
                Serial.println("[I2C] RFID PN532 (0x24): Errore versione firmware");
            }
        } else {
            Serial.println("[I2C] RFID PN532 (0x24): NON RILEVATO");
        }

        Wire.setClock(100000);
        xSemaphoreGive(_i2cMutex); 
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

    // 5. Inizializzazione Bus Audio I2S (MAX98357A) con Pre-Gain EQ
    Serial.print("[I2S] Audio MAX98357A... ");
    _audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
    _audio.setVolume(21);          // Volume master al massimo (scala 0..21)
    _audio.setTone(2, 6, 6);       // Boost +2dB Bassi, +6dB Medi, +6dB Alti per massima resa su speaker compatti
    Serial.println("OK.");
    delay(50);
}

// ============================================================================
// GESTIONE AUDIO I2S (MP3 DA MICRO-SD + SINTETIZZATORE DDS)
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

bool HardwareManager::isValidAudioFile(const String& path) {
    if (!_sdInitialized || !SD.exists(path)) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    size_t sz = f.size();
    f.close();
    return (sz > 512);
}

/**
 * @brief Aggiunge una coda di 24 frame MP3 silenziosi (~620ms) ai file sulla SD (una tantum)
 * per evitare che la chiusura anticipata dello stream da parte di ESP32-audioI2S tagli il finale.
 */
void HardwareManager::ensureMp3Padding(const String& path) {
    if (!_sdInitialized) return;
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    size_t sz = f.size();
    if (sz <= 512) {
        f.close();
        return;
    }

    const uint8_t marker[4] = {'Z', 'P', 'A', 'D'};
    uint8_t tail[4] = {0};
    if (sz > 4) {
        f.seek(sz - 4);
        f.read(tail, 4);
    }
    f.close();

    if (memcmp(tail, marker, 4) == 0) return;

    File fw = SD.open(path, FILE_APPEND);
    if (!fw) return;

    uint8_t silentFrame[417];
    memset(silentFrame, 0, sizeof(silentFrame));
    silentFrame[0] = 0xFF;
    silentFrame[1] = 0xFB;
    silentFrame[2] = 0x90;
    silentFrame[3] = 0x64;

    for (int i = 0; i < 24; i++) {
        fw.write(silentFrame, sizeof(silentFrame));
    }
    fw.write(marker, 4);
    fw.close();
}

void HardwareManager::playAudio(const String& filename) {
    String resolvedPath = "";

    if (_sdInitialized && xSemaphoreTake(_audioMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (isValidAudioFile(filename)) {
            resolvedPath = filename;
        } else {
            int lastSlash = filename.lastIndexOf('/');
            if (lastSlash >= 0) {
                String baseName = filename.substring(lastSlash);
                if (isValidAudioFile(baseName)) {
                    resolvedPath = baseName;
                } else {
                    String legacyName = baseName;
                    legacyName.replace("/bomb_exploded.mp3", "/explosion.mp3");
                    legacyName.replace("/bomb_armed.mp3", "/armed.mp3");
                    legacyName.replace("/bomb_defused.mp3", "/defused.mp3");
                    if (isValidAudioFile(legacyName)) {
                        resolvedPath = legacyName;
                    }
                }
            }
        }

        if (resolvedPath.length() > 0) {
            ensureMp3Padding(resolvedPath);
            _waitingAmpTurnOff = false;

            // Se un beep breve (es. conferma PIN) sta uscendo dal DMA, accoda l'MP3
            if (_toneActive && !_toneIsSweep) {
                strncpy(_pendingMp3Path, resolvedPath.c_str(), sizeof(_pendingMp3Path) - 1);
                _pendingMp3Path[sizeof(_pendingMp3Path) - 1] = '\0';
                xSemaphoreGive(_audioMutex);
                return;
            }

            _toneActive = false;
            _toneIsSweep = false;
            _pendingMp3Path[0] = '\0';
            if (_audio.isRunning()) {
                _audio.stopSong();
            }
            digitalWrite(PIN_AUDIO_EN, HIGH);
            delay(15);
            _audio.connecttoFS(SD, resolvedPath.c_str());
            xSemaphoreGive(_audioMutex);
            return;
        }
        xSemaphoreGive(_audioMutex);
    }

    // Fallback sintetizzato se la SD è scollegata o il file MP3 non è presente
    if (filename.indexOf("match_start") != -1) {
        playTone(1000, 200);
    } else if (filename.indexOf("match_end") != -1) {
        playTone(600, 350);
    } else if (filename.indexOf("armed") != -1) {
        playTone(1400, 250);
    } else if (filename.indexOf("defused") != -1) {
        playTone(1600, 300);
    } else if (filename.indexOf("explo") != -1) {
        playTone(200, 600);
    }
}

void HardwareManager::startToneInternal(uint16_t startFreq, uint16_t endFreq, uint32_t durationMs, bool isSweep) {
    if (xSemaphoreTake(_audioMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (isSweep) {
            _pendingMp3Path[0] = '\0';
        }
        if (_audio.isRunning()) {
            _audio.stopSong();
        }

        if (!_toneActive) {
            i2s_start(I2S_NUM_0);
            i2s_set_clk(I2S_NUM_0, 44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
            digitalWrite(PIN_AUDIO_EN, HIGH);
        }

        uint32_t dur = (durationMs > 0) ? durationMs : 1;
        _toneStartFreq = startFreq;
        _toneEndFreq = endFreq;

        uint32_t totalSamples = ((uint64_t)dur * 44100ULL) / 1000ULL;
        if (totalSamples == 0) totalSamples = 44;

        _toneTotalSamples = totalSamples;
        _toneSampleIndex = 0;
        _toneTailSamples = 10240; // Svuotamento completo del ring buffer DMA I2S (8192 campioni)
        _toneIsSweep = isSweep;
        _toneActive = true;

        xSemaphoreGive(_audioMutex);
    }
}

void HardwareManager::startToneSweep(uint16_t startFreq, uint16_t endFreq, uint32_t durationMs) {
    startToneInternal(startFreq, endFreq, durationMs, true);
}

void HardwareManager::playTone(uint16_t freq, uint32_t durationMs) {
    startToneInternal(freq, freq, durationMs, false);
}

void HardwareManager::noTone() {
    if (_toneActive && _toneIsSweep) {
        if (xSemaphoreTake(_audioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            _toneActive = false;
            _toneIsSweep = false;
            if (!_audio.isRunning()) {
                i2s_zero_dma_buffer(I2S_NUM_0);
                digitalWrite(PIN_AUDIO_EN, LOW);
            }
            xSemaphoreGive(_audioMutex);
        }
    }
}

void HardwareManager::audioLoop() {
    if (xSemaphoreTake(_audioMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }

    if (!_toneActive) {
        _audio.loop();
        static bool wasRunning = false;
        bool isRunning = _audio.isRunning();
        if (wasRunning && !isRunning) {
            _waitingAmpTurnOff = true;
            _ampTurnOffTimeMs = millis();
        }
        if (_waitingAmpTurnOff && !isRunning && (millis() - _ampTurnOffTimeMs >= 250)) {
            _waitingAmpTurnOff = false;
            digitalWrite(PIN_AUDIO_EN, LOW);
        }
        wasRunning = isRunning;
        xSemaphoreGive(_audioMutex);
        return;
    }

    _waitingAmpTurnOff = false;
    int16_t samples[512];
    const float amplitude = 26000.0f; // ~80% del fondoscala 16-bit (32767) per il massimo volume pulito

    for (int i = 0; i < 256; i++) {
        int16_t val = 0;

        if (_toneSampleIndex < _toneTotalSamples) {
            int32_t fStart = _toneStartFreq;
            int32_t fEnd = _toneEndFreq;
            uint16_t currentFreq = fStart + ((int64_t)(fEnd - fStart) * (int64_t)_toneSampleIndex) / (int64_t)_toneTotalSamples;
            _toneSampleIndex++;

            uint32_t phaseStep = ((uint64_t)currentFreq << 16) / 44100;
            _tonePhase = (_tonePhase + phaseStep) & 0xFFFF;
            float angle = (_tonePhase * 6.2831853f) / 65536.0f;
            val = (int16_t)(sinf(angle) * amplitude);
        } else if (_toneTailSamples > 0) {
            _toneTailSamples--;
            val = 0;
        } else {
            _toneActive = false;
            _toneIsSweep = false;

            if (_pendingMp3Path[0] != '\0') {
                char nextMp3[64];
                strncpy(nextMp3, _pendingMp3Path, sizeof(nextMp3));
                _pendingMp3Path[0] = '\0';
                digitalWrite(PIN_AUDIO_EN, HIGH);
                _audio.connecttoFS(SD, nextMp3);
            } else if (!_audio.isRunning()) {
                digitalWrite(PIN_AUDIO_EN, LOW);
            }
            xSemaphoreGive(_audioMutex);
            return;
        }

        samples[i * 2]     = val;
        samples[i * 2 + 1] = val;
    }

    xSemaphoreGive(_audioMutex);

    size_t bytesWritten = 0;
    i2s_write(I2S_NUM_0, samples, sizeof(samples), &bytesWritten, pdMS_TO_TICKS(100));
}

void HardwareManager::setVolume(uint8_t vol) {
    _audio.setVolume(vol);
}

bool HardwareManager::isAudioPlaying() {
    return _audio.isRunning();
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
// GESTIONE DISPLAY LCD 20x4 E BARRA DI AVANZAMENTO PIXEL-PER-PIXEL
// ============================================================================

void HardwareManager::clearLcd() {
    if (!_lcdInitialized) return;
    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        _lcd.clear();
        _lastBarPixels = -1;
        xSemaphoreGive(_i2cMutex);
    }
}

void HardwareManager::printLcd(int col, int row, const String& text) {
    if (!_lcdInitialized) return;
    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        delayMicroseconds(500);
        _lcd.setCursor(col, row);
        _lcd.print(text);
        xSemaphoreGive(_i2cMutex);
    }
}

void HardwareManager::printLcdLine(int row, const String& text, bool centered) {
    if (!_lcdInitialized) return;
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

    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        delayMicroseconds(500);
        if (row == 2) _lastBarPixels = -1;
        _lcd.setCursor(0, row);
        _lcd.print(buf);
        xSemaphoreGive(_i2cMutex);
    }
}

void HardwareManager::writeCustomChar(uint8_t charIndex) {
    if (!_lcdInitialized) return;
    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        _lcd.write(byte(charIndex));
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
        if (_lastBarPixels < 0 || progressPixels < _lastBarPixels) {
            _lcd.setCursor(2, row);
            for (int i = 0; i < barWidthChars; i++) _lcd.print(" ");
            _lastBarPixels = 0;
        }

        int startChar = _lastBarPixels / 5;
        int endChar = progressPixels / 5;
        if (endChar >= barWidthChars) endChar = barWidthChars - 1;

        _lcd.setCursor(2 + startChar, row);
        for (int c = startChar; c <= endChar; c++) {
            int charPixels = progressPixels - (c * 5);
            if (charPixels >= 5) {
                _lcd.write(byte(4));
            } else if (charPixels > 0) {
                _lcd.write(byte(charPixels - 1));
            } else {
                _lcd.print(" ");
            }
        }
        _lastBarPixels = progressPixels;
        xSemaphoreGive(_i2cMutex);
    }
}

// ============================================================================
// GESTIONE DUAL OLED 128x64 (ETICHETTE DINAMICHE PULSANTI P1 E P2)
// ============================================================================

void HardwareManager::drawOledText(Adafruit_SSD1306& display, bool isInitialized, const String& text, int size, int x, int y) {
    if (!isInitialized) return;

    if (xSemaphoreTake(_i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextWrap(false);

        int newlineIdx = text.indexOf('\n');
        if (newlineIdx >= 0 && x < 0 && y < 0) {
            // Modalità a 2 righe centrate (es. "ALPHA\nCONQUISTA")
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
            // Modalità a riga singola centrata (es. "INNESCA", "TEST HW")
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

        display.display();
        xSemaphoreGive(_i2cMutex);
    }
}

void HardwareManager::clearOled1() {
    printOled1("", 1, 0, 0);
}

void HardwareManager::printOled1(const String& text, int size, int x, int y) {
    if (_lastOled1Text == text) return;
    _lastOled1Text = text;
    drawOledText(_oled1, _oled1Initialized, text, size, x, y);
}

void HardwareManager::clearOled2() {
    printOled2("", 1, 0, 0);
}

void HardwareManager::printOled2(const String& text, int size, int x, int y) {
    if (_lastOled2Text == text) return;
    _lastOled2Text = text;
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

/**
 * @brief Sincronizza l'orologio di sistema via NTP sul fuso orario italiano (CET/CEST)
 *        e aggiorna il modulo hardware RTC DS3231 se presente sul bus I2C.
 */
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
    // 1. Priorità al chip RTC esterno DS3231
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

    // 2. Fallback sull'RTC interno dell'ESP32-S3 (sincronizzato via NTP)
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

    // 3. Fallback finale basato su millis() se offline e senza batteria RTC
    return DateTime(2026, 1, 1, 0, 0, 0) + TimeSpan(millis() / 1000);
}

String HardwareManager::readRFID(uint16_t timeout) {
    if (!_nfcInitialized || !_nfc) return "";
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength = 0;
    bool success = false;
    
    if (xSemaphoreTake(_i2cMutex, portMAX_DELAY) == pdTRUE) {
        success = _nfc->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, timeout);
        xSemaphoreGive(_i2cMutex);
    }
    
    if (success) {
        String uidString = "";
        for (uint8_t i = 0; i < uidLength; i++) {
            if (uid[i] < 0x10) uidString += "0";
            uidString += String(uid[i], HEX);
        }
        uidString.toUpperCase();
        return uidString;
    }
    return "";
}

/**
 * @brief Legge la tensione della batteria LiPo 2S su GPIO 1 con multisampling e filtro EMA.
 * Restituisce 0.0f se il partitore o la batteria non sono collegati (< 1.0V).
 */
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
    // Una LiPo 2S non può trovarsi sotto 5.5V: valori inferiori indicano pin non cablato
    if (voltage < 5.50f) {
        return 0.0f;
    }
    return voltage;
}

/**
 * @brief Calcola la percentuale di carica (0..100%) per una batteria LiPo 2S (6.60V .. 8.40V)
 *        usando una curva di interpolazione a tratti. Restituisce -1 se non collegata.
 */
int8_t HardwareManager::readBatteryPercentage() {
    float v = readBatteryVoltage();
    if (v < 5.50f) return -1; // Batteria non collegata (EXT / USB)

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
// CONTROLLO LED WS2812, MACCHINA DEL FUMO E RELÈ 12V
// ============================================================================

void HardwareManager::setLedsColor(uint8_t r, uint8_t g, uint8_t b) {
    if (_lastLedR == r && _lastLedG == g && _lastLedB == b) return;
    _lastLedR = r; _lastLedG = g; _lastLedB = b;

    for (uint16_t i = 0; i < _leds.numPixels(); i++) {
        _leds.setPixelColor(i, _leds.Color(r, g, b));
    }
    _leds.show();
}

void HardwareManager::triggerSmoke(uint8_t pwmValue) {
    analogWrite(PIN_SMOKE, pwmValue);
}

void HardwareManager::setRelay(bool state) {
    digitalWrite(PIN_EXP_CTRL_1, state ? HIGH : LOW);
}