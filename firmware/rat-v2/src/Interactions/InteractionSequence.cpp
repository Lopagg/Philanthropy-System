/**
 * @file InteractionSequence.cpp
 * @brief Implementazione dell'orchestratore di sequenze modulari (HOLD, PIN, KEY, RFID).
 */

#include "Interactions/InteractionSequence.h"
#include "Interactions/HoldButtonInteraction.h"
#include "Interactions/PinInteraction.h"
#include "Interactions/KeyInteraction.h"
#include "Interactions/RfidInteraction.h"

extern HardwareManager hardware;

// ============================================================================
// COSTRUTTORE, DISTRUTTORE E CONFIGURAZIONE JSON
// ============================================================================

InteractionSequence::InteractionSequence(const char* headerTitle, uint8_t holdButtonIndex,
                                         uint16_t sweepStartFreq, uint16_t sweepEndFreq,
                                         uint8_t ledR, uint8_t ledG, uint8_t ledB,
                                         EventType cancelEvent)
    : _headerTitle(headerTitle),
      _holdButtonIndex(holdButtonIndex),
      _sweepStartFreq(sweepStartFreq),
      _sweepEndFreq(sweepEndFreq),
      _ledR(ledR), _ledG(ledG), _ledB(ledB),
      _cancelEvent(cancelEvent),
      _stepCount(0),
      _currentStep(0),
      _result(INTERACTION_RUNNING) {
    for (uint8_t i = 0; i < MAX_STEPS; i++) {
        _steps[i] = nullptr;
    }
}

InteractionSequence::~InteractionSequence() {
    clearSteps();
}

void InteractionSequence::clearSteps() {
    for (uint8_t i = 0; i < MAX_STEPS; i++) {
        if (_steps[i] != nullptr) {
            delete _steps[i];
            _steps[i] = nullptr;
        }
    }
    _stepCount = 0;
    _currentStep = 0;
}

void InteractionSequence::setDefaultHoldStep(uint32_t seconds) {
    clearSteps();
    _steps[0] = new HoldButtonInteraction(_holdButtonIndex, seconds,
                                          _sweepStartFreq, _sweepEndFreq,
                                          _ledR, _ledG, _ledB, _cancelEvent);
    _stepCount = 1;
}

void InteractionSequence::loadFromJson(JsonArray arr, uint32_t fallbackHoldSec) {
    clearSteps();
    for (JsonObject stepObj : arr) {
        if (_stepCount >= MAX_STEPS) break;
        const char* typeStr = stepObj["type"] | "";

        if (strcmp(typeStr, "HOLD") == 0) {
            uint32_t sec = stepObj["val_num"] | fallbackHoldSec;
            _steps[_stepCount++] = new HoldButtonInteraction(_holdButtonIndex, sec,
                                                             _sweepStartFreq, _sweepEndFreq,
                                                             _ledR, _ledG, _ledB, _cancelEvent);
        } else if (strcmp(typeStr, "PIN") == 0) {
            const char* pin = stepObj["val_str"] | "1234";
            _steps[_stepCount++] = new PinInteraction(pin, _cancelEvent);
        } else if (strcmp(typeStr, "KEY") == 0) {
            const char* keyModeStr = stepObj["val_str"] | "K1_ON";
            KeyActionMode mode = KeyInteraction::parseMode(keyModeStr);
            _steps[_stepCount++] = new KeyInteraction(mode, _cancelEvent);
        } else if (strcmp(typeStr, "RFID") == 0) {
            const char* uid = stepObj["val_str"] | "";
            _steps[_stepCount++] = new RfidInteraction(uid, _cancelEvent);
        }
    }

    if (_stepCount == 0) {
        setDefaultHoldStep(fallbackHoldSec);
    }
}

// ============================================================================
// GESTIONE AVVIO SEQUENZA E AVVIO DIRETTO (KEY / RFID)
// ============================================================================

void InteractionSequence::startCurrentStep() {
    String top = String(_headerTitle) + " [" + String(_currentStep + 1) + "/" + String(_stepCount) + "]";
    hardware.printLcdLine(0, top, true);
    _steps[_currentStep]->begin();
}

void InteractionSequence::begin() {
    if (_stepCount == 0) {
        setDefaultHoldStep(5);
    }
    _currentStep = 0;
    _result = INTERACTION_RUNNING;
    startCurrentStep();
}

bool InteractionSequence::canStartWithKeyEvent(const SystemEvent& event) const {
    if (_stepCount == 0 || _steps[0] == nullptr) return false;
    if (_steps[0]->getKeyMask() == 0) return false;
    KeyInteraction* keyStep = static_cast<KeyInteraction*>(_steps[0]);
    return keyStep->matchesDirectStartEvent(event);
}

void InteractionSequence::beginWithDirectKeyEvent(const SystemEvent& event) {
    if (_stepCount == 0) {
        setDefaultHoldStep(5);
    }
    _currentStep = 0;
    _result = INTERACTION_RUNNING;

    String top = String(_headerTitle) + " [1/" + String(_stepCount) + "]";
    hardware.printLcdLine(0, top, true);

    KeyInteraction* keyStep = static_cast<KeyInteraction*>(_steps[0]);
    keyStep->beginFromDirectEvent(event);

    if (keyStep->getResult() == INTERACTION_COMPLETED) {
        _currentStep++;
        if (_currentStep >= _stepCount) {
            hardware.noTone();
            _result = INTERACTION_COMPLETED;
        } else {
            startCurrentStep();
        }
    }
}

bool InteractionSequence::isFirstStepRfid() const {
    return (_stepCount > 0 && _steps[0] != nullptr && _steps[0]->isRfidStep());
}

bool InteractionSequence::tryStartWithRfidEvent(const SystemEvent& event, const char* context) {
    if (!isFirstStepRfid() || event.type != EV_RFID_SCANNED) return false;

    RfidInteraction* rfidStep = static_cast<RfidInteraction*>(_steps[0]);
    bool allowed = rfidStep->isUidAllowed(String(event.stringData));
    RfidInteraction::sendRfidTelemetry(String(event.stringData), allowed, context);

    if (!allowed) {
        hardware.playTone(250, 400);
        return false;
    }

    hardware.playTone(1500, 120);
    _currentStep = 1;
    if (_currentStep >= _stepCount) {
        hardware.noTone();
        _result = INTERACTION_COMPLETED;
    } else {
        _result = INTERACTION_RUNNING;
        startCurrentStep();
    }
    return true;
}

// ============================================================================
// CONTROLLO VINCOLI CHIAVI E AVANZAMENTO STEP
// ============================================================================

bool InteractionSequence::areInitialKeysReady(String& outHint) const {
    uint8_t checkedMask = 0;
    for (uint8_t i = 0; i < _stepCount; i++) {
        if (_steps[i] == nullptr) continue;
        uint8_t mask = _steps[i]->getKeyMask();
        if (mask != 0 && (mask & ~checkedMask) != 0) {
            KeyInteraction* keyStep = static_cast<KeyInteraction*>(_steps[i]);
            if (!keyStep->isInitialStateReady(outHint)) {
                return false;
            }
            checkedMask |= mask;
        }
    }
    return true;
}

bool InteractionSequence::checkPriorConditionsMaintained() {
    for (uint8_t i = 0; i < _currentStep; i++) {
        uint8_t mask = _steps[i]->getKeyMask();
        if (mask == 0) continue;

        // Verifica se uno step successivo (fino a quello attivo incluso) modifica la stessa chiave
        bool overriddenByLaterStep = false;
        for (uint8_t j = i + 1; j <= _currentStep && j < _stepCount; j++) {
            if ((_steps[j]->getKeyMask() & mask) != 0) {
                overriddenByLaterStep = true;
                break;
            }
        }

        if (!overriddenByLaterStep && !_steps[i]->isConditionMaintained()) {
            hardware.noTone();
            hardware.playTone(250, 400);
            _result = INTERACTION_CANCELLED;
            return false;
        }
    }
    return true;
}

void InteractionSequence::handleEvent(const SystemEvent& event) {
    if (_result != INTERACTION_RUNNING || _currentStep >= _stepCount) return;

    if (!checkPriorConditionsMaintained()) return;

    // Se viene scansionata una tessera mentre lo step corrente non è RFID,
    // invia comunque l'UID in telemetria al server per il tracciamento statistiche
    if (event.type == EV_RFID_SCANNED && !_steps[_currentStep]->isRfidStep()) {
        RfidInteraction::sendRfidTelemetry(String(event.stringData), false, "NON_RFID_STEP");
    }

    _steps[_currentStep]->handleEvent(event);
    InteractionResult stepRes = _steps[_currentStep]->getResult();

    if (stepRes == INTERACTION_CANCELLED) {
        hardware.noTone();
        _result = INTERACTION_CANCELLED;
    } else if (stepRes == INTERACTION_COMPLETED) {
        _currentStep++;
        if (_currentStep >= _stepCount) {
            hardware.noTone();
            _result = INTERACTION_COMPLETED;
        } else {
            startCurrentStep();
        }
    }
}

void InteractionSequence::update() {
    if (_result != INTERACTION_RUNNING || _currentStep >= _stepCount) return;

    if (!checkPriorConditionsMaintained()) return;

    _steps[_currentStep]->update();
    InteractionResult stepRes = _steps[_currentStep]->getResult();

    if (stepRes == INTERACTION_CANCELLED) {
        hardware.noTone();
        _result = INTERACTION_CANCELLED;
    } else if (stepRes == INTERACTION_COMPLETED) {
        _currentStep++;
        if (_currentStep >= _stepCount) {
            hardware.noTone();
            _result = INTERACTION_COMPLETED;
        } else {
            startCurrentStep();
        }
    }
}

InteractionResult InteractionSequence::getResult() const {
    return _result;
}

uint8_t InteractionSequence::getProgress() const {
    if (_result == INTERACTION_COMPLETED) return 100;
    if (_result == INTERACTION_CANCELLED || _currentStep >= _stepCount) return 0;
    return _steps[_currentStep]->getProgress();
}