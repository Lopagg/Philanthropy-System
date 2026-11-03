// src/main.cpp

/**
 * @file main.cpp
 * @brief File di ingresso principale del firmware.
 */

#include <Arduino.h>
#include <SPI.h> 
#include "nvs_flash.h"
#include <ArduinoJson.h>

#include "app_common.h"
#include "HardwareManager.h"
#include "NetworkManager.h"
#include "FirmwareUpdater.h"
#include "melodies.h"
#include "GameModes/MusicRoomMode.h"
#include "GameMode.h" 
#include "GameModes/SearchDestroyMode.h"
#include "GameModes/SearchDestroySettings.h"
#include "GameModes/DominationMode.h"
#include "GameModes/DominationSettings.h"
#include "GameModes/Foxhuntmode.h"
#include "GameModes/TerminalMode.h" 

HardwareManager hardware;
NetworkManager networkManager;
FirmwareUpdater updater(&hardware);
SearchDestroySettings* sdSettings = nullptr;
SearchDestroyMode* sdMode = nullptr;
DominationSettings* domSettings = nullptr;
DominationMode* domMode = nullptr;
FoxhuntMode* foxhuntMode = nullptr;
MusicRoomMode* musicRoomMode = nullptr;

TerminalMode* terminalMode = nullptr;

String team1_name = "ALPHA";
String team2_name = "BRAVO";
uint8_t team1_color[3] = {255, 0, 0}; 
uint8_t team2_color[3] = {0, 255, 0}; 

void displayMainMenu();
void handleNetworkCommands();

AppState currentAppState = APP_STATE_WELCOME;
AppState* appState = &currentAppState;

int mainMenuIndex = 0;

String mainMenuOptions[] = { "Cerca & Distruggi", "Dominio", "Stanza dei Suoni", "Test Hardware", "Terminale" };
int numMainMenuOptions = sizeof(mainMenuOptions) / sizeof(mainMenuOptions[0]);

enum TestHardwareSubState {
    TEST_MAIN,
    TEST_KEYS
};
TestHardwareSubState currentTestSubState = TEST_MAIN;

void displayMainMenu();
void handleWelcomeState();
void handleMainMenuState();
void handleTestHardwareState();
void displayTestHardwareMainMenu();
void displayKeyTestMenu();

String getCurrentModeString() {
    switch (*appState) {
        case APP_STATE_WELCOME: return "BOOTING...";
        case APP_STATE_MAIN_MENU: return "MAIN MENU";
        case APP_STATE_DOMINATION_MODE: return "DOMINATION";
        case APP_STATE_SEARCH_DESTROY_MODE: return "SEARCH_AND_DESTROY";
        case APP_STATE_MUSIC_ROOM: return "MUSIC ROOM";
        case APP_STATE_FOXHUNT: return "FOXHUNT";
        case APP_STATE_TEST_HARDWARE: return "HARDWARE TEST";
        case APP_STATE_TERMINAL_MODE: return "TERMINAL";
        default: return "UNKNOWN";
    }
}

void setup() {
    Serial.begin(115200);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    Serial.println("NVS Inizializzato.");

    sdSettings = new SearchDestroySettings();
    sdMode = new SearchDestroyMode(&hardware, &networkManager, sdSettings, &currentAppState, displayMainMenu);

    domSettings = new DominationSettings();
    domMode = new DominationMode(&hardware, &networkManager, domSettings, &currentAppState, displayMainMenu);

    musicRoomMode = new MusicRoomMode(&hardware, &currentAppState, displayMainMenu);
    foxhuntMode = new FoxhuntMode(&hardware, &networkManager, &currentAppState, displayMainMenu);
    
    terminalMode = new TerminalMode(&hardware, &networkManager, &currentAppState, displayMainMenu);

    hardware.initialize();
    networkManager.initialize(&hardware); 
    Serial.println("Avvio del sistema completato.");

    JsonDocument doc;
    doc["status"] = "ready";
    doc["version"] = FIRMWARE_VERSION;
    networkManager.sendEvent("DEVICE_ONLINE", doc);
}

unsigned long lastHeartbeatTime = 0;
const unsigned long heartbeatInterval = 2000; 

void loop() {
    hardware.updateButtons();
    hardware.updateMidiTune();
    networkManager.update();
    
    handleNetworkCommands();

    if (millis() - lastHeartbeatTime > heartbeatInterval) {
        lastHeartbeatTime = millis();
        
        JsonDocument doc;
        doc["uptime"] = millis() / 1000; 
        doc["mode"] = getCurrentModeString(); 
        doc["version"] = FIRMWARE_VERSION;    
        networkManager.sendEvent("HEARTBEAT", doc);
    }

    if (currentAppState == APP_STATE_WELCOME || currentAppState == APP_STATE_MAIN_MENU) {
        hardware.updateRainbowEffect();
    }

    switch (currentAppState) {
        case APP_STATE_WELCOME:
            handleWelcomeState();
            break;
        case APP_STATE_MAIN_MENU:
            handleMainMenuState();
            break;
        case APP_STATE_SEARCH_DESTROY_MODE:
            sdMode->loop(); 
            break;
        case APP_STATE_DOMINATION_MODE:
            domMode->loop();    
            break;
        case APP_STATE_MUSIC_ROOM:
            musicRoomMode->loop();  
            break;
        case APP_STATE_FOXHUNT:
            foxhuntMode->loop(); 
            break;
        case APP_STATE_TEST_HARDWARE:
            handleTestHardwareState();
            break;
        case APP_STATE_TERMINAL_MODE:
            terminalMode->loop();
            break;
    }
}

void handleWelcomeState() {
    static bool firstEntry = true;
    static unsigned long welcomeStartTime = 0;

    if (firstEntry) {
        Serial.println("STATO: Entrato in Welcome Screen");
        firstEntry = false;
        welcomeStartTime = millis();
        hardware.clearLcd();
        hardware.printLcd(7, 0, "REMOTE");
        hardware.printLcd(3, 1, "ACCESS TERMINAL");
        String versionString = "Alpha ver. ";
        versionString += FIRMWARE_VERSION;
        hardware.printLcd(2, 2, versionString);
        
        DateTime now = hardware.getRTCTime();
        char buffer[20];
        sprintf(buffer, "%02d/%02d/%02d %02d:%02d:%02d", 
                now.day(), now.month(), now.year() % 100,
                now.hour(), now.minute(), now.second());
        hardware.printLcd(1, 3, buffer);
    }
    
    if (millis() - welcomeStartTime > 3000) {
        firstEntry = true;
        currentAppState = APP_STATE_MAIN_MENU;
        displayMainMenu();
        
        JsonDocument doc;
        doc["new_mode"] = "MAIN_MENU";
        networkManager.sendEvent("MODE_CHANGE", doc);
    }
}

void displayMainMenu() {
    Serial.println("DISPLAY: Menu Principale");
    hardware.clearLcd();
    hardware.printLcd(0, 0, "MENU PRINCIPALE");
    
    int maxRows = hardware.getLcdRows() - 1;
    int startIdx = 0;
    if (mainMenuIndex >= maxRows) {
        startIdx = mainMenuIndex - maxRows + 1;
    }

    for (int i = 0; i < maxRows; i++) {
        int currentItemIndex = startIdx + i;
        if (currentItemIndex < numMainMenuOptions) {
            String prefix = (currentItemIndex == mainMenuIndex) ? "> " : "  ";
            hardware.printLcd(0, i + 1, prefix + mainMenuOptions[currentItemIndex]);
        }
    }

    hardware.printLcd(19, 1, " "); 
    hardware.printLcd(19, 3, " ");
    if (startIdx > 0) {
        hardware.printLcd(19, 1, "^");
    }
    if (startIdx + maxRows < numMainMenuOptions) {
        hardware.printLcd(19, 3, "v");
    }
    
    hardware.clearOled1();
    hardware.printOled2("CONFERMA", 2, 18, 25);
}

void handleMainMenuState() {
    char key = hardware.getKey();
    bool btn1_pressed = hardware.wasButton1Pressed();
    bool btn2_pressed = hardware.wasButton2Pressed();

    if (key == '2') {
        Serial.println("INPUT: Tasto SU (2) premuto");
        hardware.playTone(800, 50);
        mainMenuIndex = (mainMenuIndex - 1 + numMainMenuOptions) % numMainMenuOptions;
        displayMainMenu();
    } else if (key == '8') {
        Serial.println("INPUT: Tasto GIU (8) premuto");
        hardware.playTone(600, 50);
        mainMenuIndex = (mainMenuIndex + 1) % numMainMenuOptions;
        displayMainMenu();
    }
    
    if (btn1_pressed) {
        Serial.println("INPUT: Pulsante 1 (Indietro) premuto");
        hardware.playTone(300, 70);
    }

    if (btn2_pressed) {
        Serial.println("INPUT: Pulsante 2 (Conferma) premuto");
        hardware.playTone(1200, 100);

        JsonDocument doc;

        switch (mainMenuIndex) {
            case 0:
                Serial.println("TRANSIZIONE: Main Menu -> Cerca & Distruggi");
                currentAppState = APP_STATE_SEARCH_DESTROY_MODE;
                doc["new_mode"] = "SEARCH_AND_DESTROY";
                networkManager.sendEvent("MODE_CHANGE", doc);
                sdMode->enter();
                break;
            case 1:
                Serial.println("TRANSIZIONE: Main Menu -> Dominio");
                currentAppState = APP_STATE_DOMINATION_MODE;
                doc["new_mode"] = "DOMINATION";
                networkManager.sendEvent("MODE_CHANGE", doc);
                domMode->enter();
                break;
            case 2: 
                Serial.println("TRANSIZIONE: Main Menu -> Stanza dei Suoni");
                currentAppState = APP_STATE_MUSIC_ROOM;
                doc["new_mode"] = "MUSIC_ROOM";
                networkManager.sendEvent("MODE_CHANGE", doc);
                musicRoomMode->enter();
                break;
            case 3: 
                Serial.println("TRANSIZIONE: Main Menu -> Test Hardware");
                currentAppState = APP_STATE_TEST_HARDWARE;
                doc["new_mode"] = "TEST_HARDWARE";
                networkManager.sendEvent("MODE_CHANGE", doc);
                currentTestSubState = TEST_MAIN; 
                displayTestHardwareMainMenu(); 
                break;
            case 4: 
                Serial.println("TRANSIZIONE: Main Menu -> Terminale");
                currentAppState = APP_STATE_TERMINAL_MODE;
                terminalMode->enter();
                break;
        }
    }
}

void displayTestHardwareMainMenu() {
    hardware.clearLcd();
    hardware.printLcd(0, 0, "Test Hardware");
    hardware.printLcd(0, 1, "A:RFID B:Key C:OTA");
    hardware.printLcd(0, 2, "1,2,3 Colori LED");
    hardware.setStripColor(255, 255, 255);
    hardware.printOled1("INDIETRO", 2, 10, 25);
    hardware.printOled2("TEST", 2, 35, 25);
}

void displayKeyTestMenu() {
    hardware.clearLcd();
    hardware.printLcd(0, 0, "Test Interruttori");
    hardware.printLcd(0, 2, "Chiave 1:");
    hardware.printLcd(0, 3, "Chiave 2:");
    hardware.printOled1("INDIETRO", 2, 10, 25);
    hardware.clearOled2();
}

void handleTestHardwareState() {
    char key = hardware.getKey();
    bool btn1_pressed = hardware.wasButton1Pressed();
    bool btn2_pressed = hardware.wasButton2Pressed();

    if (currentTestSubState == TEST_MAIN) {

        if (key != NO_KEY) {
            Serial.printf("INPUT: '%c' premuto\n", key);
            hardware.playTone(700, 40);

            if (key == 'A') {
                hardware.printLcd(0, 1, "                    "); 
                hardware.printLcd(0, 1, "Avvicina una card...");
                String uid = hardware.readRFID(5000); 
                displayTestHardwareMainMenu(); 
                hardware.printLcd(0, 2, "UID:");
                hardware.printLcd(0, 3, uid);

                JsonDocument doc;
                doc["uid"] = uid;
                networkManager.sendEvent("TEST_RFID_READ", doc);

            } else if (key == 'B') {
                currentTestSubState = TEST_KEYS;
                displayKeyTestMenu();
            }
              else if (key == 'C') { 
                updater.checkForUpdates();
                displayTestHardwareMainMenu();
        }
              else {
                hardware.printLcd(0, 1, "Tasto premuto: " + String(key) + "   ");
                if (key == '1') hardware.setStripColor(255, 0, 0);
                if (key == '2') hardware.setStripColor(0, 255, 0);
                if (key == '3') hardware.setStripColor(0, 0, 255);
            }
        }

        if (btn2_pressed) {
            Serial.println("INPUT: Pulsante 2 (Conferma) premuto");
            hardware.playTone(1200, 100);
            hardware.printLcd(0, 1, "Pulsante 2 OK!");
        }

        if (btn1_pressed) {
            Serial.println("INPUT: Pulsante 1 (Indietro) premuto");
            hardware.playTone(300, 70);
            hardware.turnOffStrip();
            
            JsonDocument doc;
            doc["new_mode"] = "MAIN_MENU";
            networkManager.sendEvent("MODE_CHANGE", doc);

            Serial.println("TRANSIZIONE: Test Hardware -> Main Menu");
            currentAppState = APP_STATE_MAIN_MENU;
            displayMainMenu();
        }

    } else if (currentTestSubState == TEST_KEYS) {
        
        bool key1_state = hardware.isKey1Turned();
        bool key2_state = hardware.isKey2Turned();

        hardware.printLcd(10, 2, key1_state ? "ON " : "OFF");
        hardware.printLcd(10, 3, key2_state ? "ON " : "OFF");

        int half_leds = hardware.getStripLedCount() / 2;
        for (int i = 0; i < half_leds; i++) {
            hardware.setPixelColor(i, key1_state ? 255 : 0, 0, 0);
        }
        for (int i = half_leds; i < hardware.getStripLedCount(); i++) {
            hardware.setPixelColor(i, 0, key2_state ? 255 : 0, 0);
        }
        hardware.showStrip();

        if (btn1_pressed) {
            Serial.println("INPUT: Pulsante 1 (Indietro) premuto");
            hardware.playTone(300, 70);
            currentTestSubState = TEST_MAIN;
            displayTestHardwareMainMenu();
        }
    }
}

void handleNetworkCommands() {
    String message = networkManager.getReceivedMessage();
    if (message == "") return;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) return;

    const char* cmd = doc["cmd"];
    if (!cmd) return;

    String cmdStr = String(cmd);
    Serial.print("Comando GLOBALE ricevuto: ");
    Serial.println(cmdStr);
    
    if (cmdStr == "ENTER_TERMINAL_MODE") {
        Serial.println("Comando remoto: Forza ingresso in Terminal Mode");
        if (currentAppState != APP_STATE_TERMINAL_MODE) {
            currentAppState = APP_STATE_TERMINAL_MODE;
            if (terminalMode) terminalMode->enter();
        }
        return;
    }
    
    if (cmdStr == "SET_FACTIONS") {
        if (doc["t1"].is<const char*>()) team1_name = doc["t1"].as<String>();
        if (doc["t2"].is<const char*>()) team2_name = doc["t2"].as<String>();

        JsonArray t1_c = doc["t1_c"];
        JsonArray t2_c = doc["t2_c"];
        if (t1_c.size() == 3) { 
            team1_color[0] = t1_c[0]; team1_color[1] = t1_c[1]; team1_color[2] = t1_c[2]; 
            if (team1_color[0] == 255 && team1_color[1] == 51 && team1_color[2] == 51) {
                team1_color[1] = 0; team1_color[2] = 0;
            }
        }
        if (t2_c.size() == 3) { 
            team2_color[0] = t2_c[0]; team2_color[1] = t2_c[1]; team2_color[2] = t2_c[2]; 
            if (team2_color[0] == 85 && team2_color[1] == 255 && team2_color[2] == 85) {
                team2_color[0] = 0; team2_color[2] = 0;
            }
        }
    }
    
    if (currentAppState == APP_STATE_TERMINAL_MODE) {
        if (cmdStr == "UPDATE_UI") {
            if (terminalMode) terminalMode->parseCommand(doc);
            return;
        }
        if (cmdStr == "RETURN_TO_MENU") {
            if (terminalMode) terminalMode->exit();
            currentAppState = APP_STATE_MAIN_MENU;
            displayMainMenu();
            return;
        }
        if (cmdStr == "START_SD_GAME" || cmdStr == "START_DOM_GAME" || cmdStr == "SET_SD_SETTINGS" || cmdStr == "SET_DOM_SETTINGS") {
            return; 
        }
    }

    if (cmdStr == "FORCE_END_GAME") {
        if (currentAppState == APP_STATE_SEARCH_DESTROY_MODE && sdMode) sdMode->forceEndGame();
        else if (currentAppState == APP_STATE_DOMINATION_MODE && domMode) domMode->forceEndGame();
    } 
    else if (cmdStr == "FORCE_WIN") {
        String winner = doc["winner"] | "";
        String reason = doc["reason"] | ""; 
        
        if (currentAppState == APP_STATE_SEARCH_DESTROY_MODE && sdMode) {
            sdMode->forceWin(winner, reason);
        } else if (currentAppState == APP_STATE_DOMINATION_MODE && domMode) {
            domMode->forceWin(winner); 
        }
    }
    else if (cmdStr == "RETURN_TO_MENU") {
        Serial.println("Comando remoto ricevuto: Ritorno forzato al menu principale.");
        if (currentAppState == APP_STATE_SEARCH_DESTROY_MODE && sdMode) sdMode->exit();
        else if (currentAppState == APP_STATE_DOMINATION_MODE && domMode) domMode->exit();
        
        currentAppState = APP_STATE_MAIN_MENU;
        displayMainMenu();
    }
    else if (cmdStr == "GET_STATUS") {
        if (currentAppState == APP_STATE_SEARCH_DESTROY_MODE && sdMode) {
            sdMode->sendSettingsStatus();
            sdMode->sendTelemetry();
        } else if (currentAppState == APP_STATE_DOMINATION_MODE && domMode) {
            domMode->sendSettingsStatus();
            domMode->sendTelemetry();
        }
    }
    else if (cmdStr == "SET_SD_SETTINGS") {
        if (doc["bomb_time"].is<int>()) sdSettings->setBombTime(doc["bomb_time"]);
        if (doc["arm_time"].is<int>()) sdSettings->setArmingTime(doc["arm_time"]);
        if (doc["defuse_time"].is<int>()) sdSettings->setDefuseTime(doc["defuse_time"]);
        if (doc["use_arm_pin"].is<bool>()) sdSettings->setUseArmingPin(doc["use_arm_pin"]);
        if (doc["use_defuse_pin"].is<bool>()) sdSettings->setUseDisarmingPin(doc["use_defuse_pin"]);
        if (doc["arm_pin"].is<const char*>()) sdSettings->setArmingPin(doc["arm_pin"].as<String>());
        if (doc["defuse_pin"].is<const char*>()) sdSettings->setDisarmingPin(doc["defuse_pin"].as<String>());
        sdSettings->saveParameters();
        if (currentAppState == APP_STATE_SEARCH_DESTROY_MODE && sdMode) sdMode->sendSettingsStatus();
        Serial.println("Impostazioni C&D aggiornate da remoto.");
    }
    else if (cmdStr == "SET_DOM_SETTINGS") {
        if (doc["duration"].is<int>()) domSettings->setGameDuration(doc["duration"]);
        if (doc["capture_time"].is<int>()) domSettings->setCaptureTime(doc["capture_time"]);
        if (doc["countdown"].is<int>()) domSettings->setCountdownDuration(doc["countdown"]);
        domSettings->saveParameters();
        if (currentAppState == APP_STATE_DOMINATION_MODE && domMode) domMode->sendSettingsStatus();
        Serial.println("Impostazioni Dominio aggiornate da remoto.");
    }
    else if (cmdStr == "START_SD_GAME") {
        Serial.println("Avvio partita C&D da remoto (V1)");
        JsonDocument resp;
        resp["target_mode"] = "SEARCH_AND_DESTROY";
        networkManager.sendEvent("REMOTE_START_ACK", resp);
        currentAppState = APP_STATE_SEARCH_DESTROY_MODE;
        if (sdMode) sdMode->enterInGame();
    }
    else if (cmdStr == "START_DOM_GAME") {
        Serial.println("Avvio partita Dominio da remoto (V1)");
        JsonDocument resp;
        resp["target_mode"] = "DOMINATION";
        networkManager.sendEvent("REMOTE_START_ACK", resp);
        currentAppState = APP_STATE_DOMINATION_MODE;
        if (domMode) domMode->enterInGame();
    }
    else if (cmdStr == "RESUME_MISSION") {
        Serial.println("Ricevuto RESUME_MISSION dal server!");
        String modeStr = doc["mode"] | "";
        if (modeStr == "sd" && sdMode) {
            
            JsonDocument resp;
            resp["target_mode"] = "SEARCH_AND_DESTROY";
            networkManager.sendEvent("REMOTE_START_ACK", resp);
            currentAppState = APP_STATE_SEARCH_DESTROY_MODE;
            
            sdMode->enterInGame(); 
            
            sdMode->resumeGame(doc); 
        }
    }
    else if (cmdStr == "SYNC_TIME") {
        long g_time = doc["global_time_left"] | -1;
        if (g_time >= 0) {
            if (currentAppState == APP_STATE_SEARCH_DESTROY_MODE && sdMode) {
                sdMode->syncTime(g_time);
            }
        }
    }
}