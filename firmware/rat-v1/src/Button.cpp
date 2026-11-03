// src/Button.cpp

/**
 * @file Button.cpp
 * @brief Implementazione della classe Button.
 */

#include "Button.h"

// Imposta un ritardo anti-rimbalzo predefinito di 50 millisecondi.
const unsigned long DEFAULT_DEBOUNCE_DELAY = 50;

/**
 * @brief Costruttore: inizializza le variabili membro con i loro valori di partenza.
 * @param pin Il pin del pulsante passato durante la creazione dell'oggetto.
 */
Button::Button(int pin, bool useInternalPullup) :
    _pin(pin),
    _useInternalPullup(useInternalPullup),
    _state(HIGH), // Buttons are typically pull-up, so HIGH when not pressed
    _lastReading(HIGH),
    _lastDebounceTime(0),
    _debounceDelay(DEFAULT_DEBOUNCE_DELAY),
    _wasPressedFlag(false),
    _wasReleasedFlag(false)
{}

/**
 * @brief Inizializza il pin hardware.
 * @details Configura il pin come input con una resistenza di pull-up interna attivata.
 * Questo significa che il pin leggerà HIGH quando il pulsante non è premuto
 * e LOW quando viene premuto (collegandolo a GND).
 */
void Button::init() {
    if (_useInternalPullup) {
        pinMode(_pin, INPUT_PULLUP);
    } else {
        pinMode(_pin, INPUT);
    }
    _state = digitalRead(_pin);
    _lastReading = _state;
}

/**
 * @brief Funzione principale che aggiorna lo stato del pulsante, applicando la logica di debounce.
 */
void Button::update() {
    int reading;

    // Se stiamo usando un pin problematico (ADC1), usiamo analogRead
    if (_pin == 34 || _pin == 35) {
        // Se la lettura analogica è alta (>2000), consideriamo il pin HIGH (1)
        // altrimenti lo consideriamo LOW (0). 4095 è il massimo.
        reading = (analogRead(_pin) > 2000) ? HIGH : LOW;
    } else {
        // Altrimenti, usiamo il digitalRead standard per tutti gli altri pin
        reading = digitalRead(_pin);
    }

    if (reading != _lastReading) {
        _lastDebounceTime = millis();
    }

    if ((millis() - _lastDebounceTime) > _debounceDelay) {
        if (reading != _state) {
            if (reading == LOW && _state == HIGH) {
                _wasPressedFlag = true;
            }
            else if (reading == HIGH && _state == LOW) {
                _wasReleasedFlag = true;
            }
            _state = reading;
        }
    }
    
    // Resetta i flag se lo stato è stabile
    // Questa parte è stata semplificata per essere più chiara
    if (reading == _state) {
        if( (millis() - _lastDebounceTime) > _debounceDelay) {
            // Solo dopo il debounce, se non c'è stato un cambio di stato, resetta i flag
        }
    } else {
        // Se c'è un rimbalzo, non considerarlo un evento
        _wasPressedFlag = false;
        _wasReleasedFlag = false;
    }

    _lastReading = reading;
}

/**
 * @brief Ritorna lo stato stabile attuale del pulsante.
 */
bool Button::isPressed() {
    return _state == LOW;
}

/**
 * @brief Ritorna 'true' solo per un ciclo se è stato appena rilevato un evento di pressione.
 * @details Il flag viene letto e subito dopo resettato, in modo che la chiamata successiva
 * nello stesso loop (o nei successivi) ritorni 'false' fino a una nuova pressione.
 */
bool Button::wasPressed() {
    if (_wasPressedFlag) {
        _wasPressedFlag = false; // Il flag si auto-resetta dopo la lettura
        return true;
    }
    return false;
}

/**
 * @brief Ritorna 'true' solo per un ciclo se è stato appena rilevato un evento di rilascio.
 */
bool Button::wasReleased() {
    if (_wasReleasedFlag) {
        _wasReleasedFlag = false; // Il flag si auto-resetta dopo la lettura
        return true;
    }
    return false;
}