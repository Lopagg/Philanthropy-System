#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoOTA.h>

// --- CONFIGURAZIONE RETE ---
const char* server_ip = "192.168.1.4"; 
const int mqtt_port = 1883;            

WiFiClient espClient;
PubSubClient mqttClient(espClient);
String deviceId = "NODE-KIOSK-1";

// --- DEFINIZIONE PIN ---
#define PIN_BTN_ALPHA 14
#define PIN_BTN_BRAVO 27
#define PIN_BUZZER    33

#define PIN_BTN_START  32  
#define PIN_BTN_END    15  
#define PIN_BTN_SIREN  13  
#define PIN_RELAY_SIREN 12 

// --- CONFIGURAZIONE STRISCIA LED ---
#define PIN_LED_STRIP 26  // Pin dati per la striscia LED
#define NUM_LEDS      28  // 3 LED pulsanti + 25 LED status strip
Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED_STRIP, NEO_GRB + NEO_KHZ800);

// Indici LED (0-2 per i pulsanti, 3-27 per la striscia di stato)
const int LED_SIREN = 0;
const int LED_END = 1;
const int LED_START = 2;
const int STRIP_START_IDX = 3;
const int STRIP_END_IDX = 27;
const int STRIP_LENGTH = 25;

// Colori Dinamici Iniziali
uint32_t COLOR_ALPHA = strip.Color(255, 0, 0);
uint32_t COLOR_BRAVO = strip.Color(0, 255, 0);
uint32_t COLOR_NEUTRAL = strip.Color(255, 255, 255); // Bianco
uint32_t COLOR_WAITING = strip.Color(255, 165, 0); // Arancione

// --- NUOVI COLORI IDLE (Non Rossi o Verdi) ---
// Quando la partita è pronta e tutti i giocatori in campo: Azzurro (Ciano tenue)
uint32_t COLOR_IDLE_READY = strip.Color(0, 100, 255); 
// Quando mancano giocatori o il sistema non è armato: Viola tenue
uint32_t COLOR_IDLE_WAIT = strip.Color(100, 0, 150);  
uint32_t COLOR_OFF = strip.Color(0, 0, 0);

// --- PIN RC522 (SPI) ---
#define RST_PIN 22
#define SS_PIN  5

// --- INIZIALIZZAZIONE RC522 ---
MFRC522 mfrc522(SS_PIN, RST_PIN);

// --- STATI DELLA MACCHINA ---
enum KioskState {
  STATE_WAIT_TAG,
  STATE_WAIT_SERVER,
  STATE_WAIT_ASSIGNMENT,
  STATE_COOLDOWN,
  STATE_SHOW_RESULT 
};

KioskState currentState = STATE_WAIT_TAG;
char currentUID[16] = "";
unsigned long requestTime = 0;
const unsigned long SERVER_TIMEOUT = 3000; 
const unsigned long ASSIGN_TIMEOUT = 10000; 
unsigned long lastNfcRead = 0; 
unsigned long cooldownStartTime = 0;

// Variabili per animazioni striscia
String lastAssignmentTeam = "";
String currentWinner = "";
unsigned long animStartTime = 0;
bool isAnimating = false;
String animType = "";

// --- GESTIONE HEARTBEAT E STATO SERVER ---
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 5000; 

bool server_all_players_in = false;
bool server_game_running = false;
bool server_game_ready = false;

// --- GESTIONE SIRENA E BUZZER ---
unsigned long sirenEndTime = 0;
bool sirenActive = false;
unsigned long buzzerEndTime = 0;
bool buzzerActive = false;

struct BeepSequence {
  int durations[10];
  int currentIndex;
  int length;
  unsigned long nextActionTime;
  bool isOn;
};
BeepSequence currentBeepSequence = {{}, 0, 0, 0, false};

// --- INTERRUPT PULSANTI GLOBALI ---
volatile bool flagStartPressed = false;
volatile bool flagEndPressed = false;
volatile bool flagSirenPressed = false; 

volatile unsigned long lastIsrStart = 0;
volatile unsigned long lastIsrEnd = 0;
volatile unsigned long lastIsrSiren = 0;

// Debounce Software (Alpha/Bravo)
int alphaState = HIGH;
int lastAlphaReading = HIGH;
unsigned long lastAlphaDebounce = 0;

int bravoState = HIGH;
int lastBravoReading = HIGH;
unsigned long lastBravoDebounce = 0;
const unsigned long DEBOUNCE_DELAY = 50; 

// --- FUNZIONI ISR (INTERRUPT) ---
void IRAM_ATTR isrStart() {
    if (millis() - lastIsrStart > 1000) { 
        flagStartPressed = true;
        lastIsrStart = millis();
    }
}
void IRAM_ATTR isrEnd() {
    if (millis() - lastIsrEnd > 1000) {
        flagEndPressed = true;
        lastIsrEnd = millis();
    }
}
void IRAM_ATTR isrSiren() {
    if (millis() - lastIsrSiren > 50) { 
        flagSirenPressed = true;
        lastIsrSiren = millis();
    }
}

// --- FUNZIONI DI SUPPORTO LED ---
void setMainStripColor(uint32_t color) {
    // Assicuriamoci che colori SOLO i led 3-27
    for(int i = STRIP_START_IDX; i <= STRIP_END_IDX; i++) {
        strip.setPixelColor(i, color);
    }
}

// Estrae le componenti RGB da un uint32_t (formato NeoPixel)
void extractRGB(uint32_t color, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = (color >> 16) & 0xFF;
    g = (color >>  8) & 0xFF;
    b = color         & 0xFF;
}

void processStripAnimations() {
    unsigned long currentMillis = millis();

    // 1. Priorità massima: Vittoria (Usa i Colori Dinamici)
    if (currentWinner != "") {
        uint32_t bg_color, fg_color, trail_color;
        uint8_t r, g, b;
        
        if (currentWinner == "ALPHA") {
            extractRGB(COLOR_ALPHA, r, g, b);
            bg_color = strip.Color(r/8, g/8, b/8); // Sfondo molto tenue del colore squadra
            fg_color = COLOR_ALPHA;
            trail_color = strip.Color(r/2.5, g/2.5, b/2.5); // Scia 
        } else if (currentWinner == "BRAVO") {
            extractRGB(COLOR_BRAVO, r, g, b);
            bg_color = strip.Color(r/8, g/8, b/8);
            fg_color = COLOR_BRAVO;
            trail_color = strip.Color(r/2.5, g/2.5, b/2.5);
        } else { // Pareggio (Giallo standard)
            bg_color = strip.Color(30, 30, 0);
            fg_color = strip.Color(255, 255, 0);
            trail_color = strip.Color(100, 100, 0);
        }

        // Applica sfondo tenue per identificare la squadra
        setMainStripColor(bg_color);
        
        // Animazione di celebrazione (Effetto scorrimento / wave)
        int speed = 70; // millisecondi per frame
        for (int j = 0; j < 3; j++) { // 3 onde contemporanee sulla striscia
            int pos = ((currentMillis / speed) + (j * (STRIP_LENGTH / 3))) % STRIP_LENGTH;
            strip.setPixelColor(STRIP_START_IDX + pos, fg_color);
            // Scia del punto luminoso principale
            int trail_pos = (pos + STRIP_LENGTH - 1) % STRIP_LENGTH;
            strip.setPixelColor(STRIP_START_IDX + trail_pos, trail_color);
        }
        return; 
    }

    // 2. Animazioni di assegnazione tessera (Swipe IN/OUT veloci, SOVRASCRIVONO lo stato partita)
    if (isAnimating) {
        // Velocizzato a 400ms per renderlo più rapido per check-in di massa
        if (currentMillis - animStartTime < 400) { 
            float progress = (float)(currentMillis - animStartTime) / 400.0;
            int ledsToLight = (int)(progress * STRIP_LENGTH);
            
            uint32_t teamColor = COLOR_NEUTRAL;
            if (lastAssignmentTeam == "ALPHA") teamColor = COLOR_ALPHA; // Dinamico
            else if (lastAssignmentTeam == "BRAVO") teamColor = COLOR_BRAVO; // Dinamico
            
            setMainStripColor(COLOR_OFF); 
            
            if (animType == "IN") {
                int center = STRIP_START_IDX + (STRIP_LENGTH / 2);
                int halfLight = ledsToLight / 2;
                for(int i = 0; i <= halfLight; i++) {
                    if(center + i <= STRIP_END_IDX) strip.setPixelColor(center + i, teamColor);
                    if(center - i >= STRIP_START_IDX) strip.setPixelColor(center - i, teamColor);
                }
            } else if (animType == "OUT") {
                 int center = STRIP_START_IDX + (STRIP_LENGTH / 2);
                 int halfLight = (STRIP_LENGTH - ledsToLight) / 2;
                 for(int i = 0; i <= halfLight; i++) {
                    if(center + i <= STRIP_END_IDX) strip.setPixelColor(center + i, teamColor);
                    if(center - i >= STRIP_START_IDX) strip.setPixelColor(center - i, teamColor);
                }
            }
            return; 
        } else {
            isAnimating = false; // Fine animazione dopo i 400ms
        }
    }

    // 3. Partita in corso (mostrato se non c'è animazione tessera attiva)
    if (server_game_running) {
        uint32_t gameColor = strip.Color(0, 0, 50); // Blu tenue per "In gioco"
        setMainStripColor(gameColor);
        return;
    }

    // 4. Stati del Menu Kiosk (Fuori partita)
    if (currentState == STATE_WAIT_ASSIGNMENT) {
        if ((currentMillis / 250) % 2 == 0) {
            setMainStripColor(COLOR_WAITING);
        } else {
            setMainStripColor(COLOR_OFF);
        }
    } else if (currentState == STATE_WAIT_SERVER) {
         setMainStripColor(strip.Color(50, 50, 50)); 
    } else {
        // Colori Idle (Azzurro per Ready, Viola per Wait)
        if (server_all_players_in && server_game_ready) {
            setMainStripColor(COLOR_IDLE_READY); 
        } else {
            setMainStripColor(COLOR_IDLE_WAIT); 
        }
    }
}


void updateButtonLEDs() {
    unsigned long currentMillis = millis();

    // LED 0: Sirena
    if (sirenActive) {
        strip.setPixelColor(LED_SIREN, strip.Color(255, 0, 0)); 
    } else {
        strip.setPixelColor(LED_SIREN, strip.Color(0, 0, 0)); 
    }

    // LED 1: Stop
    if (digitalRead(PIN_BTN_END) == LOW && server_game_running) {
        strip.setPixelColor(LED_END, strip.Color(255, 0, 0)); 
    } else if (server_game_running) {
        if ((currentMillis / 500) % 2 == 0) {
            strip.setPixelColor(LED_END, strip.Color(255, 0, 0)); 
        } else {
            strip.setPixelColor(LED_END, strip.Color(0, 0, 0));
        }
    } else {
        strip.setPixelColor(LED_END, strip.Color(0, 0, 0)); 
    }

    // LED 2: Start (Regole rigorose indipendenti)
    if (server_game_running) {
        // VERDE FISSO se la partita è in corso
        strip.setPixelColor(LED_START, strip.Color(0, 255, 0)); 
    } else if (!server_all_players_in) {
        // ROSSO FISSO se non tutti i giocatori sono in campo
        strip.setPixelColor(LED_START, strip.Color(255, 0, 0)); 
    } else if (server_all_players_in && server_game_ready) {
        // VERDE LAMPEGGIANTE se la partita è pronta per iniziare
        if ((currentMillis / 500) % 2 == 0) {
            strip.setPixelColor(LED_START, strip.Color(0, 255, 0)); 
        } else {
            strip.setPixelColor(LED_START, strip.Color(0, 0, 0));
        }
    } else {
        // SPENTO in tutti gli altri casi
        strip.setPixelColor(LED_START, strip.Color(0, 0, 0)); 
    }
}

// --- FUNZIONI DI SUPPORTO ---
void getUIDString(byte *uid, byte uidLength, char *buffer) {
  for (byte i = 0; i < uidLength; i++) {
    sprintf(&buffer[i * 2], "%02X", uid[i]);
  }
}

void beepBlocking(int duration_ms) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(duration_ms);
    digitalWrite(PIN_BUZZER, LOW);
}

void beepAsync(int duration_ms) {
  currentBeepSequence.length = 0; 
  digitalWrite(PIN_BUZZER, HIGH);
  buzzerEndTime = millis() + duration_ms;
  buzzerActive = true;
}

void startBeepSequence(int* durations, int length) {
  buzzerActive = false; 
  for(int i=0; i<length; i++) {
    currentBeepSequence.durations[i] = durations[i];
  }
  currentBeepSequence.length = length;
  currentBeepSequence.currentIndex = 0;
  currentBeepSequence.isOn = true;
  digitalWrite(PIN_BUZZER, HIGH);
  currentBeepSequence.nextActionTime = millis() + durations[0];
}

void updateBuzzer() {
  if (buzzerActive && millis() >= buzzerEndTime) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerActive = false;
  }
  
  if (currentBeepSequence.length > 0 && millis() >= currentBeepSequence.nextActionTime) {
    currentBeepSequence.currentIndex++;
    if (currentBeepSequence.currentIndex >= currentBeepSequence.length) {
      digitalWrite(PIN_BUZZER, LOW);
      currentBeepSequence.length = 0;
    } else {
      currentBeepSequence.isOn = !currentBeepSequence.isOn;
      digitalWrite(PIN_BUZZER, currentBeepSequence.isOn ? HIGH : LOW);
      currentBeepSequence.nextActionTime = millis() + currentBeepSequence.durations[currentBeepSequence.currentIndex];
    }
  }
}

void resetKiosk(bool isBoot = false) {
  currentUID[0] = '\0';
  
  if (isBoot) {
      currentWinner = "";
      currentState = STATE_WAIT_TAG;
      strip.clear(); strip.show();
      Serial.println("\n--- TACTICAL KIOSK PRONTO ---");
  } else {
      currentState = STATE_COOLDOWN;
      cooldownStartTime = millis();
      Serial.println("\n--- TACTICAL KIOSK IN COOLDOWN ---");
  }
}

void triggerSiren(unsigned long duration_ms) {
    unsigned long newEndTime = millis() + duration_ms;
    if (newEndTime > sirenEndTime) {
        sirenEndTime = newEndTime;
    }
    if (!sirenActive) {
        sirenActive = true;
        digitalWrite(PIN_RELAY_SIREN, HIGH); 
        Serial.println(">>> SIRENA (REMOTA) ATTIVATA <<<");
    }
}

// --- COMUNICAZIONE MQTT ---
void sendToServer(const char* type, const char* team = "", const char* cmd = "") {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<200> doc;
  doc["id"] = deviceId.c_str();
  doc["type"] = type; 
  doc["mode"] = "KIOSK";
  doc["version"] = "1.6-STRIP-OTA"; 
  
  JsonObject payload = doc.createNestedObject("payload");
  if (strlen(currentUID) > 0) payload["uid"] = currentUID;
  if (strlen(team) > 0) payload["team"] = team;
  if (strlen(cmd) > 0) payload["cmd"] = cmd;
  
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  
  char topicBuffer[64];
  snprintf(topicBuffer, sizeof(topicBuffer), "zulu/telemetry/%s", deviceId.c_str());
  
  mqttClient.publish(topicBuffer, jsonBuffer);
  
  Serial.print("INVIATO: ");
  Serial.println(jsonBuffer);
}

void sendHeartbeat() {
  if (!mqttClient.connected()) return;
  StaticJsonDocument<200> doc;
  doc["id"] = deviceId.c_str();
  doc["type"] = "HEARTBEAT"; 
  doc["mode"] = "KIOSK";
  doc["version"] = "1.6-STRIP-OTA";
  
  char jsonBuffer[128];
  serializeJson(doc, jsonBuffer);
  
  char topicBuffer[64];
  snprintf(topicBuffer, sizeof(topicBuffer), "zulu/telemetry/%s", deviceId.c_str());
  
  mqttClient.publish(topicBuffer, jsonBuffer);
}

void handleServerReply(const char* action) {
  if (strcmp(action, "WAIT_ASSIGN") == 0) {
    currentState = STATE_WAIT_ASSIGNMENT;
    requestTime = millis();
    int seq[] = {50, 50, 50}; 
    startBeepSequence(seq, 3);
  } else {
    isAnimating = true;
    animStartTime = millis();

    if (strstr(action, "IN_ALPHA") != NULL) { 
        lastAssignmentTeam = "ALPHA"; animType = "IN";
        int seq[] = {100, 50, 300}; startBeepSequence(seq, 3); 
    } 
    else if (strstr(action, "FUORI_ALPHA") != NULL) { 
        lastAssignmentTeam = "ALPHA"; animType = "OUT";
        int seq[] = {100, 50, 300}; startBeepSequence(seq, 3); 
    }
    else if (strstr(action, "IN_BRAVO") != NULL) { 
        lastAssignmentTeam = "BRAVO"; animType = "IN";
        int seq[] = {100, 50, 300}; startBeepSequence(seq, 3); 
    } 
    else if (strstr(action, "FUORI_BRAVO") != NULL) { 
        lastAssignmentTeam = "BRAVO"; animType = "OUT";
        int seq[] = {100, 50, 300}; startBeepSequence(seq, 3); 
    }
    else { 
        beepAsync(500); 
    }
    
    currentState = STATE_SHOW_RESULT;
    cooldownStartTime = millis();
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (length > 511) return; 
    
    char incomingBuffer[512];
    memcpy(incomingBuffer, payload, length);
    incomingBuffer[length] = '\0';
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, incomingBuffer);
    
    if (!error) {
      const char* type = doc["type"] | "";
      const char* cmd = doc["cmd"] | "";
      const char* action = doc["action"] | "";
      
      if (strcmp(cmd, "KIOSK_REPLY") == 0 && currentState == STATE_WAIT_SERVER) {
        handleServerReply(action);
      } 
      else if (strcmp(cmd, "SIREN_LONG") == 0) { triggerSiren(5000); } 
      else if (strcmp(cmd, "SIREN_SHORT") == 0) { triggerSiren(4000); } 
      else if (strcmp(cmd, "KIOSK_LED_SYNC") == 0) {
          server_all_players_in = doc["all_players_in"] | false;
          server_game_running = doc["game_running"] | false;
          server_game_ready = doc["game_ready"] | false;
          
          // Estrazione dinamica colori ad ogni heartbeat
          JsonArray t1_c = doc["t1_c"];
          JsonArray t2_c = doc["t2_c"];
          if (t1_c.size() == 3) COLOR_ALPHA = strip.Color(t1_c[0], t1_c[1], t1_c[2]);
          if (t2_c.size() == 3) COLOR_BRAVO = strip.Color(t2_c[0], t2_c[1], t2_c[2]);
          
          const char* m_state = doc["mission_state"] | "";
          if (strstr(m_state, "ALPHA WINS")) currentWinner = "ALPHA";
          else if (strstr(m_state, "BRAVO WINS")) currentWinner = "BRAVO";
          else if (strstr(m_state, "DRAW")) currentWinner = "DRAW";
          else if (strstr(m_state, "STOPPED") || strcmp(m_state, "STANDBY") == 0 || strcmp(m_state, "PREPARING") == 0 || strcmp(m_state, "ACTIVE") == 0) {
              currentWinner = ""; 
          }
      }
      else if (strcmp(cmd, "RESET") == 0) {
          Serial.println("!!! SISTEMA KIOSK: COMANDO RESET RICEVUTO !!!");
          
          strip.clear();
          for(int i = 0; i < NUM_LEDS; i++) {
              strip.setPixelColor(i, strip.Color(255, 0, 0));
          }
          strip.show();
          
          delay(1000); 
          ESP.restart();
      }
    }
}

void reconnectMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;
    Serial.print("Connessione MQTT Kiosk...");
    
    char willTopic[64];
    snprintf(willTopic, sizeof(willTopic), "zulu/telemetry/%s", deviceId.c_str());
    
    char willPayload[128];
    snprintf(willPayload, sizeof(willPayload), "{\"id\":\"%s\", \"type\":\"MODE_EXIT\", \"payload\":{\"mode\":\"OFFLINE\"}}", deviceId.c_str());
    
    if (mqttClient.connect(deviceId.c_str(), "admin", "admin", willTopic, 1, true, willPayload)) {
        Serial.println(" OK!");
        mqttClient.subscribe("zulu/cmd/broadcast");
        mqttClient.subscribe("zulu/telemetry/#"); 
        
        char myTopic[64];
        snprintf(myTopic, sizeof(myTopic), "zulu/cmd/%s", deviceId.c_str());
        mqttClient.subscribe(myTopic);
        
        StaticJsonDocument<200> doc;
        doc["id"] = deviceId.c_str();
        doc["type"] = "DEVICE_ONLINE";
        doc["mode"] = "KIOSK";
        doc["version"] = "1.6-STRIP-OTA";
        
        char jsonStr[128];
        serializeJson(doc, jsonStr);
        mqttClient.publish(willTopic, jsonStr);
    } else {
        Serial.println(" Fallita.");
    }
}

void setupOTA() {
  ArduinoOTA.setHostname("PHIL-KIOSK");
  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("Inizio aggiornamento " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nFine Aggiornamento.");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Avanzamento: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Errore OTA[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Autenticazione Fallita");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Fallito");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Fallito");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Fallito");
    else if (error == OTA_END_ERROR) Serial.println("End Fallito");
  });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_BTN_ALPHA, INPUT_PULLUP);
  pinMode(PIN_BTN_BRAVO, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_END, INPUT_PULLUP);
  pinMode(PIN_BTN_SIREN, INPUT_PULLUP);
  pinMode(PIN_RELAY_SIREN, OUTPUT);
  digitalWrite(PIN_RELAY_SIREN, LOW);

  strip.begin();
  strip.setBrightness(150); 
  strip.clear();
  strip.show();

  attachInterrupt(digitalPinToInterrupt(PIN_BTN_START), isrStart, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_END), isrEnd, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_SIREN), isrSiren, FALLING); 

  WiFiManager wm;
  wm.setConfigPortalTimeout(180); 
  if (!wm.autoConnect("ZULU-KIOSK")) {
      Serial.println("Connessione fallita. Riavvio...");
      delay(3000);
      ESP.restart();
  }
  
  setupOTA(); 

  mqttClient.setServer(server_ip, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);

  beepBlocking(100); delay(100); beepBlocking(100); delay(100); beepBlocking(100);
  
  resetKiosk(true); 
  delay(300);

  Serial.println("Avvio bus SPI...");
  SPI.begin();
  
  Serial.println("Inizializzazione modulo RC522...");
  mfrc522.PCD_Init();
  delay(4);
  
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (v == 0x00 || v == 0xFF) {
      Serial.println("ERRORE CRITICO: Modulo RC522 non rilevato! Controlla i cavi.");
      while(1) { beepBlocking(500); delay(500); }
  } else {
      Serial.print("RC522 Inizializzato! Versione Firmware: 0x");
      Serial.println(v, HEX);
  }
}

void loop() {
  ArduinoOTA.handle(); 

  static unsigned long lastMqttReconnect = 0;
  if (!mqttClient.connected()) {
      if (millis() - lastMqttReconnect > 5000) {
          lastMqttReconnect = millis();
          reconnectMQTT();
      }
  } else {
      mqttClient.loop();
  }

  updateBuzzer(); 
  
  if (flagStartPressed) {
      flagStartPressed = false;
      if (server_all_players_in && server_game_ready && !server_game_running) {
          Serial.println("=> COMANDO LOCALE: START MISSION");
          int seq[] = {200, 100, 400}; startBeepSequence(seq, 3);
          sendToServer("KIOSK_CMD", "", "GAME_START");
      } else {
          Serial.println("=> START NEGATO (Partita non pronta o giocatori fuori campo)");
          beepAsync(150);
      }
  }
  if (flagEndPressed) {
      flagEndPressed = false;
      
      if (server_game_running) {
          Serial.println("=> COMANDO LOCALE: FORCE END");
          int seq[] = {500, 100, 500}; startBeepSequence(seq, 3);
          sendToServer("KIOSK_CMD", "", "FORCE_END_GAME");
      } 
      else if (currentWinner != "") {
          currentWinner = "";
          beepAsync(200);
          Serial.println("=> VITTORIA RESETTATA DA TASTO STOP (Ritorno a IDLE)");
      } else {
          Serial.println("=> END NEGATO (Nessuna partita in corso e nessuna vittoria da pulire)");
      }
  }

  bool isSirenBtnHeld = (digitalRead(PIN_BTN_SIREN) == LOW);
  if (flagSirenPressed || isSirenBtnHeld) {
      flagSirenPressed = false; 
      if (!sirenActive) {
          digitalWrite(PIN_RELAY_SIREN, HIGH);
          sirenActive = true;
          Serial.println(">>> SIRENA (MANUALE) ATTIVATA <<<");
      }
      unsigned long manualEnd = millis() + 500; 
      if (manualEnd > sirenEndTime) sirenEndTime = manualEnd;
  } else {
      if (sirenActive && millis() > sirenEndTime) {
          digitalWrite(PIN_RELAY_SIREN, LOW);
          sirenActive = false;
          Serial.println("<<< SIRENA DISATTIVATA <<<");
      }
  }

  if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
     sendHeartbeat();
     lastHeartbeat = millis();
  }
  
  bool alphaConfirmedPressed = false;
  int currentAlphaReading = digitalRead(PIN_BTN_ALPHA);
  if (currentAlphaReading != lastAlphaReading) lastAlphaDebounce = millis();
  if ((millis() - lastAlphaDebounce) > DEBOUNCE_DELAY) {
      if (currentAlphaReading != alphaState) {
          alphaState = currentAlphaReading;
          if (alphaState == LOW) alphaConfirmedPressed = true; 
      }
  }
  lastAlphaReading = currentAlphaReading;

  bool bravoConfirmedPressed = false;
  int currentBravoReading = digitalRead(PIN_BTN_BRAVO);
  if (currentBravoReading != lastBravoReading) lastBravoDebounce = millis();
  if ((millis() - lastBravoDebounce) > DEBOUNCE_DELAY) {
      if (currentBravoReading != bravoState) {
          bravoState = currentBravoReading;
          if (bravoState == LOW) bravoConfirmedPressed = true;
      }
  }
  lastBravoReading = currentBravoReading;

  if (currentState == STATE_SHOW_RESULT) {
      if (millis() - cooldownStartTime > 2000) {
          resetKiosk(false);
      }
  }
  else if (currentState == STATE_COOLDOWN) {
      if (millis() - cooldownStartTime > 500) {
          currentState = STATE_WAIT_TAG;
      }
  }
  else if (currentState == STATE_WAIT_TAG) {
      
      if (millis() - lastNfcRead > 200) {
          lastNfcRead = millis();
          if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
              getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size, currentUID);
              mfrc522.PICC_HaltA(); 
              
              beepAsync(100);
              sendToServer("TAG_SCANNED");
              currentState = STATE_WAIT_SERVER;
              requestTime = millis();
              
              isAnimating = true;
              animStartTime = millis();
              lastAssignmentTeam = "NEUTRAL";
              animType = "IN"; 
          }
      }
  }
  else if (currentState == STATE_WAIT_SERVER) {
      if (millis() - requestTime > SERVER_TIMEOUT) {
          // --- FEEDBACK ERRORE CONNESSIONE (Respawn Fallito) ---
          Serial.println("ERRORE: Timeout Server durante il check-in!");
          
          // Triplo beep di errore
          int errSeq[] = {150, 50, 150, 50, 400}; 
          startBeepSequence(errSeq, 5);
          
          // Lampeggio Rosso della Striscia per 1 secondo
          setMainStripColor(strip.Color(255, 0, 0));
          strip.show();
          delay(200);
          setMainStripColor(COLOR_OFF);
          strip.show();
          delay(200);
          setMainStripColor(strip.Color(255, 0, 0));
          strip.show();
          delay(200);
          
          resetKiosk(false);
      }
  }
  else if (currentState == STATE_WAIT_ASSIGNMENT) {
      if (alphaConfirmedPressed) {
          beepAsync(400);
          sendToServer("TAG_ASSIGN", "ALPHA");
          isAnimating = true;
          animStartTime = millis();
          lastAssignmentTeam = "ALPHA";
          animType = "IN";
          currentState = STATE_SHOW_RESULT;
          cooldownStartTime = millis();
      } 
      else if (bravoConfirmedPressed) {
          beepAsync(400);
          sendToServer("TAG_ASSIGN", "BRAVO");
          isAnimating = true;
          animStartTime = millis();
          lastAssignmentTeam = "BRAVO";
          animType = "IN";
          currentState = STATE_SHOW_RESULT;
          cooldownStartTime = millis();
      }

      if (millis() - requestTime > ASSIGN_TIMEOUT) {
          int seq[] = {50, 50, 50}; startBeepSequence(seq, 3);
          resetKiosk(false);
      }
  }

  processStripAnimations(); 
  updateButtonLEDs(); 
  strip.show();
}