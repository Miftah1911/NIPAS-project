/*
 * NIPAS — Precision Agriculture Robot Firmware
 * Target: ESP32
 * 
 * Base: WiFi provisioning via WiFiManager (autoConnect)
 * Added: sensor reads + WebSocket telemetry broadcast + command handler
 * 
 * In production this runs on the physical robot. For demo purposes,
 * the dashboard simulates this stream — but this code documents the
 * intended hardware behavior.
 */

#include <WiFi.h>
#include <WiFiManager.h>          // by tzapu  — your original include
#include <WebSocketsServer.h>     // by Markus Sattler
#include <ArduinoJson.h>          // by Benoit Blanchon

// ---------- Pin map (would connect to real hardware) ----------
#define PIN_SOIL_MOISTURE  34
#define PIN_TEMPERATURE    35
#define PIN_SPRAY_RELAY    26
#define PIN_VENT_RELAY     27
#define PIN_HEAT_RELAY     14

// ---------- Globals ----------
WebSocketsServer ws(81);
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL_MS = 3000;

// ---------- Sensor reads ----------
float readSoilMoisture() {
    int raw = analogRead(PIN_SOIL_MOISTURE);
    return constrain(map(raw, 3200, 1200, 0, 100), 0, 100);
}

float readTemperature() {
    int raw = analogRead(PIN_TEMPERATURE);
    float v = raw * (3.3f / 4095.0f);
    return v * 100.0f;   // LM35: 10 mV per °C
}

// ---------- WebSocket command handler ----------
void onWsEvent(uint8_t client, WStype_t type, uint8_t *payload, size_t len) {
    if (type == WStype_TEXT) {
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, payload, len)) return;
        const char* cmd = doc["command"];
        if (!cmd) return;

        if (strcmp(cmd, "spray") == 0) { digitalWrite(PIN_SPRAY_RELAY, HIGH); delay(400); digitalWrite(PIN_SPRAY_RELAY, LOW); }
        if (strcmp(cmd, "vent")  == 0) { digitalWrite(PIN_VENT_RELAY,  HIGH); delay(400); digitalWrite(PIN_VENT_RELAY,  LOW); }
        if (strcmp(cmd, "heat")  == 0) { digitalWrite(PIN_HEAT_RELAY,  HIGH); delay(400); digitalWrite(PIN_HEAT_RELAY,  LOW); }
    }
}

// ---------- Setup (your original WiFi logic, extended) ----------
void setup() {
    Serial.begin(115200);

    // Relay outputs
    pinMode(PIN_SPRAY_RELAY, OUTPUT);
    pinMode(PIN_VENT_RELAY,  OUTPUT);
    pinMode(PIN_HEAT_RELAY,  OUTPUT);
    digitalWrite(PIN_SPRAY_RELAY, LOW);
    digitalWrite(PIN_VENT_RELAY,  LOW);
    digitalWrite(PIN_HEAT_RELAY,  LOW);

    // --- Your original WiFiManager block (kept as-is) ---
    WiFiManager wm;
    // wm.resetSettings();   // uncomment for testing
    bool res = wm.autoConnect("NIPAS-Setup", "12345678");
    if (!res) {
        Serial.println("Failed to connect or hit timeout");
        ESP.restart();
    } else {
        Serial.println("Connected to Wi-Fi successfully!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    }

    // --- NEW: WebSocket server ---
    ws.begin();
    ws.onEvent(onWsEvent);
    Serial.println("WebSocket server started on port 81");
}

// ---------- Loop (your original empty loop, extended) ----------
void loop() {
    ws.loop();

    if (millis() - lastSend >= SEND_INTERVAL_MS) {
        lastSend = millis();

        StaticJsonDocument<160> doc;
        doc["type"]     = "telemetry";
        doc["moisture"] = readSoilMoisture();
        doc["temp"]     = readTemperature();

        String out;
        serializeJson(doc, out);
        ws.broadcastTXT(out);
    }
}