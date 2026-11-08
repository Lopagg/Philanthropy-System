/**
 * @file CountdownTimer.cpp
 * @brief Implementazione del conto alla rovescia basato su RTC DS3231 con:
 *        - Annunci vocali MP3 (/audio/sd/min_X, sec_30, sec_10) + toni acustici in polifonia
 *        - Effetto Breathing Rosso continuo quando remaining > 10s
 *        - Flash ad alta potenza con pre/post-blackout allo scadere di ogni minuto e a 30s
 *        - Accelerazione sonora e visiva negli ultimi 10 secondi.
 */

#include "Interactions/CountdownTimer.h"

extern HardwareManager hardware;

enum MilestoneFlashType : uint8_t {
    FLASH_NONE = 0,
    FLASH_MINUTE_LONG, // > 60s : 2 flash lunghi al 100% con stacco di buio prima/durante/dopo
    FLASH_FAST_TRIPLE  // 60s/30s: 3 flash rapidi al 100% ad alto impatto
};

static MilestoneFlashType s_flashType = FLASH_NONE;
static unsigned long      s_flashStartMs = 0;
static bool               s_secondBeepPending = false;
static unsigned long      s_secondBeepTriggerMs = 0;

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
    s_flashType = FLASH_NONE;
    s_secondBeepPending = false;
}

void CountdownTimer::stop() {
    _running = false;
    s_flashType = FLASH_NONE;
    s_secondBeepPending = false;
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

    unsigned long nowMs = millis();

    // 1. Secondo beep asincrono per i traguardi da 60s e 30s (1600Hz 80ms + 100ms pausa + 1600Hz 80ms)
    if (s_secondBeepPending && nowMs >= s_secondBeepTriggerMs) {
        s_secondBeepPending = false;
        hardware.playTone(1600, 80);
    }

    // 2. Lettura RTC DS3231 cadenzata ogni 200ms per non saturare il bus I2C
    if (nowMs - _lastRtcPollMs >= 200 || _lastDisplayedSec == -1) {
        _lastRtcPollMs = nowMs;
        TimeSpan elapsed = hardware.getRTCTime() - _startTimeRtc;
        int32_t elapsedSec = elapsed.totalseconds();
        if (elapsedSec < 0) elapsedSec = 0;
        _cachedRemainingSec = (_durationSec > (uint32_t)elapsedSec) ? (_durationSec - (uint32_t)elapsedSec) : 0;
    }

    uint32_t remaining = _cachedRemainingSec;

    // 3. Aggiornamento riga LCD, annunci vocali MP3 + beep di scandaglio e avvio lampeggi LED
    if ((int)remaining != _lastDisplayedSec) {
        bool isFirstDraw = (_lastDisplayedSec == -1);
        _lastDisplayedSec = (int)remaining;

        int minutes = remaining / 60;
        int seconds = remaining % 60;
        char timeBuffer[10];
        sprintf(timeBuffer, "%02d : %02d", minutes, seconds);
        hardware.printLcdLine(_lcdRow, String(timeBuffer), true);

        if (!isFirstDraw && remaining > 0 && remaining < _durationSec) {
            if (remaining > 60 && (remaining % 60 == 0)) {
                if (minutes <= 15) {
                    hardware.playAudio("/audio/sd/min_" + String(minutes) + ".mp3", AUDIO_CH_VOICE);
                }
                hardware.playTone(1500, 150);
                s_flashType = FLASH_MINUTE_LONG;
                s_flashStartMs = nowMs;
            } else if (remaining == 60) {
                hardware.playAudio("/audio/sd/min_1.mp3", AUDIO_CH_VOICE);
                hardware.playTone(1600, 80);
                s_secondBeepPending = true;
                s_secondBeepTriggerMs = nowMs + 180;
                s_flashType = FLASH_FAST_TRIPLE;
                s_flashStartMs = nowMs;
            } else if (remaining == 30) {
                hardware.playAudio("/audio/sd/sec_30.mp3", AUDIO_CH_VOICE);
                hardware.playTone(1600, 80);
                s_secondBeepPending = true;
                s_secondBeepTriggerMs = nowMs + 180;
                s_flashType = FLASH_FAST_TRIPLE;
                s_flashStartMs = nowMs;
            } else if (remaining == 10) {
                hardware.playAudio("/audio/sd/sec_10.mp3", AUDIO_CH_VOICE);
            }
        }
    }

    // 4. Gestione Effetti LED (Breathing Rosso, Flash ad alta intensità Minuto/30s e Ultimi 10s)
    if (remaining > 0) {
        if (remaining <= 10) {
            uint32_t interval = map(remaining, 10, 1, 900, 180);
            uint16_t freq = map(remaining, 10, 1, 1200, 2200);
            if (nowMs - _lastFastBeepMs >= interval) {
                _lastFastBeepMs = nowMs;
                hardware.playTone(freq, 70);
            }
            if (enableEffects) {
                if ((nowMs - _lastFastBeepMs) < (interval / 2)) {
                    hardware.setLedsColor(255, 0, 0);
                } else {
                    hardware.setLedsColor(0, 0, 0);
                }
            }
        } else if (enableEffects) {
            if (s_flashType == FLASH_MINUTE_LONG) {
                // Sequenza forte minuto: 100ms OFF -> 450ms ON (picco iniziale + Rosso 100%)
                // -> 350ms OFF -> 450ms ON -> 200ms OFF
                unsigned long el = nowMs - s_flashStartMs;
                if (el < 100)        hardware.setLedsColor(0, 0, 0);
                else if (el < 160)   hardware.setLedsColor(255, 90, 60);
                else if (el < 550)   hardware.setLedsColor(255, 0, 0);
                else if (el < 900)   hardware.setLedsColor(0, 0, 0);
                else if (el < 960)   hardware.setLedsColor(255, 90, 60);
                else if (el < 1350)  hardware.setLedsColor(255, 0, 0);
                else if (el < 1550)  hardware.setLedsColor(0, 0, 0);
                else                 s_flashType = FLASH_NONE;
            } else if (s_flashType == FLASH_FAST_TRIPLE) {
                // Sequenza forte 60s e 30s: 3 lampi rapidi al 100% con stacco netto
                unsigned long el = nowMs - s_flashStartMs;
                if (el < 80)         hardware.setLedsColor(0, 0, 0);
                else if (el < 130)   hardware.setLedsColor(255, 110, 70);
                else if (el < 300)   hardware.setLedsColor(255, 0, 0);
                else if (el < 440)   hardware.setLedsColor(0, 0, 0);
                else if (el < 490)   hardware.setLedsColor(255, 110, 70);
                else if (el < 660)   hardware.setLedsColor(255, 0, 0);
                else if (el < 800)   hardware.setLedsColor(0, 0, 0);
                else if (el < 850)   hardware.setLedsColor(255, 110, 70);
                else if (el < 1020)  hardware.setLedsColor(255, 0, 0);
                else if (el < 1180)  hardware.setLedsColor(0, 0, 0);
                else                 s_flashType = FLASH_NONE;
            } else {
                hardware.updateBreathingEffect(255, 0, 0);
            }
        }
    }

    // 5. Controllo scadenza timer
    if (remaining == 0) {
        _running = false;
        _expired = true;
        s_flashType = FLASH_NONE;
        s_secondBeepPending = false;
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