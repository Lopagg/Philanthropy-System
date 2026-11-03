// src/GameModes/TerminalMode.cpp

#include "GameModes/TerminalMode.h"
#include <ArduinoJson.h>

TerminalMode::TerminalMode(HardwareManager* hardware, NetworkManager* network, AppState* appState, MainMenuDisplayFunction displayFunc)
    : _hardware(hardware),
      _network(network),
      _appStatePtr(appState),
      _mainMenuDisplayFunc(displayFunc) {
}

void TerminalMode::enter() {
    Serial.println("Entrato in Modalita' Terminale (Thin Client V2)");
    
    JsonDocument doc;
    doc["mode"] = "TERMINAL";
    _network->sendEvent("MODE_ENTER", doc);
    
    _hardware->clearLcd();
    _hardware->printLcd(0, 1, "MODALITA' TERMINALE");
    _hardware->printLcd(0, 2, "In attesa di comandi");
    _hardware->setStripColor(50, 50, 255); 
    _hardware->printOled1("INDIETRO", 2, 10, 25);
    _hardware->clearOled2();
    
    _lastInputTime = millis();
}

void TerminalMode::loop() {
    handleInput();
}

void TerminalMode::handleInput() {
    char key = _hardware->getKey();
    bool btn1_pressed = _hardware->isButton1Pressed();
    bool btn1_was_pressed = _hardware->wasButton1Pressed();
    bool btn2_pressed = _hardware->isButton2Pressed();
    bool btn2_was_pressed = _hardware->wasButton2Pressed();
    bool key1_turned = _hardware->isKey1Turned();
    bool key2_turned = _hardware->isKey2Turned();

    if (btn1_pressed && btn2_pressed) {
        delay(2000); 
        if (_hardware->isButton1Pressed() && _hardware->isButton2Pressed()) {
            _hardware->playTone(1500, 500);
            exit();
            *_appStatePtr = APP_STATE_MAIN_MENU;
            _mainMenuDisplayFunc();
            return;
        }
    }

    if (millis() - _lastInputTime > 100) { 
        if (key != NO_KEY || btn1_was_pressed || btn2_was_pressed || btn1_pressed || btn2_pressed) {
            JsonDocument doc;
            if (key != NO_KEY) doc["key"] = String(key);
            
            if (btn1_was_pressed) doc["btn1"] = "click";
            else if (btn1_pressed) doc["btn1"] = "hold";
            
            if (btn2_was_pressed) doc["btn2"] = "click";
            else if (btn2_pressed) doc["btn2"] = "hold";

            doc["key1"] = key1_turned;
            doc["key2"] = key2_turned;
            
            _network->sendEvent("TERMINAL_INPUT", doc);
            _lastInputTime = millis();
        }
    }
}

void TerminalMode::exit() {
    Serial.println("Uscito da Modalita' Terminale");
    
    JsonDocument doc;
    doc["mode"] = "TERMINAL";
    _network->sendEvent("MODE_EXIT", doc);
    
    _hardware->turnOffStrip();
    _hardware->noTone();
    _hardware->clearOled1();
    _hardware->clearOled2();
}

void TerminalMode::parseCommand(JsonDocument& doc) {
    const char* cmd = doc["cmd"]; 
    if (!cmd) return;

    if (strcmp(cmd, "UPDATE_UI") == 0) {
        
        if (doc["lcd_0"].is<const char*>()) {
            _hardware->printLcd(0, 0, "                    "); 
            _hardware->printLcd(0, 0, doc["lcd_0"].as<String>());
        }
        if (doc["lcd_1"].is<const char*>()) {
            _hardware->printLcd(0, 1, "                    ");
            _hardware->printLcd(0, 1, doc["lcd_1"].as<String>());
        }
        if (doc["lcd_2"].is<const char*>()) {
            _hardware->printLcd(0, 2, "                    ");
            _hardware->printLcd(0, 2, doc["lcd_2"].as<String>());
        }
        if (doc["lcd_3"].is<const char*>()) {
            _hardware->printLcd(0, 3, "                    ");
            _hardware->printLcd(0, 3, doc["lcd_3"].as<String>());
        }
        if (doc["clear_lcd"] | false) {
            _hardware->clearLcd();
        }

        if (doc["oled_1"].is<const char*>()) {
            _hardware->printOled1(doc["oled_1"].as<String>(), 2, 10, 25);
        }
        if (doc["oled_2"].is<const char*>()) {
            _hardware->printOled2(doc["oled_2"].as<String>(), 2, 10, 25);
        }
        if (doc["clear_oled"] | false) {
            _hardware->clearOled1();
            _hardware->clearOled2();
        }

        if (doc["led_r"].is<int>() && doc["led_g"].is<int>() && doc["led_b"].is<int>()) {
            _hardware->setStripColor(doc["led_r"], doc["led_g"], doc["led_b"]);
        }
        if (doc["led_off"] | false) {
            _hardware->turnOffStrip();
        }

        if (doc["sound_freq"].is<int>() && doc["sound_dur"].is<int>()) {
            _hardware->playToneAsync(doc["sound_freq"], doc["sound_dur"]);
        }
        if (doc["sound_off"] | false) {
            _hardware->noTone();
        }

    } 
    else if (strcmp(cmd, "RETURN_TO_MENU") == 0) {
        exit();
        *_appStatePtr = APP_STATE_MAIN_MENU;
        _mainMenuDisplayFunc();
    }
}