/**
 * @file PinInteraction.cpp
 * @brief Implementazione dello step di inserimento e verifica codice PIN da tastierino 4x4
 *        con feedback LED su pressione tasto ed errore.
 */

#include "Interactions/PinInteraction.h"

extern HardwareManager hardware;

// ============================================================================
// COSTRUTTORE E INIZIALIZZAZIONE
// ============================================================================

PinInteraction::PinInteraction(const char* targetPin, EventType cancelEvent)
    : _cancelEvent(cancelEvent),
      _showingError(false),
      _errorTime(0),
      _result(INTERACTION_RUNNING) {
    strncpy(_targetPin, targetPin ? targetPin : "1234", 15);
    _targetPin[15] = '\0';
    resetBuffer();
}

void PinInteraction::resetBuffer() {
    memset(_inputBuffer, 0, sizeof(_inputBuffer));
}

void PinInteraction::begin() {
    _result = INTERACTION_RUNNING;
    _showingError = false;
    resetBuffer();
    hardware.noTone();
    if (_cancelEvent == EV_BTN_1_PRESSED) {
        hardware.setOledLabels("ANNULLA", "");
    } else if (_cancelEvent == EV_BTN_2_PRESSED) {
        hardware.setOledLabels("", "ANNULLA");
    } else {
        hardware.setOledLabels("", "");
    }
    hardware.printLcdLine(1, "INSERISCI PIN:", true);
    hardware.printLcdLine(2, "", false);
}

// ============================================================================
// GESTIONE EVENTI TASTIERINO E AGGIORNAMENTO STATO
// ============================================================================

void PinInteraction::handleEvent(const SystemEvent& event) {
    if (_result != INTERACTION_RUNNING) return;

    if (_cancelEvent != EV_NONE && event.type == _cancelEvent) {
        _result = INTERACTION_CANCELLED;
        return;
    }

    if (event.type == EV_KEYPAD_PRESSED) {
        if (_showingError) {
            _showingError = false;
        }

        // Tasto '*': cancella l'ultimo carattere digitato
        if (event.charValue == '*') {
            hardware.playTone(500, 35);
            hardware.triggerLedFlash(180, 180, 255, 50, 30, 1);
            uint8_t len = strlen(_inputBuffer);
            if (len > 0) {
                _inputBuffer[len - 1] = '\0';
            }
            hardware.printLcdLine(2, String(_inputBuffer), true);
            return;
        }

        if (!isalnum(event.charValue)) return;

        uint8_t len = strlen(_inputBuffer);
        if (len < 15) {
            _inputBuffer[len] = event.charValue;
            _inputBuffer[len + 1] = '\0';

            if (strlen(_inputBuffer) >= strlen(_targetPin)) {
                if (strcmp(_inputBuffer, _targetPin) == 0) {
                    hardware.playTone(1500, 120);
                    _result = INTERACTION_COMPLETED;
                } else {
                    resetBuffer();
                    hardware.printLcdLine(2, "PIN ERRATO!", true);
                    hardware.playTone(250, 400);
                    hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
                    _showingError = true;
                    _errorTime = millis();
                    _result = INTERACTION_FAILED;
                }
            } else {
                hardware.playTone(700, 35);
                hardware.triggerLedFlash(180, 255, 255, 55, 25, 1);
                hardware.printLcdLine(2, String(_inputBuffer), true);
            }
        }
    }
}

void PinInteraction::update() {
    if (_result != INTERACTION_RUNNING) return;

    if (_showingError && (millis() - _errorTime >= 1200)) {
        _showingError = false;
        hardware.printLcdLine(2, "", false);
    }
}

InteractionResult PinInteraction::getResult() const {
    return _result;
}

uint8_t PinInteraction::getProgress() const {
    return 100;
}