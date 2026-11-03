/**
 * @file CountdownTimer.h
 * @brief Timer di conto alla rovescia ad alta precisione basato su RTC DS3231.
 *
 * Progettato come componente riutilizzabile (es. timer bomba in S&D o timer di possesso
 * in Domination). Interroga l'RTC hardware ogni 200ms per non saturare il bus I2C,
 * aggiorna la riga LCD dedicata nel formato "MM : SS" solo al cambio del secondo e,
 * se abilitato (enableEffects), gestisce i segnali acustici/visivi di scandaglio
 * (ogni minuto, ai 30 secondi e l'accelerazione progressiva negli ultimi 10 secondi).
 */

#ifndef COUNTDOWN_TIMER_H
#define COUNTDOWN_TIMER_H

#include <Arduino.h>
#include "HardwareManager.h"

class CountdownTimer {
public:
    /**
     * @brief Costruttore del timer di conto alla rovescia.
     * @param lcdRow Indice della riga dell'LCD 20x4 su cui stampare "MM : SS" (default: 3).
     */
    CountdownTimer(uint8_t lcdRow = 3);

    // --- Configurazione e Controllo ---
    void setDuration(uint32_t seconds);
    uint32_t getDuration() const;

    void start();
    void stop();
    void forceRedraw();

    /**
     * @brief Aggiorna il calcolo del tempo residuo, la stampa su LCD e gli effetti opzionali.
     * @param enableEffects true per attivare i beep di scandaglio e il lampeggio LED rosso.
     * @return true nel momento in cui il conto alla rovescia raggiunge 00:00.
     */
    bool update(bool enableEffects = false);

    // --- Interrogazione Stato ---
    uint32_t getRemainingSeconds() const;
    bool isRunning() const;
    bool isExpired() const;

private:
    uint8_t _lcdRow;                // Riga LCD dedicata al timer
    uint32_t _durationSec;          // Durata totale configurata in secondi
    DateTime _startTimeRtc;         // Timestamp RTC catturato all'avvio del timer
    unsigned long _lastRtcPollMs;   // Ultimo istante (millis) di lettura del bus I2C per l'RTC
    unsigned long _lastFastBeepMs;  // Timer non-bloccante per i beep accelerati negli ultimi 10s
    uint32_t _cachedRemainingSec;   // Ultimo valore calcolato dei secondi residui
    int _lastDisplayedSec;          // Ultimo secondo stampato sull'LCD (-1 per forzare il ridisegno)
    bool _running;                  // true se il timer è attivo
    bool _expired;                  // true se il timer è arrivato a zero
};

#endif // COUNTDOWN_TIMER_H