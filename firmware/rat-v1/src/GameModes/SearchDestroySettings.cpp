// src/GameModes/SearchDestroySettings.cpp

/**
 * @file SearchDestroySettings.cpp
 * @brief Implementazione della classe SearchDestroySettings.
 */

#include "SearchDestroySettings.h"

/**
 * @brief Costruttore. Carica automaticamente i parametri all'avvio.
 */
SearchDestroySettings::SearchDestroySettings() {
    loadParameters();
}

/**
 * @brief Salva tutti i parametri nella memoria flash.
 * @details Usa uno "spazio dei nomi" ("sd-settings") per evitare conflitti con altre
 * impostazioni salvate. 'false' indica che la memoria viene aperta in modalità di scrittura.
 */
void SearchDestroySettings::saveParameters() {
    preferences.begin("sd-settings", false);
    preferences.putInt("bombTime", _bombTime);
    preferences.putString("armingPin", _armingPin);
    preferences.putString("disarmingPin", _disarmingPin);
    preferences.putInt("armingTime", _armingTime);
    preferences.putInt("defuseTime", _defuseTime);
    preferences.putBool("useArmPin", _useArmingPin);
    preferences.putBool("useDisarmPin", _useDisarmingPin);
    preferences.putInt("gameDuration", _gameDuration);
    preferences.end();
    Serial.println("Parametri salvati.");
}

/**
 * @brief Carica tutti i parametri dalla memoria flash.
 * @details 'true' indica che la memoria viene aperta in modalità di sola lettura.
 * Per ogni parametro, se non viene trovato un valore salvato, viene utilizzato
 * un valore di default (es. getInt("bombTime", 10) imposta 10 minuti se non c'è nulla).
 */
void SearchDestroySettings::loadParameters() {
    preferences.begin("sd-settings", true);
    _bombTime = preferences.getInt("bombTime", 10);
    _armingPin = preferences.getString("armingPin", "1234");
    _disarmingPin = preferences.getString("disarmingPin", "4321");
    _armingTime = preferences.getInt("armingTime", 5);
    _defuseTime = preferences.getInt("defuseTime", 10);
    _useArmingPin = preferences.getBool("useArmPin", true);
    _useDisarmingPin = preferences.getBool("useDisarmPin", true);
    _gameDuration = preferences.getInt("gameDuration", 15);
    preferences.end();
    Serial.println("Parametri caricati.");
}

// --- Implementazione dei Metodi Getter ---
// Queste funzioni semplicemente restituiscono il valore della variabile privata corrispondente.
int SearchDestroySettings::getBombTime() { return _bombTime; }
String SearchDestroySettings::getArmingPin() { return _armingPin; }
String SearchDestroySettings::getDisarmingPin() { return _disarmingPin; }
int SearchDestroySettings::getArmingTime() { return _armingTime; }
int SearchDestroySettings::getDefuseTime() { return _defuseTime; }
bool SearchDestroySettings::getUseArmingPin() { return _useArmingPin; }
bool SearchDestroySettings::getUseDisarmingPin() { return _useDisarmingPin; }
int SearchDestroySettings::getGameDuration() { return _gameDuration; }

// --- Implementazione dei Metodi Setter ---
// Queste funzioni permettono di modificare il valore della variabile privata corrispondente.
void SearchDestroySettings::setBombTime(int time) { _bombTime = time; }
void SearchDestroySettings::setArmingPin(const String& pin) { _armingPin = pin; }
void SearchDestroySettings::setDisarmingPin(const String& pin) { _disarmingPin = pin; }
void SearchDestroySettings::setArmingTime(int time) { _armingTime = time; }
void SearchDestroySettings::setDefuseTime(int time) { _defuseTime = time; }
void SearchDestroySettings::setUseArmingPin(bool value) { _useArmingPin = value; }
void SearchDestroySettings::setUseDisarmingPin(bool value) { _useDisarmingPin = value; }
void SearchDestroySettings::setGameDuration(int minutes) { _gameDuration = minutes; }