/**
 * @file HoldButtonInteraction.cpp
 * @brief Implementazione dello step di pressione prolungata pulsante (P1 o P2)
 *        con barra di progresso LCD, sweep audio I2S, riempimento progressivo LED
 *        (da Blu a Rosso in innesco, da Rosso a Verde in disinnesco) ed etichette OLED.
 */

#include "Interactions/HoldButtonInteraction.h"

extern HardwareManager hardware;

// ============================================================================
// COSTRUTTORE E DISTRUTTORE
// ============================================================================

HoldButtonInteraction::HoldButtonInteraction(uint8_t buttonIndex, uint32_t durationSec,
                                             uint16_t startFreq, uint16_t endFreq,
                                             uint8_t ledR, uint8_t ledG, uint8_t ledB,
                                             EventType cancelEvent,
                                             const String& actionLabel)
    : _buttonIndex(buttonIndex),
      _durationMs((durationSec > 0 ? durationSec : 1) * 1000),
      _startFreq(startFreq),
      _endFreq(endFreq),
      _ledR(ledR), _ledG(ledG), _ledB(ledB),
      _cancelEvent(cancelEvent),
      _actionLabel(actionLabel.length() > 0 ? actionLabel : (buttonIndex == 1 ? "INNESCA" : "DISINNESCA")),
      _holdingStarted(false),
      _startTime(0),
      _result(INTERACTION_RUNNING) {}

HoldButtonInteraction::~HoldButtonInteraction() {
    hardware.noTone();
}

void HoldButtonInteraction::setActionLabel(const String& label) {
    if (label.length() > 0) {
        _actionLabel = label;
    }
}

// ============================================================================
// METODI DI SUPPORTO E CICLO DI VITA
// ============================================================================

bool HoldButtonInteraction::isTargetButtonPressed() const {
    return (_buttonIndex == 1) ? hardware.isButton1Pressed() : hardware.isButton2Pressed();
}

void HoldButtonInteraction::applyOledLabels(bool isHolding) const {
    if (_buttonIndex == 1) {
        String p2Label = (!isHolding && _cancelEvent == EV_BTN_2_PRESSED) ? "ANNULLA" : "";
        hardware.setOledLabels(_actionLabel, p2Label);
    } else {
        String p1Label = (!isHolding && _cancelEvent == EV_BTN_1_PRESSED) ? "ANNULLA" : "";
        hardware.setOledLabels(p1Label, _actionLabel);
    }
}

void HoldButtonInteraction::begin() {
    _result = INTERACTION_RUNNING;
    hardware.printLcdLine(2, "", false);

    if (isTargetButtonPressed()) {
        _holdingStarted = true;
        _startTime = millis();
        hardware.printLcdLine(1, "TIENI PREMUTO...", true);
        applyOledLabels(true);
        hardware.startToneSweep(_startFreq, _endFreq, _durationMs);
    } else {
        _holdingStarted = false;
        String prompt = (_buttonIndex == 1) ? "TIENI PREMUTO P1" : "TIENI PREMUTO P2";
        hardware.printLcdLine(1, prompt, true);
        applyOledLabels(false);
        hardware.noTone();
    }
}

void HoldButtonInteraction::handleEvent(const SystemEvent& event) {
    if (_result != INTERACTION_RUNNING) return;

    EventType targetPressEvent = (_buttonIndex == 1) ? EV_BTN_1_PRESSED : EV_BTN_2_PRESSED;

    if (!_holdingStarted) {
        if (event.type == targetPressEvent) {
            _holdingStarted = true;
            _startTime = millis();
            hardware.printLcdLine(1, "TIENI PREMUTO...", true);
            applyOledLabels(true);
            hardware.startToneSweep(_startFreq, _endFreq, _durationMs);
            return;
        }

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
            applyOledLabels(true);
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

    // 3. Aggiornamento barra grafica sull'LCD e riempimento progressivo dei LED:
    //    - Innesco (target Rosso 255,0,0)    : i LED passano uno a uno da Blu (0,0,255) a Rosso (255,0,0)
    //    - Disinnesco (target Verde 0,255,0) : i LED passano uno a uno da Rosso (255,0,0) a Verde (0,255,0)
    uint32_t elapsedMs = millis() - _startTime;
    hardware.drawProgressBar(2, elapsedMs, _durationMs);

    uint8_t bgR = 0, bgG = 0, bgB = 0;
    if (_ledR == 255 && _ledG == 0 && _ledB == 0) {
        bgR = 0; bgG = 0; bgB = 255; // Da Blu a Rosso
    } else if (_ledR == 0 && _ledG == 255 && _ledB == 0) {
        bgR = 255; bgG = 0; bgB = 0; // Da Rosso a Verde
    }

    hardware.setLedsProgress(elapsedMs, _durationMs, _ledR, _ledG, _ledB, bgR, bgG, bgB);

    // 4. Controllo completamento durata richiesta
    if (elapsedMs >= _durationMs) {
        hardware.setLedsColor(_ledR, _ledG, _ledB);
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