/**
 * @file InteractionSequence.cpp
 * @brief Implementazione dell'orchestratore di sequenze modulari (HOLD, PIN, KEY, RFID)
 *        con gestione delle 3 modalità di errore (RETRY_STEP, RESTART_SEQ, EXPLODE).
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
                                         EventType cancelEvent,
                                         const char* holdActionLabel)
    : _headerTitle(headerTitle),
      _holdButtonIndex(holdButtonIndex),
      _sweepStartFreq(sweepStartFreq),
      _sweepEndFreq(sweepEndFreq),
      _ledR(ledR), _ledG(ledG), _ledB(ledB),
      _cancelEvent(cancelEvent),
      _holdActionLabel(holdActionLabel ? String(holdActionLabel) : (holdButtonIndex == 1 ? "INNESCA" : "DISINNESCA")),
      _stepCount(0),
      _currentStep(0),
      _result(INTERACTION_RUNNING),
      _failMode(SEQ_FAIL_RETRY_STEP),
      _waitingFailDelay(false),
      _failDelayStartMs(0) {
    for (uint8_t i = 0; i < MAX_STEPS; i++) {
        _steps[i] = nullptr;
        _stepTypeTags[i] = 0;
    }
}

InteractionSequence::~InteractionSequence() {
    clearSteps();
}

SequenceFailMode InteractionSequence::parseFailMode(const char* str, bool allowExplode) {
    if (!str) return SEQ_FAIL_RETRY_STEP;
    if (strcmp(str, "EXPLODE") == 0 && allowExplode) return SEQ_FAIL_EXPLODE;
    if (strcmp(str, "RESTART_SEQ") == 0) return SEQ_FAIL_RESTART_SEQ;
    return SEQ_FAIL_RETRY_STEP;
}

void InteractionSequence::clearSteps() {
    for (uint8_t i = 0; i < MAX_STEPS; i++) {
        if (_steps[i] != nullptr) {
            delete _steps[i];
            _steps[i] = nullptr;
        }
        _stepTypeTags[i] = 0;
    }
    _stepCount = 0;
}

void InteractionSequence::setHoldActionLabel(const String& label) {
    _holdActionLabel = label;
    for (uint8_t i = 0; i < _stepCount; i++) {
        if (_steps[i] != nullptr && _stepTypeTags[i] == 1) {
            static_cast<HoldButtonInteraction*>(_steps[i])->setActionLabel(_holdActionLabel);
        }
    }
}

void InteractionSequence::setDefaultHoldStep(uint32_t seconds) {
    clearSteps();
    _steps[0] = new HoldButtonInteraction(_holdButtonIndex, seconds,
                                          _sweepStartFreq, _sweepEndFreq,
                                          _ledR, _ledG, _ledB,
                                          _cancelEvent, _holdActionLabel);
    _stepTypeTags[0] = 1;
    _stepCount = 1;
}

void InteractionSequence::loadFromJson(JsonArray arr, uint32_t fallbackHoldSec) {
    clearSteps();
    for (JsonObject stepObj : arr) {
        if (_stepCount >= MAX_STEPS) break;
        const char* type = stepObj["type"] | "HOLD";

        if (strcmp(type, "HOLD") == 0) {
            uint32_t sec = stepObj["val_num"] | fallbackHoldSec;
            _steps[_stepCount] = new HoldButtonInteraction(_holdButtonIndex, sec,
                                                           _sweepStartFreq, _sweepEndFreq,
                                                           _ledR, _ledG, _ledB,
                                                           _cancelEvent, _holdActionLabel);
            _stepTypeTags[_stepCount] = 1;
            _stepCount++;
        } else if (strcmp(type, "PIN") == 0) {
            const char* pin = stepObj["val_str"] | "1234";
            _steps[_stepCount] = new PinInteraction(pin, _cancelEvent);
            _stepTypeTags[_stepCount] = 0;
            _stepCount++;
        } else if (strcmp(type, "KEY") == 0) {
            const char* modeStr = stepObj["val_str"] | "K1_ON";
            _steps[_stepCount] = new KeyInteraction(KeyInteraction::parseMode(modeStr), _cancelEvent);
            _stepTypeTags[_stepCount] = 0;
            _stepCount++;
        } else if (strcmp(type, "RFID") == 0) {
            const char* uidStr = stepObj["val_str"] | "*";
            _steps[_stepCount] = new RfidInteraction(uidStr, _cancelEvent);
            _stepTypeTags[_stepCount] = 0;
            _stepCount++;
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
    _waitingFailDelay = false;
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
    _waitingFailDelay = false;
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
    _waitingFailDelay = false;

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
            hardware.triggerLedFlash(255, 255, 255, 90, 70, 2);
            startCurrentStep();
        }
    }
}

bool InteractionSequence::isFirstStepRfid() const {
    return (_stepCount > 0 && _steps[0] != nullptr && _steps[0]->isRfidStep());
}

bool InteractionSequence::canStartWithRfidEvent(const SystemEvent& event) const {
    if (!isFirstStepRfid() || event.type != EV_RFID_SCANNED) return false;
    RfidInteraction* rfidStep = static_cast<RfidInteraction*>(_steps[0]);
    return rfidStep->isUidAllowed(String(event.stringData));
}

bool InteractionSequence::tryStartWithRfidEvent(const SystemEvent& event, const char* context) {
    if (!isFirstStepRfid() || event.type != EV_RFID_SCANNED) return false;

    RfidInteraction* rfidStep = static_cast<RfidInteraction*>(_steps[0]);
    bool allowed = rfidStep->isUidAllowed(String(event.stringData));
    RfidInteraction::sendRfidTelemetry(String(event.stringData), allowed, context);

    if (!allowed) {
        hardware.playTone(250, 400);
        hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
        return false;
    }

    hardware.playTone(1500, 120);
    _currentStep = 1;
    _waitingFailDelay = false;
    if (_currentStep >= _stepCount) {
        hardware.noTone();
        _result = INTERACTION_COMPLETED;
    } else {
        _result = INTERACTION_RUNNING;
        hardware.triggerLedFlash(255, 255, 255, 90, 70, 2);
        startCurrentStep();
    }
    return true;
}

// ============================================================================
// CONTROLLO VINCOLI CHIAVI, GESTIONE ERRORI E AVANZAMENTO STEP
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
            hardware.triggerLedFlash(255, 80, 0, 90, 90, 4);
            if (_failMode == SEQ_FAIL_EXPLODE) {
                _result = INTERACTION_FAILED;
            } else {
                _result = INTERACTION_CANCELLED;
            }
            return false;
        }
    }
    return true;
}

void InteractionSequence::handleStepFailure(bool isHoldStep) {
    hardware.noTone();

    // 1. Modalità BOMBA ESPLODE (solo disinnesco): innesca l'esplosione a latenza zero!
    if (_failMode == SEQ_FAIL_EXPLODE) {
        _result = INTERACTION_FAILED;
        return;
    }

    // 2. Se l'utente ha rilasciato il pulsante HOLD al primo step (#1), torna subito allo stato base
    if (isHoldStep && _currentStep == 0) {
        _result = INTERACTION_CANCELLED;
        return;
    }

    // 3. Per gli altri errori (PIN errato, RFID errato, Chiavi fuori sincronia, o rilascio HOLD in step > 0):
    //    attende 1.2s mostrando l'errore e il lampeggio LED ambra, poi applica RETRY_STEP o RESTART_SEQ
    if (isHoldStep) {
        hardware.printLcdLine(2, (_failMode == SEQ_FAIL_RESTART_SEQ) ? "ERRORE! DA CAPO" : "RILASCIATO PRIMA!", true);
    } else if (_failMode == SEQ_FAIL_RESTART_SEQ) {
        hardware.printLcdLine(1, "SEQUENZA FALLITA!", true);
    }

    _waitingFailDelay = true;
    _failDelayStartMs = millis();
}

void InteractionSequence::handleEvent(const SystemEvent& event) {
    if (_result != INTERACTION_RUNNING || _currentStep >= _stepCount) return;

    if (_waitingFailDelay) {
        if (_cancelEvent != EV_NONE && event.type == _cancelEvent) {
            _waitingFailDelay = false;
            _result = INTERACTION_CANCELLED;
        }
        return;
    }

    if (!checkPriorConditionsMaintained()) return;

    if (event.type == EV_RFID_SCANNED && !_steps[_currentStep]->isRfidStep()) {
        RfidInteraction::sendRfidTelemetry(String(event.stringData), false, "NON_RFID_STEP");
    }

    _steps[_currentStep]->handleEvent(event);
    InteractionResult stepRes = _steps[_currentStep]->getResult();

    if (stepRes == INTERACTION_CANCELLED) {
        hardware.noTone();
        _result = INTERACTION_CANCELLED;
    } else if (stepRes == INTERACTION_FAILED) {
        handleStepFailure(_stepTypeTags[_currentStep] == 1);
    } else if (stepRes == INTERACTION_COMPLETED) {
        _currentStep++;
        if (_currentStep >= _stepCount) {
            hardware.noTone();
            _result = INTERACTION_COMPLETED;
        } else {
            hardware.triggerLedFlash(255, 255, 255, 90, 70, 2);
            startCurrentStep();
        }
    }
}

void InteractionSequence::update() {
    if (_result != INTERACTION_RUNNING || _currentStep >= _stepCount) return;

    if (!checkPriorConditionsMaintained()) return;

    // Gestione del ritardo di 1.2s dopo un errore per far leggere il messaggio sull'LCD
    if (_waitingFailDelay) {
        hardware.updateLedFlash();
        if (millis() - _failDelayStartMs >= 1200) {
            _waitingFailDelay = false;
            if (_failMode == SEQ_FAIL_RESTART_SEQ) {
                _result = INTERACTION_CANCELLED; // Riporta a SD_SAFE / SD_ARMED per ricominciare dallo step #1
            } else {
                startCurrentStep(); // SEQ_FAIL_RETRY_STEP: ripete solo lo step corrente!
            }
        }
        return;
    }

    _steps[_currentStep]->update();
    InteractionResult stepRes = _steps[_currentStep]->getResult();

    if (stepRes == INTERACTION_CANCELLED) {
        hardware.noTone();
        _result = INTERACTION_CANCELLED;
        return;
    } else if (stepRes == INTERACTION_FAILED) {
        handleStepFailure(_stepTypeTags[_currentStep] == 1);
        return;
    } else if (stepRes == INTERACTION_COMPLETED) {
        _currentStep++;
        if (_currentStep >= _stepCount) {
            hardware.noTone();
            _result = INTERACTION_COMPLETED;
        } else {
            hardware.triggerLedFlash(255, 255, 255, 90, 70, 2);
            startCurrentStep();
        }
        return;
    }

    if (hardware.updateLedFlash()) {
        return;
    }

    bool isActivelyHolding = false;
    if (_stepTypeTags[_currentStep] == 1) {
        isActivelyHolding = (_holdButtonIndex == 1) ? hardware.isButton1Pressed() : hardware.isButton2Pressed();
    }

    if (!isActivelyHolding) {
        bool blinkOn = ((millis() % 700) < 380);
        if (blinkOn) {
            hardware.setLedsColor(_ledR, _ledG, _ledB);
        } else if (_stepCount > 1 && _currentStep > 0) {
            hardware.setLedsProgress(_currentStep, _stepCount, _ledR, _ledG, _ledB, 0, 0, 0);
        } else {
            hardware.setLedsColor(0, 0, 0);
        }
    }
}

InteractionResult InteractionSequence::getResult() const {
    return _result;
}

uint8_t InteractionSequence::getProgress() const {
    if (_result == INTERACTION_COMPLETED) return 100;
    if (_result == INTERACTION_CANCELLED || _result == INTERACTION_FAILED || _currentStep >= _stepCount) return 0;
    return _steps[_currentStep]->getProgress();
}