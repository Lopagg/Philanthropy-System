/**
 * @file main.cpp
 * @brief Macchina a stati principale del Tactical Kiosk V1 (RFID Check-In, Controllo Partita e Sirena).
 */

#include <Arduino.h>
#include "KioskConfig.h"
#include "KioskHardware.h"
#include "KioskNetwork.h"

KioskHardware hardware;
KioskNetwork  network(hardware);

KioskState currentState = STATE_WAIT_TAG;
char currentUID[20]     = "";
unsigned long stateTimerMs = 0;

bool serverAllPlayersIn = false;
bool serverGameRunning  = false;
bool serverGameReady    = false;

// ============================================================================
// GESTIONE RISPOSTE E SINCRONIZZAZIONE DAL SERVER
// ============================================================================

void enterCooldown() {
    currentUID[0] = '\0';
    currentState  = STATE_COOLDOWN;
    stateTimerMs  = millis();
}

void handleKioskReply(const char* action) {
    if (strcmp(action, "WAIT_ASSIGN") == 0) {
        // La richiesta di assegnazione vale solo se la tessera è stata letta fisicamente sul Kiosk
        if (currentState == STATE_WAIT_SERVER) {
            currentState = STATE_WAIT_ASSIGNMENT;
            stateTimerMs = millis();
            const int seq[] = {50, 50, 50};
            hardware.startBeepSequence(seq, 3);
        }
        return;
    }

    const int confirmSeq[] = {100, 50, 300};
    if (strstr(action, "IN_ALPHA") != nullptr) {
        hardware.startSwipeAnimation("ALPHA", "IN");
        hardware.startBeepSequence(confirmSeq, 3);
    } else if (strstr(action, "FUORI_ALPHA") != nullptr) {
        hardware.startSwipeAnimation("ALPHA", "OUT");
        hardware.startBeepSequence(confirmSeq, 3);
    } else if (strstr(action, "IN_BRAVO") != nullptr) {
        hardware.startSwipeAnimation("BRAVO", "IN");
        hardware.startBeepSequence(confirmSeq, 3);
    } else if (strstr(action, "FUORI_BRAVO") != nullptr) {
        hardware.startSwipeAnimation("BRAVO", "OUT");
        hardware.startBeepSequence(confirmSeq, 3);
    } else if (strstr(action, "IN_ALL") != nullptr) {
        hardware.startSwipeAnimation("NEUTRAL", "IN");
        hardware.startBeepSequence(confirmSeq, 3);
    } else {
        hardware.beepAsync(500);
    }

    // Se il Kiosk non è impegnato nell'assegnazione di una tessera locale, mostra il risultato
    if (currentState != STATE_WAIT_ASSIGNMENT) {
        currentState = STATE_SHOW_RESULT;
        stateTimerMs = millis();
    }
}

void onServerCommand(const JsonDocument& doc) {
    const char* cmd = doc["cmd"] | "";

    // Accetta KIOSK_REPLY sia da scansione locale che da cambio stato remoto (Web / Mobile NFC)
    if (strcmp(cmd, "KIOSK_REPLY") == 0) {
        handleKioskReply(doc["action"] | "");
    }
    else if (strcmp(cmd, "SIREN_LONG") == 0) {
        unsigned long dur = doc["duration_ms"] | SIREN_LONG_MS;
        hardware.triggerSiren(dur);
    }
    else if (strcmp(cmd, "SIREN_SHORT") == 0) {
        unsigned long dur = doc["duration_ms"] | SIREN_SHORT_MS;
        hardware.triggerSiren(dur);
    }
    else if (strcmp(cmd, "SET_FACTIONS") == 0 || strcmp(cmd, "KIOSK_LED_SYNC") == 0) {
        if (doc["t1_c"].is<JsonArray>() && doc["t2_c"].is<JsonArray>()) {
            JsonArrayConst t1 = doc["t1_c"].as<JsonArrayConst>();
            JsonArrayConst t2 = doc["t2_c"].as<JsonArrayConst>();
            if (t1.size() == 3 && t2.size() == 3) {
                hardware.setFactionColors(t1[0], t1[1], t1[2], t2[0], t2[1], t2[2]);
            }
        }

        if (strcmp(cmd, "KIOSK_LED_SYNC") == 0) {
            serverAllPlayersIn = doc["all_players_in"] | false;
            serverGameRunning  = doc["game_running"]   | false;
            serverGameReady    = doc["game_ready"]     | false;

            const char* mState = doc["mission_state"] | "";
            if (strstr(mState, "ALPHA WINS"))      hardware.setWinnerState("ALPHA");
            else if (strstr(mState, "BRAVO WINS")) hardware.setWinnerState("BRAVO");
            else if (strstr(mState, "DRAW"))       hardware.setWinnerState("DRAW");
            else if (strstr(mState, "STOPPED") || strcmp(mState, "STANDBY") == 0 ||
                     strcmp(mState, "PREPARING") == 0 || strcmp(mState, "ACTIVE") == 0) {
                hardware.setWinnerState("");
            }
        }
    }
}

// ============================================================================
// SETUP & LOOP PRINCIPALE
// ============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- AVVIO TACTICAL KIOSK (" FIRMWARE_VERSION ") ---");

    hardware.initialize();
    network.begin(onServerCommand);
}

void loop() {
    network.loop();

    // 1. Gestione Pulsante START MISSION
    if (hardware.pollStartPressed()) {
        if (serverAllPlayersIn && serverGameReady && !serverGameRunning) {
            Serial.println("[KIOSK] Avvio missione da pulsante fisico START.");
            const int seq[] = {200, 100, 400};
            hardware.startBeepSequence(seq, 3);
            network.sendKioskCommand("GAME_START");
        } else {
            Serial.println("[KIOSK] START negato: missione non pronta o operatori fuori campo.");
            hardware.beepAsync(150);
        }
    }

    // 2. Gestione Pulsante STOP MISSION / CHIUSURA FINE PARTITA
    if (hardware.pollEndPressed()) {
        if (serverGameRunning) {
            Serial.println("[KIOSK] Arresto forzato partita da pulsante STOP.");
            const int seq[] = {500, 100, 500};
            hardware.startBeepSequence(seq, 3);
            network.sendKioskCommand("FORCE_END_GAME");
        } else if (hardware.getWinnerState().length() > 0) {
            Serial.println("[KIOSK] Chiusura missione post-partita e ritorno a IDLE.");
            hardware.setWinnerState("");
            hardware.beepAsync(200);
            network.sendKioskCommand("CLOSE_MISSION");
        }
    }

    // 3. Lettura Pulsanti Assegnazione Squadra (ALPHA / BRAVO)
    bool alphaPressed = hardware.pollAlphaPressed();
    bool bravoPressed = hardware.pollBravoPressed();

    // 4. Macchina a Stati Principale RFID
    switch (currentState) {
        case STATE_WAIT_TAG:
            if (hardware.readCardUID(currentUID)) {
                Serial.printf("[RFID] Letto UID: %s\n", currentUID);
                hardware.beepAsync(100);
                hardware.startSwipeAnimation("NEUTRAL", "IN");
                network.sendTagScanned(currentUID);
                currentState = STATE_WAIT_SERVER;
                stateTimerMs = millis();
            }
            break;

        case STATE_WAIT_SERVER:
            if (millis() - stateTimerMs > SERVER_TIMEOUT_MS) {
                Serial.println("[ERR] Timeout risposta server durante il check-in!");
                const int errSeq[] = {150, 50, 150, 50, 400};
                hardware.startBeepSequence(errSeq, 5);
                currentState = STATE_ERROR_FLASH;
                stateTimerMs = millis();
            }
            break;

        case STATE_WAIT_ASSIGNMENT:
            if (alphaPressed) {
                hardware.beepAsync(400);
                network.sendTagAssign(currentUID, "ALPHA");
                hardware.startSwipeAnimation("ALPHA", "IN");
                currentState = STATE_SHOW_RESULT;
                stateTimerMs = millis();
            } else if (bravoPressed) {
                hardware.beepAsync(400);
                network.sendTagAssign(currentUID, "BRAVO");
                hardware.startSwipeAnimation("BRAVO", "IN");
                currentState = STATE_SHOW_RESULT;
                stateTimerMs = millis();
            } else if (millis() - stateTimerMs > ASSIGN_TIMEOUT_MS) {
                const int timeoutSeq[] = {50, 50, 50};
                hardware.startBeepSequence(timeoutSeq, 3);
                enterCooldown();
            }
            break;

        case STATE_SHOW_RESULT:
            if (millis() - stateTimerMs > SHOW_RESULT_MS) {
                enterCooldown();
            }
            break;

        case STATE_ERROR_FLASH:
            if (millis() - stateTimerMs > 750) {
                enterCooldown();
            }
            break;

        case STATE_COOLDOWN:
            if (millis() - stateTimerMs > COOLDOWN_MS) {
                currentState = STATE_WAIT_TAG;
            }
            break;
    }

    // 5. Aggiornamento non-bloccante di Buzzer, Sirena e Striscia LED (50 FPS)
    hardware.update(currentState, serverAllPlayersIn, serverGameRunning, serverGameReady);
}