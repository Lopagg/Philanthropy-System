/**
 * @file DominationSettings.h
 * @brief Dichiarazione della classe DominationSettings per la gestione delle impostazioni della modalità Dominio.
 */

#ifndef DOMINATION_SETTINGS_H
#define DOMINATION_SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>

/**
 * @class DominationSettings
 * @brief Gestisce il salvataggio e il caricamento delle impostazioni per la modalità Dominio.
 * @details Utilizza la libreria Preferences dell'ESP32 per memorizzare i dati in modo persistente.
 */
class DominationSettings {
public:
    /**
     * @brief Costruttore. Chiama automaticamente loadParameters() per caricare le impostazioni salvate.
     */
    DominationSettings();

    /**
     * @brief Salva tutte le impostazioni correnti nella memoria non volatile.
     */
    void saveParameters();
    
    /**
     * @brief Carica le impostazioni dalla memoria non volatile.
     * @details Se non trova valori salvati, utilizza dei valori di default predefiniti.
     */
    void loadParameters();

    // --- Metodi Getter ---
    int getGameDuration();
    int getCaptureTime();
    int getCountdownDuration();

    // --- Metodi Setter ---
    void setGameDuration(int minutes);
    void setCaptureTime(int seconds);
    void setCountdownDuration(int seconds);

private:
    // Variabili membro private
    int _gameDuration;      // Durata totale della partita in minuti
    int _captureTime;       // Tempo necessario per conquistare un punto (secondi)
    int _countdownDuration; // Conto alla rovescia prima dell'inizio (secondi)

    Preferences preferences;
};

#endif // DOMINATION_SETTINGS_H