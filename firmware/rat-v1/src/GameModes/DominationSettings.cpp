/**
 * @file DominationSettings.cpp
 * @brief Implementazione della classe DominationSettings.
 */

#include "DominationSettings.h"

/**
 * @brief Costruttore. Carica automaticamente i parametri all'avvio.
 */
DominationSettings::DominationSettings() {
    loadParameters();
}

/**
 * @brief Salva tutti i parametri nella memoria flash.
 * @details Usa lo spazio dei nomi "dom-settings".
 */
void DominationSettings::saveParameters() {
    preferences.begin("dom-settings", false);
    preferences.putInt("gameDuration", _gameDuration);
    preferences.putInt("captureTime", _captureTime);
    preferences.putInt("countdown", _countdownDuration);
    preferences.end();
    Serial.println("Parametri Dominio salvati.");
}

/**
 * @brief Carica tutti i parametri dalla memoria flash.
 * @details Imposta valori di default se non presenti: 
 * - Game Duration: 30 minuti
 * - Capture Time: 10 secondi
 * - Countdown: 10 secondi
 */
void DominationSettings::loadParameters() {
    preferences.begin("dom-settings", true);
    _gameDuration = preferences.getInt("gameDuration", 15);
    _captureTime = preferences.getInt("captureTime", 10);
    _countdownDuration = preferences.getInt("countdown", 10);
    preferences.end();
    Serial.println("Parametri Dominio caricati.");
}

// --- Implementazione Getter ---
int DominationSettings::getGameDuration() { return _gameDuration; }
int DominationSettings::getCaptureTime() { return _captureTime; }
int DominationSettings::getCountdownDuration() { return _countdownDuration; }

// --- Implementazione Setter ---
void DominationSettings::setGameDuration(int minutes) { _gameDuration = minutes; }
void DominationSettings::setCaptureTime(int seconds) { _captureTime = seconds; }
void DominationSettings::setCountdownDuration(int seconds) { _countdownDuration = seconds; }