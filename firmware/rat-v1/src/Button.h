// src/Button.h

/**
 * @file Button.h
 * @brief Dichiarazione della classe Button per la gestione di un pulsante fisico.
 * @details Questa classe fornisce una logica anti-rimbalzo (debounce) per leggere
 * in modo affidabile lo stato di un pulsante, distinguendo tra lo stato
 * continuo (premuto/rilasciato) e l'evento singolo (appena premuto/appena rilasciato).
 */

#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

class Button {
public:
    /**
     * @brief Costruttore della classe Button.
     * @param pin Il numero del pin GPIO a cui è collegato il pulsante.
     */
    Button(int pin, bool useInternalPullup = true);

    /**
     * @brief Inizializza il pin del pulsante. Da chiamare nella funzione setup().
     */
    void init();

    /**
     * @brief Aggiorna lo stato del pulsante.
     * @details Questa funzione deve essere chiamata ad ogni ciclo del loop() principale
     * per leggere lo stato fisico del pin e applicare la logica di debounce.
     */
    void update();

    /**
     * @brief Controlla se il pulsante è attualmente tenuto premuto.
     * @return true se il pulsante è premuto, false altrimenti true.
     * @note Rappresenta lo STATO continuo del pulsante.
     */
    bool isPressed();

    /**
     * @brief Controlla se il pulsante è stato appena premuto.
     * @return true solo per il singolo ciclo di loop in cui avviene la transizione
     * da non premuto a premuto.
     * @note Rappresenta l'EVENTO della pressione, ideale per i menu.
     */
    bool wasPressed();

    /**
     * @brief Controlla se il pulsante è stato appena rilasciato.
     * @return true solo per il singolo ciclo di loop in cui avviene la transizione
     * da premuto a non premuto.
     * @note Rappresenta l'EVENTO del rilascio.
     */
    bool wasReleased();

private:
    int _pin;               // Pin a cui è collegato il pulsante.
    bool _useInternalPullup; // Usa la resistenza di pull-up interna?
    int _state;             // Stato "pulito" (debounced) del pulsante.
    int _lastReading;       // Ultima lettura grezza dal pin.
    unsigned long _lastDebounceTime; // Timestamp dell'ultimo cambio di stato.
    unsigned long _debounceDelay;    // Tempo di attesa per considerare stabile una lettura.
    bool _wasPressedFlag;   // Flag per l'evento wasPressed().
    bool _wasReleasedFlag;  // Flag per l'evento wasReleased().
};

#endif // BUTTON_H