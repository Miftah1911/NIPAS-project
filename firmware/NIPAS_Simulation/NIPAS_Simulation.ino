/*
 * NIPAS_Simulation.ino — Full state machine, zero hardware
 * Target: ESP32 Dev Board (or even ESP32-S3 without any wiring)
 *
 * Runs the complete autonomous loop:
 *   - State machine cycles IDLE → MOVING → SCANNING → SPRAYING → TURNING
 *   - Sensor values are synthesized
 *   - Motor commands are printed to Serial instead of driving pins
 *   - MQTT publishes real telemetry to the broker
 *   - Dashboard receives real messages — identical to real hardware
 *
 * Use this to:
 *   - Test the MQTT pipeline before assembly
 *   - Demo the dashboard with real MQTT (not browser simulation)
 *   - Verify state transitions work
 *   - Practice the full autonomous cycle
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "NIPAS_Config.h"

// ═══════════════════════════════════════════════════════════
// SIMULATION
// ═══════════════════════════════════════════════════════════
#define SIM_TICK_INTERVAL_MS     500    // "drive" a bit every 500ms
#define SIM_SCAN_TRIGGER_MS      4000   // scan a "plant" every 4 seconds
#define CM_PER_SIM_TICK          5.0f   // simulate 5 cm per tick

// ═══════════════════════════════════════════════════════════
// STATE MACHINE
// ═══════════════════════════════════════════════════════════
enum RobotState { STATE_IDLE, STATE_MOVING, STATE_SCANNING, STATE_SPRAYING, STATE_TURNING, STATE_ROW_COMPLETE, STATE_FINISHED, STATE_ERROR };
RobotState state = STATE_IDLE;
const char* stateName(RobotState s) {
    switch (s) {
        case STATE_IDLE:         return "IDLE";
        case STATE_MOVING:       return "MOVING";
        case STATE_SCANNING:     return "SCANNING";
        case STATE_SPRAYING:     return "SPRAYING";
        case STATE_TURNING:      return "TURNING";
        case STATE_ROW_COMPLETE: return "ROW_COMPLETE";
        case STATE_FINISHED:     return "FINISHED";
        case STATE_ERROR:        return "ERROR";
    }
    return "?";
}

// ═══════════════════════════════════════════════════════════
// SIM STATE
// ═══════════════════════════════════════════════════════════
float simDistanceCm = 0;
float simSoil       = 42.0f;
float simTemp       = 23.5f;
float simHumidity   = 65.0f;
float simBattery    = 12.1f;
int   simRow        = 1;
int   simScanned    = 0;
int   simSprayed    = 0;
float simInfection  = 0;
String simSeverity  = "none";

unsigned long stateEnteredAt  = 0;
unsigned long lastSimTick     = 0;
unsigned long lastSensorRead  = 0;
unsigned long lastPublish     = 0;

// ═══════════════════════════════════════════════════════════
// OFFLINE BUFFER (mirrors NIPAS_Robot.ino)
// ═══════════════════════════════════════════════════════════
Preferences prefs;
int bufferCount = 0;
int bufferHead  = 0;
const int BUFFER_MAX = 500;

// ═══════════════════════════════════════════════════════════
// HARDWARE OBJECTS (MQTT only — no motors, no servos)
// ═══════════════════════════════════════════════════════════
WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);

// ═══════════════════════════════════════════════════════════
// SIM HELPERS
// ═══════════════════════════════════════════════════════════
float randFloat(float min, float max) {
    return min + (random(0, 10000) / 10000.0f) * (max - min);
}

void simMotorsForward()  { Serial.printf("[SIM-MOTOR] → FORWARD  (dist %.1f cm)\n", simDistanceCm); }
void simMotorsStop()     { Serial.println("[SIM-MOTOR] → STOP"); }
void simMotorsTurnRight(){ Serial.println("[SIM-MOTOR] → TURN RIGHT 180°"); }
void simSprayOn()        { Serial.println("[SIM-PUMP]  → ON"); }
void simSprayOff()       { Serial.println("[SIM-PUMP]  → OFF"); }

void simSensorRead() {
    // Small random drift around baselines
    simSoil     = constrain(simSoil     + randFloat(-0.5f, 0.5f), 15, 70);
    simTemp     = constrain(simTemp     + randFloat(-0.3f, 0.3f), 15, 34);
    simHumidity = constrain(simHumidity + randFloat(-1.0f, 1.0f), 35, 90);
    simBattery  = constrain(simBattery  - randFloat(0.001f, 0.004f), 9.0f, 12.8f);
}

// Simulate HSV result: mostly healthy, occasionally infected
void simLeafScan() {
    int roll = random(0, 100);
    if (roll < 45) {
        simInfection = 0; simSeverity = "none";
    } else if (roll < 65) {
        simInfection = randFloat(0.06f, 0.15f); simSeverity = "low";
    } else if (roll < 82) {
        simInfection = randFloat(0.16f, 0.30f); simSeverity = "moderate";
    } else {
        simInfection = randFloat(0.31f, 0.45f); simSeverity = "high";
    }
    Serial.printf("[SIM-CAM]   → infection %.1f%% severity %s\n",
                  simInfection * 100, simSeverity.c_str());
}

unsigned long sprayDurationMs() {
    if (simInfection < 0.05f) return 0;
    float t = constrain((simInfection - 0.05f) / 0.35f, 0.0f, 1.0f);
    return 800 + (unsigned long)(t * 1700);
}

// ═══════════════════════════════════════════════════════════
// OFFLINE BUFFER
// ═══════════════════════════════════════════════════════════
void bufferReading() {
    String key = "r" + String(bufferHead);
    StaticJsonDocument<128> doc;
    doc["ts"] = (uint32_t)time(nullptr);
    doc["m"]  = simSoil;
    doc["t"]  = simTemp;
    doc["h"]  = simHumidity;
    String out;
    serializeJson(doc, out);
    prefs.putString(key.c_str(), out);
    bufferHead = (bufferHead + 1) % BUFFER_MAX;
    if (bufferCount < BUFFER_MAX) bufferCount++;
    prefs.putInt("head",  bufferHead);
    prefs.putInt("count", bufferCount);
    Serial.printf("[SIM-BUF]   Stored %d/%d\n", bufferCount, BUFFER_MAX);
}

void flushBuffer() {
    if (!mqtt.connected() || bufferCount == 0) return;
    Serial.printf("[SIM-BUF]   Flushing %d buffered readings\n", bufferCount);
    int start = (bufferHead - bufferCount + BUFFER_MAX * 2) % BUFFER_MAX;
    int flushed = 0;
    for (int i = 0; i < bufferCount; i++) {
        int idx = (start + i) % BUFFER_MAX;
        String key = "r" + String(idx);
        String data = prefs.getString(key.c_str(), "");
        if (data.length() == 0) continue;
        if (mqtt.publish(TOPIC_TELEMETRY, data.c_str())) {
            prefs.remove(key.c_str());
            flushed++;
        }
        delay(60);
        if (!mqtt.connected()) break;
    }
    bufferCount -= flushed;
    if (bufferCount < 0) bufferCount = 0;
    prefs.putInt("count", bufferCount);
    Serial.printf("[SIM-BUF]   Flushed %d · %d remaining\n", flushed, bufferCount);
}

// ═══════════════════════════════════════════════════════════
// MQTT PUBLISH
// ═══════════════════════════════════════════════════════════
void publishRobotState() {
    if (!mqtt.connected()) return;
    StaticJsonDocument<384> doc;
    doc["ts"]             = (uint32_t)time(nullptr);
    doc["state"]          = stateName(state);
    doc["row"]            = simRow;
    doc["rows_total"]     = 5;
    doc["plants_scanned"] = simScanned;
    doc["plants_sprayed"] = simSprayed;
    doc["distance_cm"]    = simDistanceCm;
    doc["last_infection"] = simInfection;
    doc["last_severity"]  = simSeverity;
    doc["battery"]        = simBattery;
    doc["moisture"]       = simSoil;
    doc["temp"]           = simTemp;
    doc["humidity"]       = simHumidity;
    doc["buffered"]       = bufferCount;
    doc["simulation"]     = true;
    String out;
    serializeJson(doc, out);
    mqtt.publish(TOPIC_ROBOT, out.c_str());
}

void publishTelemetry() {
    if (!mqtt.connected()) return;
    StaticJsonDocument<160> doc;
    doc["ts"]          = (uint32_t)time(nullptr);
    doc["moisture"]    = simSoil;
    doc["temperature"] = simTemp;
    doc["humidity"]    = simHumidity;
    doc["buffered"]    = bufferCount;
    String out;
    serializeJson(doc, out);
    mqtt.publish(TOPIC_TELEMETRY, out.c_str());
}

// ═══════════════════════════════════════════════════════════
// MQTT CALLBACK
// ═══════════════════════════════════════════════════════════
void onMqttMessage(char* topic, byte* payload, unsigned int len) {
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, payload, len)) return;
    const char* cmd = doc["cmd"];
    if (!cmd) return;
    Serial.printf("[SIM-MQTT]  ← %s\n", cmd);

    if (strcmp(cmd, "start") == 0) {
        simDistanceCm = 0; simRow = 1; simScanned = 0; simSprayed = 0;
        state = STATE_MOVING; stateEnteredAt = millis();
        Serial.println("[SIM]       ▶ Mission started");
    } else if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "emergency") == 0) {
        state = STATE_IDLE; stateEnteredAt = millis();
        Serial.println("[SIM]       ⏹ Stopped");
    } else if (strcmp(cmd, "next_row") == 0) {
        state = STATE_TURNING; stateEnteredAt = millis();
    } else if (strcmp(cmd, "scan_now") == 0) {
        if (state == STATE_MOVING) { state = STATE_SCANNING; stateEnteredAt = millis(); }
    } else if (strcmp(cmd, "spray_now") == 0) {
        simSprayOn(); delay(1500); simSprayOff();
    } else if (strcmp(cmd, "flush_buffer") == 0) {
        flushBuffer();
    }
}

// ═══════════════════════════════════════════════════════════
// MQTT CONNECT
// ═══════════════════════════════════════════════════════════
bool mqttConnect() {
    if (WiFi.status() != WL_CONNECTED) return false;
    secureClient.setInsecure();
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(onMqttMessage);
    mqtt.setKeepAlive(30);
    mqtt.setBufferSize(512);
    String cid = String("nipas-sim-") + DEVICE_ID;
    if (mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS,
                     TOPIC_LWT, 1, true, "{\"online\":false}")) {
        mqtt.publish(TOPIC_LWT, "{\"online\":true}", true);
        mqtt.subscribe(TOPIC_CMD, 1);
        Serial.println("[SIM-MQTT]  Connected");
        return true;
    }
    Serial.printf("[SIM-MQTT]  Failed rc=%d\n", mqtt.state());
    return false;
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\n═══════════════════════════════════════════");
    Serial.println("  NIPAS Simulation Mode");
    Serial.println("  No hardware required — full MQTT pipeline");
    Serial.println("═══════════════════════════════════════════\n");

    randomSeed(analogRead(0));

    prefs.begin("nipas", false);
    bufferCount = prefs.getInt("count", 0);
    bufferHead  = prefs.getInt("head",  0);

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    Serial.println("[SIM-WIFI] Connecting...");
    bool connected = wm.autoConnect("NIPAS-Setup", "12345678");
    if (connected) {
        Serial.println("[SIM-WIFI] Connected: " + WiFi.localIP().toString());
        configTime(6 * 3600, 0, "pool.ntp.org", "time.google.com");
        mqttConnect();
    } else {
        Serial.println("[SIM-WIFI] No network — simulation will buffer locally");
    }

    state = STATE_IDLE;
    stateEnteredAt = millis();
    Serial.println("[SIM]       Ready — waiting for START command\n");
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqtt.connected()) {
            static unsigned long lastRetry = 0;
            if (millis() - lastRetry > 5000) { lastRetry = millis(); mqttConnect(); }
        } else mqtt.loop();
    }

    // Sim drive tick
    if (millis() - lastSimTick > SIM_TICK_INTERVAL_MS) {
        lastSimTick = millis();
        if (state == STATE_MOVING) simDistanceCm += CM_PER_SIM_TICK;
    }

    // Sensors + telemetry
    if (millis() - lastSensorRead > 30000) {
        lastSensorRead = millis();
        simSensorRead();
        if (mqtt.connected()) publishTelemetry();
        else                  bufferReading();
    }

    // Periodic state publish
    if (millis() - lastPublish > 2000) {
        lastPublish = millis();
        publishRobotState();
    }

    // Flush buffer when online
    static unsigned long lastFlush = 0;
    if (mqtt.connected() && bufferCount > 0 && millis() - lastFlush > 30000) {
        lastFlush = millis();
        flushBuffer();
    }

    // ═══════════════════════════════════════════════════════
    // SIMULATED STATE MACHINE
    // ═══════════════════════════════════════════════════════
    switch (state) {
        case STATE_IDLE:    simMotorsStop(); break;

        case STATE_MOVING: {
            simMotorsForward();
            // Trigger a scan every N cm of simulated travel
            static float lastScanDist = 0;
            if (simDistanceCm - lastScanDist >= 40) {
                lastScanDist = simDistanceCm;
                simMotorsStop();
                state = STATE_SCANNING; stateEnteredAt = millis();
            }
            // Row end?
            if (simDistanceCm >= 500) {
                simMotorsStop();
                state = STATE_ROW_COMPLETE; stateEnteredAt = millis();
            }
            break;
        }

        case STATE_SCANNING: {
            if (millis() - stateEnteredAt < 100) break;
            simLeafScan();
            simScanned++;
            unsigned long dur = sprayDurationMs();
            if (dur > 0) { state = STATE_SPRAYING; }
            else         { state = STATE_MOVING; }
            stateEnteredAt = millis();
            break;
        }

        case STATE_SPRAYING: {
            if (millis() - stateEnteredAt < 50) simSprayOn();
            unsigned long dur = sprayDurationMs();
            if (millis() - stateEnteredAt >= dur) {
                simSprayOff();
                simSprayed++;
                simSoil = constrain(simSoil + 8.0f, 15, 70);   // moisture up
                simInfection = max(0.0f, simInfection * 0.35f);
                state = STATE_MOVING; stateEnteredAt = millis();
            }
            break;
        }

        case STATE_ROW_COMPLETE: {
            simRow++;
            if (simRow > 5) { state = STATE_FINISHED; stateEnteredAt = millis(); }
            else            { state = STATE_TURNING;  stateEnteredAt = millis(); }
            break;
        }

        case STATE_TURNING: {
            if (millis() - stateEnteredAt < 2000) simMotorsTurnRight();
            else { simDistanceCm = 0; state = STATE_MOVING; stateEnteredAt = millis(); }
            break;
        }

        case STATE_FINISHED:
            simMotorsStop();
            break;

        case STATE_ERROR:
            simMotorsStop();
            break;
    }
}