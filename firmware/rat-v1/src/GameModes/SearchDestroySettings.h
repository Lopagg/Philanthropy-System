// src/GameModes/SearchDestroySettings.h

/**
 * @file SearchDestroySettings.h
 * @brief Dichiarazione della classe SearchDestroySettings per la gestione delle impostazioni della modalità Cerca e Distruggi.
 */

#ifndef SEARCH_DESTROY_SETTINGS_H
#define SEARCH_DESTROY_SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>

/**
 * @class SearchDestroySettings
 * @brief Gestisce il salvataggio e il caricamento delle impostazioni per la modalità Cerca e Distruggi.
 * @details Utilizza la libreria Preferences dell'ESP32 per memorizzare i dati in modo persistente
 * nella memoria flash non volatile.
 */
class SearchDestroySettings {
public:
    /**
     * @brief Costruttore. Chiama automaticamente loadParameters() per caricare le impostazioni salvate.
     */
    SearchDestroySettings();

    /**
     * @brief Salva tutte le impostazioni correnti nella memoria non volatile.
     * @details Viene chiamata quando si esce dalla modalità di gioco per rendere persistenti le modifiche.
     */
    void saveParameters();
    /**
     * @brief Carica le impostazioni dalla memoria non volatile.
     * @details Se non trova valori salvati, utilizza dei valori di default predefiniti.
     * Viene chiamata automaticamente alla creazione dell'oggetto.
     */
    void loadParameters();

    // --- Metodi Getter (per leggere i valori delle impostazioni) ---
    int getBombTime();
    String getArmingPin();
    String getDisarmingPin();
    int getArmingTime();
    int getDefuseTime();
    bool getUseArmingPin();
    bool getUseDisarmingPin();
    int getGameDuration();

    // --- Metodi Setter (per modificare i valori delle impostazioni) ---
    void setBombTime(int time);
    void setArmingPin(const String& pin);
    void setDisarmingPin(const String& pin);
    void setArmingTime(int time);
    void setDefuseTime(int time);
    void setUseArmingPin(bool value);
    void setUseDisarmingPin(bool value);
    void setGameDuration(int minutes);

private:
    // Variabili membro private che contengono i valori delle impostazioni.
    int _bombTime;
    String _armingPin;
    String _disarmingPin;
    int _armingTime;
    int _defuseTime;
    bool _useArmingPin;
    bool _useDisarmingPin;
    int _gameDuration;

    // Oggetto della libreria Preferences per interagire con la memoria flash.
    Preferences preferences;
};

#endif // SEARCH_DESTROY_SETTINGS_H