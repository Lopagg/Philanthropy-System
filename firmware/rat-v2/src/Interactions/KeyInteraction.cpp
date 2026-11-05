/**
 * @file KeyInteraction.cpp
 * @brief Implementazione delle 8 modalità operative per i selettori a chiave K1 e K2
 *        (singola chiave, doppia chiave e doppia chiave sincronizzata "Two-Man Rule").
 */

#include "Interactions/KeyInteraction.h"

extern HardwareManager hardware;

// ============================================================================
// COSTRUTTORE E PARSING CONFIGURAZIONE
// ============================================================================

KeyInteraction::KeyInteraction(KeyActionMode mode, EventType cancelEvent)
    : _mode(mode),
      _cancelEvent(cancelEvent),
      _result(INTERACTION_RUNNING),
      _needsResetFirst(false),
      _syncPhase(SYNC_WAIT_READY),
      _syncTimerMs(0),
      _lastK1(false),
      _lastK2(false) {}

KeyActionMode KeyInteraction::parseMode(const char* modeStr) {
    if (!modeStr || strlen(modeStr) == 0) return KEY_MODE_K1_ON;
    if (strcmp(modeStr, "K1_ON") == 0) return KEY_MODE_K1_ON;
    if (strcmp(modeStr, "K1_OFF") == 0) return KEY_MODE_K1_OFF;
    if (strcmp(modeStr, "K2_ON") == 0) return KEY_MODE_K2_ON;
    if (strcmp(modeStr, "K2_OFF") == 0) return KEY_MODE_K2_OFF;
    if (strcmp(modeStr, "BOTH_ON") == 0) return KEY_MODE_BOTH_ON;
    if (strcmp(modeStr, "BOTH_OFF") == 0) return KEY_MODE_BOTH_OFF;
    if (strcmp(modeStr, "BOTH_SYNC_ON") == 0) return KEY_MODE_BOTH_SYNC_ON;
    if (strcmp(modeStr, "BOTH_SYNC_OFF") == 0) return KEY_MODE_BOTH_SYNC_OFF;
    return KEY_MODE_K1_ON;
}

// ============================================================================
// VERIFICA STATO FISICO CHIAVI E VINCOLI
// ============================================================================

bool KeyInteraction::isSyncMode() const {
    return (_mode == KEY_MODE_BOTH_SYNC_ON || _mode == KEY_MODE_BOTH_SYNC_OFF);
}

uint8_t KeyInteraction::getKeyMask() const {
    switch (_mode) {
        case KEY_MODE_K1_ON:
        case KEY_MODE_K1_OFF:
            return 0x01; // Solo K1
        case KEY_MODE_K2_ON:
        case KEY_MODE_K2_OFF:
            return 0x02; // Solo K2
        default:
            return 0x03; // Entrambe (K1 + K2)
    }
}

bool KeyInteraction::isTargetStateReached() const {
    bool k1 = hardware.isKey1Inserted();
    bool k2 = hardware.isKey2Inserted();
    switch (_mode) {
        case KEY_MODE_K1_ON:         return k1;
        case KEY_MODE_K1_OFF:        return !k1;
        case KEY_MODE_K2_ON:         return k2;
        case KEY_MODE_K2_OFF:        return !k2;
        case KEY_MODE_BOTH_ON:
        case KEY_MODE_BOTH_SYNC_ON:  return (k1 && k2);
        case KEY_MODE_BOTH_OFF:
        case KEY_MODE_BOTH_SYNC_OFF: return (!k1 && !k2);
    }
    return false;
}

bool KeyInteraction::isConditionMaintained() const {
    return isTargetStateReached();
}

bool KeyInteraction::matchesDirectStartEvent(const SystemEvent& event) const {
    if (event.type != EV_KEY_1_STATE_CHANGED && event.type != EV_KEY_2_STATE_CHANGED) {
        return false;
    }
    switch (_mode) {
        case KEY_MODE_K1_ON:
            return (event.type == EV_KEY_1_STATE_CHANGED && event.boolValue == true);
        case KEY_MODE_K1_OFF:
            return (event.type == EV_KEY_1_STATE_CHANGED && event.boolValue == false);
        case KEY_MODE_K2_ON:
            return (event.type == EV_KEY_2_STATE_CHANGED && event.boolValue == true);
        case KEY_MODE_K2_OFF:
            return (event.type == EV_KEY_2_STATE_CHANGED && event.boolValue == false);
        case KEY_MODE_BOTH_ON:
        case KEY_MODE_BOTH_SYNC_ON:
            return (event.boolValue == true);
        case KEY_MODE_BOTH_OFF:
        case KEY_MODE_BOTH_SYNC_OFF:
            return (event.boolValue == false);
    }
    return false;
}

bool KeyInteraction::isInitialStateReady(String& outHint) const {
    bool k1 = hardware.isKey1Inserted();
    bool k2 = hardware.isKey2Inserted();
    switch (_mode) {
        case KEY_MODE_K1_ON:
            if (k1) { outHint = "PORTA K1 SU OFF!"; return false; }
            return true;
        case KEY_MODE_K1_OFF:
            if (!k1) { outHint = "INSERISCI K1 (ON)!"; return false; }
            return true;
        case KEY_MODE_K2_ON:
            if (k2) { outHint = "PORTA K2 SU OFF!"; return false; }
            return true;
        case KEY_MODE_K2_OFF:
            if (!k2) { outHint = "INSERISCI K2 (ON)!"; return false; }
            return true;
        case KEY_MODE_BOTH_ON:
            if (k1 && k2) { outHint = "PORTA K1/K2 OFF!"; return false; }
            return true;
        case KEY_MODE_BOTH_OFF:
            if (!k1 && !k2) { outHint = "INSERISCI K1 E K2!"; return false; }
            return true;
        case KEY_MODE_BOTH_SYNC_ON:
            if (k1 || k2) { outHint = "PORTA K1 E K2 OFF!"; return false; }
            return true;
        case KEY_MODE_BOTH_SYNC_OFF:
            if (!k1 || !k2) { outHint = "PORTA K1 E K2 ON!"; return false; }
            return true;
    }
    return true;
}

// ============================================================================
// RENDERING INTERFACCIA SU DISPLAY LCD 20x4
// ============================================================================

void KeyInteraction::renderDualStatusLine() {
    bool k1 = hardware.isKey1Inserted();
    bool k2 = hardware.isKey2Inserted();
    String line = String("K1:[") + (k1 ? "ON " : "OFF") + "]  K2:[" + (k2 ? "ON " : "OFF") + "]";
    hardware.printLcdLine(2, line, true);
}

void KeyInteraction::renderPrompt() {
    if (isSyncMode()) {
        bool targetOn = (_mode == KEY_MODE_BOTH_SYNC_ON);
        if (_syncPhase == SYNC_WAIT_READY) {
            hardware.printLcdLine(1, targetOn ? "PORTA K1 E K2 OFF" : "PORTA K1 E K2 ON", true);
            renderDualStatusLine();
        } else if (_syncPhase == SYNC_ARMED) {
            hardware.printLcdLine(1, targetOn ? "GIRA K1+K2 INSIEME" : "TOGLI K1+K2 INSIEME", true);
            hardware.printLcdLine(2, "SINCRONIA PRONTA", true);
        } else if (_syncPhase == SYNC_FIRST_TURNED) {
            hardware.printLcdLine(1, targetOn ? "GIRA K1+K2 INSIEME" : "TOGLI K1+K2 INSIEME", true);
            hardware.printLcdLine(2, "ATTESA 2a CHIAVE...", true);
        }
        return;
    }

    if (_needsResetFirst) {
        switch (_mode) {
            case KEY_MODE_K1_ON:    hardware.printLcdLine(1, "PORTA PRIMA K1 OFF", true); break;
            case KEY_MODE_K1_OFF:   hardware.printLcdLine(1, "PORTA PRIMA K1 ON", true); break;
            case KEY_MODE_K2_ON:    hardware.printLcdLine(1, "PORTA PRIMA K2 OFF", true); break;
            case KEY_MODE_K2_OFF:   hardware.printLcdLine(1, "PORTA PRIMA K2 ON", true); break;
            case KEY_MODE_BOTH_ON:  hardware.printLcdLine(1, "PORTA K1 E K2 OFF", true); break;
            case KEY_MODE_BOTH_OFF: hardware.printLcdLine(1, "PORTA K1 E K2 ON", true); break;
            default: break;
        }
        if (_mode == KEY_MODE_BOTH_ON || _mode == KEY_MODE_BOTH_OFF) {
            renderDualStatusLine();
        } else {
            hardware.printLcdLine(2, "", false);
        }
        return;
    }

    switch (_mode) {
        case KEY_MODE_K1_ON:
            hardware.printLcdLine(1, "GIRA CHIAVE K1 (ON)", true);
            hardware.printLcdLine(2, "", false);
            break;
        case KEY_MODE_K1_OFF:
            hardware.printLcdLine(1, "ESTRAI CHIAVE K1", true);
            hardware.printLcdLine(2, "", false);
            break;
        case KEY_MODE_K2_ON:
            hardware.printLcdLine(1, "GIRA CHIAVE K2 (ON)", true);
            hardware.printLcdLine(2, "", false);
            break;
        case KEY_MODE_K2_OFF:
            hardware.printLcdLine(1, "ESTRAI CHIAVE K2", true);
            hardware.printLcdLine(2, "", false);
            break;
        case KEY_MODE_BOTH_ON:
            hardware.printLcdLine(1, "INSERISCI K1 E K2", true);
            renderDualStatusLine();
            break;
        case KEY_MODE_BOTH_OFF:
            hardware.printLcdLine(1, "ESTRAI K1 E K2", true);
            renderDualStatusLine();
            break;
        default:
            break;
    }
}

// ============================================================================
// CICLO DI VITA E MACCHINA A STATI (SINCRONIA & SINGOLA CHIAVE)
// ============================================================================

void KeyInteraction::begin() {
    _result = INTERACTION_RUNNING;
    hardware.noTone();
    if (_cancelEvent == EV_BTN_1_PRESSED) {
        hardware.setOledLabels("ANNULLA", "");
    } else if (_cancelEvent == EV_BTN_2_PRESSED) {
        hardware.setOledLabels("", "ANNULLA");
    } else {
        hardware.setOledLabels("", "");
    }

    _lastK1 = hardware.isKey1Inserted();
    _lastK2 = hardware.isKey2Inserted();

    if (isSyncMode()) {
        bool ready = (_mode == KEY_MODE_BOTH_SYNC_ON) ? (!_lastK1 && !_lastK2) : (_lastK1 && _lastK2);
        _syncPhase = ready ? SYNC_ARMED : SYNC_WAIT_READY;
        _needsResetFirst = false;
    } else {
        // Se la chiave si trova già nello stato finale all'avvio dello step, forza prima il reset
        _needsResetFirst = isTargetStateReached();
    }

    renderPrompt();
}

void KeyInteraction::beginFromDirectEvent(const SystemEvent& event) {
    _result = INTERACTION_RUNNING;
    hardware.noTone();
    if (_cancelEvent == EV_BTN_1_PRESSED) {
        hardware.setOledLabels("ANNULLA", "");
    } else if (_cancelEvent == EV_BTN_2_PRESSED) {
        hardware.setOledLabels("", "ANNULLA");
    } else {
        hardware.setOledLabels("", "");
    }

    _lastK1 = hardware.isKey1Inserted();
    _lastK2 = hardware.isKey2Inserted();
    _needsResetFirst = false;

    if (isSyncMode()) {
        _syncPhase = SYNC_FIRST_TURNED;
        _syncTimerMs = millis();
        hardware.playTone(900, 40);
        renderPrompt();
        return;
    }

    if (isTargetStateReached()) {
        hardware.playTone(1500, 120);
        _result = INTERACTION_COMPLETED;
    } else {
        renderPrompt();
    }
}

void KeyInteraction::handleEvent(const SystemEvent& event) {
    if (_result != INTERACTION_RUNNING) return;

    if (_cancelEvent != EV_NONE && event.type == _cancelEvent) {
        _result = INTERACTION_CANCELLED;
        return;
    }
}

void KeyInteraction::update() {
    if (_result != INTERACTION_RUNNING) return;

    bool k1 = hardware.isKey1Inserted();
    bool k2 = hardware.isKey2Inserted();
    bool stateChanged = (k1 != _lastK1 || k2 != _lastK2);
    _lastK1 = k1;
    _lastK2 = k2;

    // --- 1. Gestione Modalità Doppia Chiave Sincronizzata (Two-Man Rule) ---
    if (isSyncMode()) {
        bool targetOn = (_mode == KEY_MODE_BOTH_SYNC_ON);
        bool bothInStartPos = targetOn ? (!k1 && !k2) : (k1 && k2);
        bool bothInTargetPos = targetOn ? (k1 && k2) : (!k1 && !k2);
        bool oneInTargetPos = !bothInStartPos && !bothInTargetPos;

        if (_syncPhase == SYNC_ERROR_COOLDOWN) {
            if (millis() - _syncTimerMs >= 1200) {
                _syncPhase = bothInStartPos ? SYNC_ARMED : SYNC_WAIT_READY;
                renderPrompt();
            }
            return;
        }

        if (_syncPhase == SYNC_WAIT_READY) {
            if (bothInStartPos) {
                _syncPhase = SYNC_ARMED;
                hardware.playTone(900, 40);
                renderPrompt();
            } else if (stateChanged) {
                renderDualStatusLine();
            }
            return;
        }

        if (_syncPhase == SYNC_ARMED) {
            if (bothInTargetPos) {
                hardware.playTone(1500, 120);
                _result = INTERACTION_COMPLETED;
                return;
            }
            if (oneInTargetPos) {
                _syncPhase = SYNC_FIRST_TURNED;
                _syncTimerMs = millis();
                hardware.playTone(900, 40);
                renderPrompt();
            }
            return;
        }

        if (_syncPhase == SYNC_FIRST_TURNED) {
            if (bothInTargetPos) {
                hardware.playTone(1500, 120);
                _result = INTERACTION_COMPLETED;
                return;
            }
            if (bothInStartPos) {
                _syncPhase = SYNC_ARMED;
                renderPrompt();
                return;
            }
            if (millis() - _syncTimerMs > SYNC_WINDOW_MS) {
                hardware.playTone(250, 400);
                hardware.printLcdLine(2, "FUORI SINCRONIA!", true);
                _syncPhase = SYNC_ERROR_COOLDOWN;
                _syncTimerMs = millis();
            }
            return;
        }
    }

    // --- 2. Gestione Modalità Standard (Singola o Doppia Non-Sincronizzata) ---
    if (_needsResetFirst) {
        if (!isTargetStateReached()) {
            _needsResetFirst = false;
            renderPrompt();
        } else if (stateChanged && (_mode == KEY_MODE_BOTH_ON || _mode == KEY_MODE_BOTH_OFF)) {
            renderDualStatusLine();
        }
        return;
    }

    if (isTargetStateReached()) {
        hardware.playTone(1500, 120);
        _result = INTERACTION_COMPLETED;
        return;
    }

    if (stateChanged && (_mode == KEY_MODE_BOTH_ON || _mode == KEY_MODE_BOTH_OFF)) {
        renderDualStatusLine();
    }
}

InteractionResult KeyInteraction::getResult() const {
    return _result;
}

uint8_t KeyInteraction::getProgress() const {
    return 100;
}