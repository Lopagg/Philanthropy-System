/**
 * @file HoldButtonInteraction.cpp
 * @brief Implementazione dello step di pressione prolungata pulsante (P1 o P2)
 *        con barra di progresso LCD, sweep audio I2S e lampeggio LED.
 */

#include "Interactions/HoldButtonInteraction.h"

extern HardwareManager hardware;

// ============================================================================
// COSTRUTTORE E DISTRUTTORE
// ============================================================================

HoldButtonInteraction::HoldButtonInteraction(uint8_t buttonIndex, uint32_t durationSec,
                                             uint16_t startFreq, uint16_t endFreq,
                                             uint8_t ledR, uint8_t ledG, uint8_t ledB,
                                             EventType cancelEvent)
    : _buttonIndex(buttonIndex),
      _durationMs((durationSec > 0 ? durationSec : 1) * 1000),
      _startFreq(startFreq),
      _endFreq(endFreq),
      _ledR(ledR), _ledG(ledG), _ledB(ledB),
      _cancelEvent(cancelEvent),
      _holdingStarted(false),
      _startTime(0),
      _result(INTERACTION_RUNNING) {}

HoldButtonInteraction::~HoldButtonInteraction() {
    hardware.noTone();
}

// ============================================================================
// METODI DI SUPPORTO E CICLO DI VITA
// ============================================================================

bool HoldButtonInteraction::isTargetButtonPressed() const {
    return (_buttonIndex == 1) ? hardware.isButton1Pressed() : hardware.isButton2Pressed();
}

void HoldButtonInteraction::begin() {
    _result = INTERACTION_RUNNING;
    hardware.printLcdLine(2, "", false);

    if (isTargetButtonPressed()) {
        // Il pulsante è già premuto (es. primo step avviato dalla schermata SAFE/ARMED)
        _holdingStarted = true;
        _startTime = millis();
        hardware.printLcdLine(1, "TIENI PREMUTO...", true);
        if (_buttonIndex == 1) {
            hardware.setOledLabels("INNESCA", "");
        } else {
            hardware.setOledLabels("", "DISINNESCA");
        }
        hardware.startToneSweep(_startFreq, _endFreq, _durationMs);
    } else {
        // Step successivo (es. dopo un PIN): attende che il giocatore prema il pulsante target
        _holdingStarted = false;
        String prompt = (_buttonIndex == 1) ? "TIENI PREMUTO P1" : "TIENI PREMUTO P2";
        hardware.printLcdLine(1, prompt, true);
        if (_buttonIndex == 1) {
            hardware.setOledLabels("INNESCA", "");
        } else {
            hardware.setOledLabels("ANNULLA", "DISINNESCA");
        }
        hardware.noTone();
    }
}

void HoldButtonInteraction::handleEvent(const SystemEvent& event) {
    if (_result != INTERACTION_RUNNING) return;

    EventType targetPressEvent = (_buttonIndex == 1) ? EV_BTN_1_PRESSED : EV_BTN_2_PRESSED;

    if (!_holdingStarted) {
        // Se l'utente preme il pulsante richiesto dallo step, avvia il conteggio e lo sweep
        if (event.type == targetPressEvent) {
            _holdingStarted = true;
            _startTime = millis();
            hardware.printLcdLine(1, "TIENI PREMUTO...", true);
            if (_buttonIndex == 1) {
                hardware.setOledLabels("INNESCA", "");
            } else {
                hardware.setOledLabels("", "DISINNESCA");
            }
            hardware.startToneSweep(_startFreq, _endFreq, _durationMs);
            return;
        }

        // Annulla solo se viene premuto il pulsante opposto di annullamento
        if (_cancelEvent != EV_NONE && _cancelEvent != targetPressEvent && event.type == _cancelEvent) {
            hardware.noTone();
            _result = INTERACTION_CANCELLED;
        }
    }
}

void HoldButtonInteraction::update() {
    if (_result != INTERACTION_RUNNING) return;

    // 1. Fase di attesa inizio pressione (per step intermedi/finali)
    if (!_holdingStarted) {
        if (isTargetButtonPressed()) {
            _holdingStarted = true;
            _startTime = millis();
            hardware.printLcdLine(1, "TIENI PREMUTO...", true);
            if (_buttonIndex == 1) {
                hardware.setOledLabels("INNESCA", "");
            } else {
                hardware.setOledLabels("", "DISINNESCA");
            }
            hardware.startToneSweep(_startFreq, _endFreq, _durationMs);
        }
        return;
    }

    // 2. Verifica mantenimento pressione: il rilascio anticipato annulla la sequenza
    if (!isTargetButtonPressed()) {
        hardware.noTone();
        _result = INTERACTION_CANCELLED;
        return;
    }

    // 3. Aggiornamento barra grafica sull'LCD e lampeggio LED a 2 Hz
    uint32_t elapsedMs = millis() - _startTime;
    hardware.drawProgressBar(2, elapsedMs, _durationMs);

    if (millis() % 500 < 250) {
        hardware.setLedsColor(_ledR, _ledG, _ledB);
    } else {
        hardware.setLedsColor(0, 0, 0);
    }

    // 4. Controllo completamento durata richiesta
    if (elapsedMs >= _durationMs) {
        hardware.noTone();
        _result = INTERACTION_COMPLETED;
    }
}

// ============================================================================
// STATO E TELEMETRIA
// ============================================================================

InteractionResult HoldButtonInteraction::getResult() const {
    return _result;
}

uint8_t HoldButtonInteraction::getProgress() const {
    if (!_holdingStarted) return 100;
    uint32_t elapsedMs = millis() - _startTime;
    uint8_t prog = (elapsedMs * 100) / _durationMs;
    if (prog < 1) prog = 1;
    if (prog > 100) prog = 100;
    return prog;
}