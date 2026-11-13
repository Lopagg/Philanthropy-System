/**
 * @file KioskHardware.cpp
 * @brief Implementazione dei driver hardware, animazioni LED a 50 FPS e auto-recovery RC522.
 */

#include "KioskHardware.h"

// Flag ISR per i pulsanti sul pannello frontale
static volatile bool isrFlagStart = false;
static volatile bool isrFlagEnd   = false;
static volatile bool isrFlagSiren = false;

static volatile unsigned long lastIsrStartMs = 0;
static volatile unsigned long lastIsrEndMs   = 0;
static volatile unsigned long lastIsrSirenMs = 0;

void IRAM_ATTR onStartButtonIsr() {
    unsigned long now = millis();
    if (now - lastIsrStartMs > 800) {
        isrFlagStart = true;
        lastIsrStartMs = now;
    }
}

void IRAM_ATTR onEndButtonIsr() {
    unsigned long now = millis();
    if (now - lastIsrEndMs > 800) {
        isrFlagEnd = true;
        lastIsrEndMs = now;
    }
}

void IRAM_ATTR onSirenButtonIsr() {
    unsigned long now = millis();
    if (now - lastIsrSirenMs > 50) {
        isrFlagSiren = true;
        lastIsrSirenMs = now;
    }
}

// ============================================================================
// COSTRUTTORE E INIZIALIZZAZIONE
// ============================================================================

KioskHardware::KioskHardware()
    : _strip(NUM_LEDS, PIN_LED_STRIP, NEO_GRB + NEO_KHZ800),
      _rfid(PIN_RFID_SS, PIN_RFID_RST),
      _rfidHealthy(false),
      _lastRfidPollMs(0),
      _lastRfidRetryMs(0),
      _lastAssignmentTeam(""),
      _currentWinner(""),
      _animType(""),
      _isAnimating(false),
      _animStartMs(0),
      _lastLedFrameMs(0),
      _sirenActive(false),
      _sirenEndMs(0),
      _buzzerActive(false),
      _buzzerEndMs(0),
      _alphaState(HIGH), _lastAlphaReading(HIGH), _lastAlphaDebounceMs(0),
      _bravoState(HIGH), _lastBravoReading(HIGH), _lastBravoDebounceMs(0) {
    _colorAlpha     = _strip.Color(255, 0, 0);
    _colorBravo     = _strip.Color(0, 255, 0);
    _colorNeutral   = _strip.Color(255, 255, 255);
    _colorWaiting   = _strip.Color(255, 165, 0);
    _colorIdleReady = _strip.Color(0, 100, 255);
    _colorIdleWait  = _strip.Color(100, 0, 150);
    _colorOff       = _strip.Color(0, 0, 0);
    _beepSeq        = {{}, 0, 0, 0, false};
}

void KioskHardware::initialize() {
    pinMode(PIN_BTN_ALPHA, INPUT_PULLUP);
    pinMode(PIN_BTN_BRAVO, INPUT_PULLUP);
    pinMode(PIN_BTN_START, INPUT_PULLUP);
    pinMode(PIN_BTN_END,   INPUT_PULLUP);
    pinMode(PIN_BTN_SIREN, INPUT_PULLUP);

    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    pinMode(PIN_RELAY_SIREN, OUTPUT);
    digitalWrite(PIN_RELAY_SIREN, LOW);

    _strip.begin();
    _strip.setBrightness(150);
    _strip.clear();
    _strip.show();

    attachInterrupt(digitalPinToInterrupt(PIN_BTN_START), onStartButtonIsr, FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_BTN_END),   onEndButtonIsr,   FALLING);
    attachInterrupt(digitalPinToInterrupt(PIN_BTN_SIREN), onSirenButtonIsr, FALLING);

    SPI.begin();
    checkRfidHealth();

    // Triplo beep rapido di avvio
    const int bootSeq[] = {80, 80, 80, 80, 120};
    startBeepSequence(bootSeq, 5);
}

/**
 * @brief Verifica ed eventualmente reinizializza il lettore RC522 senza mai bloccare il sistema.
 */
void KioskHardware::checkRfidHealth() {
    _rfid.PCD_Init();
    delay(4);
    // Aumenta il guadagno d'antenna al massimo (48 dB) per letture più rapide
    _rfid.PCD_SetAntennaGain(_rfid.RxGain_max);

    byte v = _rfid.PCD_ReadRegister(_rfid.VersionReg);
    if (v == 0x00 || v == 0xFF) {
        _rfidHealthy = false;
        Serial.println("[RFID] ATTENZIONE: Modulo RC522 non rilevato sul bus SPI! Nuovo tentativo tra 5s...");
    } else {
        if (!_rfidHealthy) {
            Serial.printf("[RFID] Modulo RC522 operativo (Versione FW: 0x%02X)\n", v);
        }
        _rfidHealthy = true;
    }
}

// ============================================================================
// GESTIONE INPUT PULSANTI E LETTURA RFID
// ============================================================================

bool KioskHardware::pollStartPressed() {
    if (isrFlagStart) {
        isrFlagStart = false;
        return true;
    }
    return false;
}

bool KioskHardware::pollEndPressed() {
    if (isrFlagEnd) {
        isrFlagEnd = false;
        return true;
    }
    return false;
}

bool KioskHardware::isSirenButtonActive() {
    bool held = (digitalRead(PIN_BTN_SIREN) == LOW);
    if (isrFlagSiren || held) {
        isrFlagSiren = false;
        return true;
    }
    return false;
}

bool KioskHardware::isEndButtonHeld() {
    return (digitalRead(PIN_BTN_END) == LOW);
}

bool KioskHardware::pollAlphaPressed() {
    bool confirmed = false;
    int reading = digitalRead(PIN_BTN_ALPHA);
    if (reading != _lastAlphaReading) _lastAlphaDebounceMs = millis();
    if ((millis() - _lastAlphaDebounceMs) > 40) {
        if (reading != _alphaState) {
            _alphaState = reading;
            if (_alphaState == LOW) confirmed = true;
        }
    }
    _lastAlphaReading = reading;
    return confirmed;
}

bool KioskHardware::pollBravoPressed() {
    bool confirmed = false;
    int reading = digitalRead(PIN_BTN_BRAVO);
    if (reading != _lastBravoReading) _lastBravoDebounceMs = millis();
    if ((millis() - _lastBravoDebounceMs) > 40) {
        if (reading != _bravoState) {
            _bravoState = reading;
            if (_bravoState == LOW) confirmed = true;
        }
    }
    _lastBravoReading = reading;
    return confirmed;
}

bool KioskHardware::readCardUID(char* outUidBuffer) {
    // Auto-recovery periodico ogni 5s se l'RC522 è scollegato o ha perso il registro
    if (!_rfidHealthy) {
        if (millis() - _lastRfidRetryMs >= 5000) {
            _lastRfidRetryMs = millis();
            checkRfidHealth();
        }
        return false;
    }

    if (millis() - _lastRfidPollMs < 150) return false;
    _lastRfidPollMs = millis();

    if (_rfid.PICC_IsNewCardPresent() && _rfid.PICC_ReadCardSerial()) {
        for (byte i = 0; i < _rfid.uid.size; i++) {
            sprintf(&outUidBuffer[i * 2], "%02X", _rfid.uid.uidByte[i]);
        }
        outUidBuffer[_rfid.uid.size * 2] = '\0';
        _rfid.PICC_HaltA();
        _rfid.PCD_StopCrypto1();
        return true;
    }
    return false;
}

// ============================================================================
// BUZZER ASINCRONO E SIRENA DA CAMPO
// ============================================================================

void KioskHardware::beepAsync(uint16_t durationMs) {
    _beepSeq.length = 0;
    digitalWrite(PIN_BUZZER, HIGH);
    _buzzerEndMs = millis() + durationMs;
    _buzzerActive = true;
}

void KioskHardware::startBeepSequence(const int* durations, int length) {
    if (length <= 0 || length > 10) return;
    _buzzerActive = false;
    for (int i = 0; i < length; i++) {
        _beepSeq.durations[i] = durations[i];
    }
    _beepSeq.length = length;
    _beepSeq.currentIndex = 0;
    _beepSeq.isOn = true;
    digitalWrite(PIN_BUZZER, HIGH);
    _beepSeq.nextActionMs = millis() + durations[0];
}

void KioskHardware::updateBuzzer() {
    if (_buzzerActive && millis() >= _buzzerEndMs) {
        digitalWrite(PIN_BUZZER, LOW);
        _buzzerActive = false;
    }

    if (_beepSeq.length > 0 && millis() >= _beepSeq.nextActionMs) {
        _beepSeq.currentIndex++;
        if (_beepSeq.currentIndex >= _beepSeq.length) {
            digitalWrite(PIN_BUZZER, LOW);
            _beepSeq.length = 0;
        } else {
            _beepSeq.isOn = !_beepSeq.isOn;
            digitalWrite(PIN_BUZZER, _beepSeq.isOn ? HIGH : LOW);
            _beepSeq.nextActionMs = millis() + _beepSeq.durations[_beepSeq.currentIndex];
        }
    }
}

void KioskHardware::triggerSiren(unsigned long durationMs) {
    unsigned long newEnd = millis() + durationMs;
    if (newEnd > _sirenEndMs) {
        _sirenEndMs = newEnd;
    }
    if (!_sirenActive) {
        _sirenActive = true;
        digitalWrite(PIN_RELAY_SIREN, HIGH);
        Serial.println("[SIREN] Sirena attivata.");
    }
}

void KioskHardware::updateSiren() {
    if (isSirenButtonActive()) {
        if (!_sirenActive) {
            _sirenActive = true;
            digitalWrite(PIN_RELAY_SIREN, HIGH);
            Serial.println("[SIREN] Sirena manuale attivata.");
        }
        unsigned long manualEnd = millis() + 100;
        if (manualEnd > _sirenEndMs) _sirenEndMs = manualEnd;
    } else if (_sirenActive && millis() > _sirenEndMs) {
        _sirenActive = false;
        digitalWrite(PIN_RELAY_SIREN, LOW);
        Serial.println("[SIREN] Sirena disattivata.");
    }
}

// ============================================================================
// MOTORE GRAFICO STRISCIA LED WS2812 (50 FPS)
// ============================================================================

void KioskHardware::setFactionColors(uint8_t aR, uint8_t aG, uint8_t aB, uint8_t bR, uint8_t bG, uint8_t bB) {
    _colorAlpha = _strip.Color(aR, aG, aB);
    _colorBravo = _strip.Color(bR, bG, bB);
}

void KioskHardware::startSwipeAnimation(const String& team, const String& direction) {
    _lastAssignmentTeam = team;
    _animType = direction;
    _animStartMs = millis();
    _isAnimating = true;
}

void KioskHardware::setWinnerState(const String& winner) {
    _currentWinner = winner;
}

void KioskHardware::setMainStripColor(uint32_t color) {
    for (int i = STRIP_START_IDX; i <= STRIP_END_IDX; i++) {
        _strip.setPixelColor(i, color);
    }
}

void KioskHardware::extractRGB(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = (color >> 16) & 0xFF;
    g = (color >>  8) & 0xFF;
    b = color         & 0xFF;
}

void KioskHardware::renderMainStrip(KioskState state, bool allPlayersIn, bool gameRunning, bool gameReady) {
    unsigned long now = millis();

    // 0. Lampeggio Rosso in caso di errore/timeout server durante il check-in
    if (state == STATE_ERROR_FLASH) {
        bool on = ((now / 150) % 2 == 0);
        setMainStripColor(on ? _strip.Color(255, 0, 0) : _colorOff);
        return;
    }

    // 1. Priorità massima: Animazione di Vittoria Fazione
    if (_currentWinner.length() > 0) {
        uint32_t bgColor, fgColor, trailColor;
        uint8_t r, g, b;

        if (_currentWinner == "ALPHA") {
            extractRGB(_colorAlpha, r, g, b);
            bgColor    = _strip.Color(r / 8, g / 8, b / 8);
            fgColor    = _colorAlpha;
            trailColor = _strip.Color((uint8_t)(r / 2.5f), (uint8_t)(g / 2.5f), (uint8_t)(b / 2.5f));
        } else if (_currentWinner == "BRAVO") {
            extractRGB(_colorBravo, r, g, b);
            bgColor    = _strip.Color(r / 8, g / 8, b / 8);
            fgColor    = _colorBravo;
            trailColor = _strip.Color((uint8_t)(r / 2.5f), (uint8_t)(g / 2.5f), (uint8_t)(b / 2.5f));
        } else {
            bgColor    = _strip.Color(30, 30, 0);
            fgColor    = _strip.Color(255, 255, 0);
            trailColor = _strip.Color(100, 100, 0);
        }

        setMainStripColor(bgColor);
        int speed = 70;
        for (int j = 0; j < 3; j++) {
            int pos = ((now / speed) + (j * (STRIP_LENGTH / 3))) % STRIP_LENGTH;
            _strip.setPixelColor(STRIP_START_IDX + pos, fgColor);
            int trailPos = (pos + STRIP_LENGTH - 1) % STRIP_LENGTH;
            _strip.setPixelColor(STRIP_START_IDX + trailPos, trailColor);
        }
        return;
    }

    // 2. Animazione Rapida Swipe IN / OUT (400ms) al passaggio tessera
    if (_isAnimating) {
        if (now - _animStartMs < 400) {
            float progress = (float)(now - _animStartMs) / 400.0f;
            int ledsToLight = (int)(progress * STRIP_LENGTH);

            uint32_t teamColor = _colorNeutral;
            if (_lastAssignmentTeam == "ALPHA") teamColor = _colorAlpha;
            else if (_lastAssignmentTeam == "BRAVO") teamColor = _colorBravo;

            setMainStripColor(_colorOff);
            int center = STRIP_START_IDX + (STRIP_LENGTH / 2);
            int halfLight = (_animType == "IN") ? (ledsToLight / 2) : ((STRIP_LENGTH - ledsToLight) / 2);

            for (int i = 0; i <= halfLight; i++) {
                if (center + i <= STRIP_END_IDX)  _strip.setPixelColor(center + i, teamColor);
                if (center - i >= STRIP_START_IDX) _strip.setPixelColor(center - i, teamColor);
            }
            return;
        } else {
            _isAnimating = false;
        }
    }

    // 3. Partita in corso (Blu profondo)
    if (gameRunning) {
        setMainStripColor(_strip.Color(0, 0, 50));
        return;
    }

    // 4. Stati fuori partita (Attesa assegnazione, Attesa server, Idle Ready/Wait)
    if (state == STATE_WAIT_ASSIGNMENT) {
        setMainStripColor(((now / 250) % 2 == 0) ? _colorWaiting : _colorOff);
    } else if (state == STATE_WAIT_SERVER) {
        setMainStripColor(_strip.Color(50, 50, 50));
    } else {
        setMainStripColor((allPlayersIn && gameReady) ? _colorIdleReady : _colorIdleWait);
    }
}

void KioskHardware::renderButtonLeds(bool allPlayersIn, bool gameRunning, bool gameReady) {
    unsigned long now = millis();

    // LED 0: Sirena
    _strip.setPixelColor(LED_IDX_SIREN, _sirenActive ? _strip.Color(255, 0, 0) : _colorOff);

    // LED 1: Stop / Reset Vittoria
    if (isEndButtonHeld() && gameRunning) {
        _strip.setPixelColor(LED_IDX_END, _strip.Color(255, 0, 0));
    } else if (gameRunning || _currentWinner.length() > 0) {
        _strip.setPixelColor(LED_IDX_END, ((now / 500) % 2 == 0) ? _strip.Color(255, 0, 0) : _colorOff);
    } else {
        _strip.setPixelColor(LED_IDX_END, _colorOff);
    }

    // LED 2: Start
    if (gameRunning) {
        _strip.setPixelColor(LED_IDX_START, _strip.Color(0, 255, 0));
    } else if (!allPlayersIn) {
        _strip.setPixelColor(LED_IDX_START, _strip.Color(255, 0, 0));
    } else if (allPlayersIn && gameReady) {
        _strip.setPixelColor(LED_IDX_START, ((now / 500) % 2 == 0) ? _strip.Color(0, 255, 0) : _colorOff);
    } else {
        _strip.setPixelColor(LED_IDX_START, _colorOff);
    }
}

void KioskHardware::update(KioskState state, bool allPlayersIn, bool gameRunning, bool gameReady) {
    updateBuzzer();
    updateSiren();

    // Limitatore di frame rate a 50 Hz (ogni 20ms) per non bloccare gli interrupt di rete/SPI
    if (millis() - _lastLedFrameMs >= LED_FRAME_INTERVAL_MS) {
        _lastLedFrameMs = millis();
        renderMainStrip(state, allPlayersIn, gameRunning, gameReady);
        renderButtonLeds(allPlayersIn, gameRunning, gameReady);
        _strip.show();
    }
}

void KioskHardware::drawOtaProgress(uint8_t percentage) {
    _strip.clear();
    _strip.setPixelColor(LED_IDX_START, _strip.Color(0, 0, 255));
    int litLeds = (percentage * STRIP_LENGTH) / 100;
    for (int i = 0; i < STRIP_LENGTH; i++) {
        if (i < litLeds) {
            _strip.setPixelColor(STRIP_START_IDX + i, _strip.Color(0, 255, 200));
        } else {
            _strip.setPixelColor(STRIP_START_IDX + i, _strip.Color(5, 5, 15));
        }
    }
    _strip.show();
}

void KioskHardware::showSolidColorAll(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t col = _strip.Color(r, g, b);
    for (int i = 0; i < NUM_LEDS; i++) {
        _strip.setPixelColor(i, col);
    }
    _strip.show();
}