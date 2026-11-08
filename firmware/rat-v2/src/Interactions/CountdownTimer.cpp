/**
 * @file CountdownTimer.cpp
 * @brief Implementazione del conto alla rovescia basato su RTC DS3231 con
 *        annunci vocali MP3 (/audio/sd/min_X, sec_30, sec_10), segnali acustici
 *        di scandaglio e lampeggio LED progressivo.
 */

#include "Interactions/CountdownTimer.h"

extern HardwareManager hardware;

// ============================================================================
// COSTRUTTORE E CONTROLLO STATO TIMER
// ============================================================================

CountdownTimer::CountdownTimer(uint8_t lcdRow)
    : _lcdRow(lcdRow),
      _durationSec(600),
      _lastRtcPollMs(0),
      _lastFastBeepMs(0),
      _cachedRemainingSec(600),
      _lastDisplayedSec(-1),
      _running(false),
      _expired(false) {}

void CountdownTimer::setDuration(uint32_t seconds) {
    _durationSec = seconds;
    if (!_running) {
        _cachedRemainingSec = seconds;
    }
}

uint32_t CountdownTimer::getDuration() const {
    return _durationSec;
}

void CountdownTimer::start() {
    _startTimeRtc = hardware.getRTCTime();
    _lastRtcPollMs = millis();
    _lastFastBeepMs = 0;
    _cachedRemainingSec = _durationSec;
    _lastDisplayedSec = -1;
    _running = true;
    _expired = false;
}

void CountdownTimer::stop() {
    _running = false;
}

void CountdownTimer::forceRedraw() {
    _lastDisplayedSec = -1;
}

uint32_t CountdownTimer::getRemainingSeconds() const {
    if (!_running) return _expired ? 0 : _durationSec;
    return _cachedRemainingSec;
}

// ============================================================================
// AGGIORNAMENTO CONTO ALLA ROVESCIA ED EFFETTI AUDIO/LED
// ============================================================================

bool CountdownTimer::update(bool enableEffects) {
    if (!_running) return _expired;

    // 1. Lettura RTC DS3231 cadenzata ogni 200ms per non saturare il bus I2C
    if (millis() - _lastRtcPollMs >= 200 || _lastDisplayedSec == -1) {
        _lastRtcPollMs = millis();
        TimeSpan elapsed = hardware.getRTCTime() - _startTimeRtc;
        int32_t elapsedSec = elapsed.totalseconds();
        if (elapsedSec < 0) elapsedSec = 0;
        _cachedRemainingSec = (_durationSec > (uint32_t)elapsedSec) ? (_durationSec - (uint32_t)elapsedSec) : 0;
    }

    uint32_t remaining = _cachedRemainingSec;

    // 2. Aggiornamento riga LCD e annunci vocali (solo al cambio effettivo del secondo)
    if ((int)remaining != _lastDisplayedSec) {
        bool isFirstDraw = (_lastDisplayedSec == -1);
        _lastDisplayedSec = (int)remaining;

        int minutes = remaining / 60;
        int seconds = remaining % 60;
        char timeBuffer[10];
        sprintf(timeBuffer, "%02d : %02d", minutes, seconds);
        hardware.printLcdLine(_lcdRow, String(timeBuffer), true);

        // Annunci vocali bomba ad ogni minuto esatto (1..15), a 30s e a 10s
        // (remaining < _durationSec evita di sovrascrivere bomb_armed.mp3 all'istante 0)
        if (enableEffects && !isFirstDraw && remaining > 0 && remaining < _durationSec) {
            if (remaining >= 60 && (remaining % 60 == 0)) {
                if (minutes >= 1 && minutes <= 15) {
                    hardware.playAudio("/audio/sd/min_" + String(minutes) + ".mp3", AUDIO_CH_VOICE);
                } else {
                    hardware.playTone(1500, 150);
                }
            } else if (remaining == 30) {
                hardware.playAudio("/audio/sd/sec_30.mp3", AUDIO_CH_VOICE);
            } else if (remaining == 10) {
                hardware.playAudio("/audio/sd/sec_10.mp3", AUDIO_CH_VOICE);
            }
        }
    }

    // 3. Lampeggio LED e accelerazione sonora negli ultimi 10 secondi (in polifonia sotto la voce)
    if (enableEffects && remaining > 0) {
        if (remaining > 10) {
            if (millis() % 1000 < 100) {
                hardware.setLedsColor(255, 0, 0);
            } else {
                hardware.setLedsColor(0, 0, 0);
            }
        } else {
            uint32_t interval = map(remaining, 10, 1, 900, 180);
            uint16_t freq = map(remaining, 10, 1, 1200, 2200);
            if (millis() - _lastFastBeepMs >= interval) {
                _lastFastBeepMs = millis();
                hardware.playTone(freq, 70);
            }
            if ((millis() - _lastFastBeepMs) < (interval / 2)) {
                hardware.setLedsColor(255, 0, 0);
            } else {
                hardware.setLedsColor(0, 0, 0);
            }
        }
    }

    // 4. Controllo scadenza timer
    if (remaining == 0) {
        _running = false;
        _expired = true;
        return true;
    }
    return false;
}

bool CountdownTimer::isRunning() const {
    return _running;
}

bool CountdownTimer::isExpired() const {
    return _expired;
}