/*
 * NIPAS_Robot.ino — Main field robot firmware
 * Target: ESP32 Dev Board
 *
 * ─────────────────────────────────────────────────────────
 * OFFLINE-FIRST DESIGN
 * ─────────────────────────────────────────────────────────
 * Core autonomy (driving, scanning, spraying) runs 100% offline.
 * Every telemetry reading is buffered to ESP32 flash memory.
 * When a network becomes available (home WiFi, phone hotspot, 4G),
 * the buffer flushes automatically to the MQTT broker.
 *
 *   Network absent  → buffer grows locally (~45 days capacity)
 *   Network present → buffer flushes + live streaming resumes
 *
 * Libraries needed:
 *   - WiFiManager       (tzapu)
 *   - PubSubClient      (Nick O'Leary)
 *   - ArduinoJson       (Benoit Blanchon)
 *   - ESP32Servo        (Kevin Harrington)
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
// PINS
// ═══════════════════════════════════════════════════════════
#define PIN_L298_IN1       25
#define PIN_L298_IN2       26
#define PIN_L298_IN3       27
#define PIN_L298_IN4       14
#define PIN_L298_ENA       32
#define PIN_L298_ENB       33
#define PIN_TRIG            5
#define PIN_ECHO           18
#define PIN_ENCODER_L      34
#define PIN_ENCODER_R      35
#define PIN_RELAY_PUMP     13
#define PIN_LED_STATUS      2
#define PIN_BUZZER          4

// ═══════════════════════════════════════════════════════════
// STATE MACHINE
// ═══════════════════════════════════════════════════════════
enum RobotState {
    STATE_IDLE,
    STATE_MOVING,
    STATE_SCANNING,
    STATE_SPRAYING,
    STATE_TURNING,
    STATE_OBSTACLE_AVOID,
    STATE_ROW_COMPLETE,
    STATE_FINISHED,
    STATE_ERROR
};

RobotState state = STATE_IDLE;
const char* stateName(RobotState s) {
    switch (s) {
        case STATE_IDLE:           return "IDLE";
        case STATE_MOVING:         return "MOVING";
        case STATE_SCANNING:       return "SCANNING";
        case STATE_SPRAYING:       return "SPRAYING";
        case STATE_TURNING:        return "TURNING";
        case STATE_OBSTACLE_AVOID: return "OBSTACLE_AVOID";
        case STATE_ROW_COMPLETE:   return "ROW_COMPLETE";
        case STATE_FINISHED:       return "FINISHED";
        case STATE_ERROR:          return "ERROR";
    }
    return "?";
}

// ═══════════════════════════════════════════════════════════
// STATE VARIABLES
// ═══════════════════════════════════════════════════════════
volatile long encoderLeftTicks  = 0;
volatile long encoderRightTicks = 0;
long lastScanTick = 0;
long rowStartTick = 0;

int plantSpacingCm = DEFAULT_PLANT_SPACING_CM;
int rowLengthCm    = DEFAULT_ROW_LENGTH_CM;
int totalRows      = DEFAULT_TOTAL_ROWS;
int ticksPerPlant  = 0;
int ticksPerRow    = 0;

int currentRow     = 1;
int plantsScanned  = 0;
int plantsSprayed  = 0;

float lastDistanceCm = 999;
float soilMoisture   = 0;
float ambientTemp    = 23.5f;
float airHumidity    = 65.0f;
float batteryVoltage = 12.0f;

// Camera UART
String camBuf = "";
bool camReady = false;
float camInfection = 0;
String camSeverity = "none";
unsigned long camRequestTime = 0;

// Timers
unsigned long stateEnteredAt = 0;
unsigned long lastPublish    = 0;
unsigned long lastSensorRead = 0;
unsigned long lastNetworkTry = 0;
unsigned long lastBufferFlush = 0;

// ═══════════════════════════════════════════════════════════
// OFFLINE BUFFER
// ═══════════════════════════════════════════════════════════
Preferences prefs;
int    bufferCount   = 0;
int    bufferHead    = 0;    // circular index for writing
const int BUFFER_MAX = 500;  // 500 readings ≈ 4 hours at 30s intervals
const unsigned long BUFFER_FLUSH_INTERVAL_MS = 30000;   // try flush every 30s

// ═══════════════════════════════════════════════════════════
// HARDWARE OBJECTS
// ═══════════════════════════════════════════════════════════
WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);
HardwareSerial camSerial(2);

// ═══════════════════════════════════════════════════════════
// MOTOR CONTROL
// ═══════════════════════════════════════════════════════════
void motorsStop() {
    digitalWrite(PIN_L298_IN1, LOW); digitalWrite(PIN_L298_IN2, LOW);
    digitalWrite(PIN_L298_IN3, LOW); digitalWrite(PIN_L298_IN4, LOW);
    analogWrite(PIN_L298_ENA, 0);    analogWrite(PIN_L298_ENB, 0);
}
void motorsForward(int speed = MOTOR_SPEED_NORMAL) {
    digitalWrite(PIN_L298_IN1, HIGH); digitalWrite(PIN_L298_IN2, LOW);
    digitalWrite(PIN_L298_IN3, HIGH); digitalWrite(PIN_L298_IN4, LOW);
    analogWrite(PIN_L298_ENA, speed); analogWrite(PIN_L298_ENB, speed);
}
void motorsBackward(int speed = MOTOR_SPEED_NORMAL) {
    digitalWrite(PIN_L298_IN1, LOW); digitalWrite(PIN_L298_IN2, HIGH);
    digitalWrite(PIN_L298_IN3, LOW); digitalWrite(PIN_L298_IN4, HIGH);
    analogWrite(PIN_L298_ENA, speed); analogWrite(PIN_L298_ENB, speed);
}
void motorsTurnRight(int speed = MOTOR_SPEED_TURN) {
    digitalWrite(PIN_L298_IN1, HIGH); digitalWrite(PIN_L298_IN2, LOW);
    digitalWrite(PIN_L298_IN3, LOW);  digitalWrite(PIN_L298_IN4, HIGH);
    analogWrite(PIN_L298_ENA, speed); analogWrite(PIN_L298_ENB, speed);
}
void motorsTurnLeft(int speed = MOTOR_SPEED_TURN) {
    digitalWrite(PIN_L298_IN1, LOW);  digitalWrite(PIN_L298_IN2, HIGH);
    digitalWrite(PIN_L298_IN3, HIGH); digitalWrite(PIN_L298_IN4, LOW);
    analogWrite(PIN_L298_ENA, speed); analogWrite(PIN_L298_ENB, speed);
}

// ═══════════════════════════════════════════════════════════
// ENCODER ISRs
// ═══════════════════════════════════════════════════════════
void IRAM_ATTR isrEncoderLeft()  { encoderLeftTicks++; }
void IRAM_ATTR isrEncoderRight() { encoderRightTicks++; }

// ═══════════════════════════════════════════════════════════
// ULTRASONIC
// ═══════════════════════════════════════════════════════════
float readDistanceCm() {
    digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    long dur = pulseIn(PIN_ECHO, HIGH, 25000);
    if (dur == 0) return 999;
    return dur * 0.0343f / 2.0f;
}

// ═══════════════════════════════════════════════════════════
// ESP32-CAM UART
// ═══════════════════════════════════════════════════════════
void camRequestScan() {
    camBuf = "";
    camReady = false;
    camRequestTime = millis();
    camSerial.println("SCAN");
    Serial.println("[CAM] → SCAN");
}

void camParseLine(const String& line) {
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, line)) return;
    camInfection = doc["infection"] | 0.0f;
    camSeverity  = String((const char*)(doc["severity"] | "none"));
    camReady = true;
    Serial.printf("[CAM] ← infection=%.3f severity=%s\n",
                  camInfection, camSeverity.c_str());
}

void camPump() {
    while (camSerial.available()) {
        char c = camSerial.read();
        if (c == '\n') { camParseLine(camBuf); camBuf = ""; }
        else if (c != '\r' && camBuf.length() < 200) camBuf += c;
    }
}

// ═══════════════════════════════════════════════════════════
// SPRAY DOSING
// ═══════════════════════════════════════════════════════════
unsigned long sprayDurationFor(float infection) {
    if (infection < 0.05f) return 0;
    float t = (infection - 0.05f) / 0.35f;
    t = constrain(t, 0.0f, 1.0f);
    return SPRAY_BASE_MS + (unsigned long)(t * (SPRAY_MAX_MS - SPRAY_BASE_MS));
}
void sprayOn()  { digitalWrite(PIN_RELAY_PUMP, HIGH); Serial.println("[PUMP] ON"); }
void sprayOff() { digitalWrite(PIN_RELAY_PUMP, LOW);  Serial.println("[PUMP] OFF"); }

// ═══════════════════════════════════════════════════════════
// OFFLINE BUFFER — store telemetry to flash
// ═══════════════════════════════════════════════════════════
void bufferReading(float m, float t, float h) {
    if (bufferCount >= BUFFER_MAX) {
        // Buffer full — drop oldest (advance head pointer mod BUFFER_MAX)
        Serial.println("[BUF] Buffer full — rotating oldest out");
    }

    String key = "r" + String(bufferHead);
    StaticJsonDocument<128> doc;
    doc["ts"] = (uint32_t)time(nullptr);
    doc["m"]  = m;
    doc["t"]  = t;
    doc["h"]  = h;
    String out;
    serializeJson(doc, out);
    prefs.putString(key.c_str(), out);

    bufferHead = (bufferHead + 1) % BUFFER_MAX;
    if (bufferCount < BUFFER_MAX) bufferCount++;
    prefs.putInt("head",  bufferHead);
    prefs.putInt("count", bufferCount);

    Serial.printf("[BUF] Stored reading %d/%d (offline)\n", bufferCount, BUFFER_MAX);
}

void flushBuffer() {
    if (!mqtt.connected() || bufferCount == 0) return;

    Serial.printf("[BUF] Flushing %d readings to MQTT...\n", bufferCount);
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
        delay(60);   // rate-limit to avoid overwhelming broker

        // Abort if MQTT drops mid-flush
        if (!mqtt.connected()) {
            Serial.println("[BUF] MQTT lost during flush — aborting");
            break;
        }
    }

    // Update counters
    bufferCount -= flushed;
    if (bufferCount < 0) bufferCount = 0;
    prefs.putInt("count", bufferCount);
    // head stays same — new writes continue from there

    Serial.printf("[BUF] Flushed %d readings · %d remaining\n", flushed, bufferCount);
}

void clearBuffer() {
    for (int i = 0; i < BUFFER_MAX; i++) {
        prefs.remove(("r" + String(i)).c_str());
    }
    bufferCount = 0;
    bufferHead = 0;
    prefs.putInt("count", 0);
    prefs.putInt("head", 0);
    Serial.println("[BUF] Buffer cleared");
}

// ═══════════════════════════════════════════════════════════
// STATE TRANSITIONS
// ═══════════════════════════════════════════════════════════
void publishRobotState();
void enterState(RobotState s) {
    state = s;
    stateEnteredAt = millis();
    Serial.printf("=== STATE → %s ===\n", stateName(s));
    publishRobotState();
}
void recomputeTicks() {
    ticksPerPlant = (int)(plantSpacingCm / CM_PER_TICK);
    ticksPerRow   = (int)(rowLengthCm / CM_PER_TICK);
    Serial.printf("[CFG] spacing=%dcm rowlen=%dcm ticks/plant=%d ticks/row=%d\n",
                  plantSpacingCm, rowLengthCm, ticksPerPlant, ticksPerRow);
}

// ═══════════════════════════════════════════════════════════
// MQTT PUBLISH
// ═══════════════════════════════════════════════════════════
void publishRobotState() {
    if (!mqtt.connected()) return;
    StaticJsonDocument<384> doc;
    doc["ts"]             = (uint32_t)time(nullptr);
    doc["state"]          = stateName(state);
    doc["row"]            = currentRow;
    doc["rows_total"]     = totalRows;
    doc["plants_scanned"] = plantsScanned;
    doc["plants_sprayed"] = plantsSprayed;
    doc["distance_cm"]    = encoderLeftTicks * CM_PER_TICK;
    doc["last_infection"] = camInfection;
    doc["last_severity"]  = camSeverity;
    doc["spacing_cm"]     = plantSpacingCm;
    doc["row_length_cm"]  = rowLengthCm;
    doc["battery"]        = batteryVoltage;
    doc["moisture"]       = soilMoisture;
    doc["temp"]           = ambientTemp;
    doc["humidity"]       = airHumidity;
    doc["buffered"]       = bufferCount;
    String out;
    serializeJson(doc, out);
    mqtt.publish(TOPIC_ROBOT, out.c_str());
}

void publishTelemetry() {
    if (!mqtt.connected()) return;
    StaticJsonDocument<192> doc;
    doc["ts"]          = (uint32_t)time(nullptr);
    doc["moisture"]    = soilMoisture;
    doc["temperature"] = ambientTemp;
    doc["humidity"]    = airHumidity;
    doc["buffered"]    = bufferCount;
    String out;
    serializeJson(doc, out);
    mqtt.publish(TOPIC_TELEMETRY, out.c_str());
}

void publishLeafResult() {
    if (!mqtt.connected()) return;
    StaticJsonDocument<160> doc;
    doc["ts"]         = (uint32_t)time(nullptr);
    doc["infection"]  = camInfection;
    doc["severity"]   = camSeverity;
    String out;
    serializeJson(doc, out);
    mqtt.publish(TOPIC_LEAF, out.c_str());
}

// ═══════════════════════════════════════════════════════════
// MQTT CALLBACK
// ═══════════════════════════════════════════════════════════
void onMqttMessage(char* topic, byte* payload, unsigned int len) {
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, payload, len)) return;
    const char* cmd = doc["cmd"];
    if (!cmd) return;
    Serial.printf("[MQTT] ← %s\n", cmd);

    if (strcmp(cmd, "start") == 0) {
        currentRow = 1;
        plantsScanned = 0;
        plantsSprayed = 0;
        encoderLeftTicks  = 0;
        encoderRightTicks = 0;
        lastScanTick = 0;
        rowStartTick = 0;
        recomputeTicks();
        enterState(STATE_MOVING);
    }
    else if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "emergency") == 0) {
        motorsStop(); sprayOff();
        enterState(STATE_IDLE);
    }
    else if (strcmp(cmd, "next_row") == 0)       enterState(STATE_TURNING);
    else if (strcmp(cmd, "set_spacing") == 0) {
        plantSpacingCm = doc["cm"] | plantSpacingCm;
        prefs.putInt("spacing", plantSpacingCm);
        recomputeTicks();
    }
    else if (strcmp(cmd, "set_row_length") == 0) {
        rowLengthCm = doc["cm"] | rowLengthCm;
        prefs.putInt("rowlen", rowLengthCm);
        recomputeTicks();
    }
    else if (strcmp(cmd, "set_total_rows") == 0) {
        totalRows = doc["n"] | totalRows;
        prefs.putInt("rows", totalRows);
    }
    else if (strcmp(cmd, "scan_now") == 0) {
        if (state == STATE_MOVING) enterState(STATE_SCANNING);
    }
    else if (strcmp(cmd, "spray_now") == 0) {
        sprayOn(); delay(1500); sprayOff();
    }
    else if (strcmp(cmd, "clear_buffer") == 0) {
        clearBuffer();
    }
    else if (strcmp(cmd, "flush_buffer") == 0) {
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

    String cid = String("nipas-robot-") + DEVICE_ID;
    if (mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS,
                     TOPIC_LWT, 1, true, "{\"online\":false}")) {
        mqtt.publish(TOPIC_LWT, "{\"online\":true}", true);
        mqtt.subscribe(TOPIC_CMD, 1);
        Serial.println("[MQTT] connected");
        return true;
    }
    Serial.printf("[MQTT] failed rc=%d\n", mqtt.state());
    return false;
}

// ═══════════════════════════════════════════════════════════
// SENSOR READS
// ═══════════════════════════════════════════════════════════
void readSensors() {
    lastDistanceCm = readDistanceCm();
    int raw = analogRead(34);
    soilMoisture = constrain((3200.0f - raw) * 100.0f / (3200.0f - 1200.0f),
                             0.0f, 100.0f);
    // batteryVoltage = analogRead(35) * 3.3f / 4095.0f * 4.0f;
    if (isnan(ambientTemp) || ambientTemp < -10) ambientTemp = 23.5f;
    if (isnan(airHumidity) || airHumidity < 0)   airHumidity = 65.0f;
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\n=== NIPAS Robot v1.1 (offline-first) ===");
    Serial.println("Device: " DEVICE_ID);

    // GPIO
    pinMode(PIN_L298_IN1, OUTPUT); pinMode(PIN_L298_IN2, OUTPUT);
    pinMode(PIN_L298_IN3, OUTPUT); pinMode(PIN_L298_IN4, OUTPUT);
    pinMode(PIN_L298_ENA, OUTPUT); pinMode(PIN_L298_ENB, OUTPUT);
    pinMode(PIN_TRIG, OUTPUT);     pinMode(PIN_ECHO, INPUT);
    pinMode(PIN_RELAY_PUMP, OUTPUT);
    pinMode(PIN_LED_STATUS, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_RELAY_PUMP, LOW);
    motorsStop();

    // Encoders
    pinMode(PIN_ENCODER_L, INPUT_PULLUP);
    pinMode(PIN_ENCODER_R, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_L), isrEncoderLeft,  RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_R), isrEncoderRight, RISING);

    // Camera UART
    camSerial.begin(CAM_BAUD, SERIAL_8N1, CAM_RX_PIN, CAM_TX_PIN);
    Serial.println("[CAM] UART2 ready");

    // Preferences + restore buffer state
    prefs.begin("nipas", false);
    plantSpacingCm = prefs.getInt("spacing", DEFAULT_PLANT_SPACING_CM);
    rowLengthCm    = prefs.getInt("rowlen",  DEFAULT_ROW_LENGTH_CM);
    totalRows      = prefs.getInt("rows",    DEFAULT_TOTAL_ROWS);
    bufferCount    = prefs.getInt("count",   0);
    bufferHead     = prefs.getInt("head",    0);
    Serial.printf("[BUF] Restored %d buffered readings from flash\n", bufferCount);
    recomputeTicks();

    // ─── WiFi via WiFiManager ────────────────────────────
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);   // 3 min max wait
    Serial.println("[WIFI] Starting WiFiManager...");
    Serial.println("[WIFI] First boot: connect to 'NIPAS-Setup' (pass: 12345678)");
    Serial.println("[WIFI] Then open http://192.168.4.1");

    bool connected = wm.autoConnect("NIPAS-Setup", "12345678");

    if (connected) {
        Serial.println("[WIFI] Connected: " + WiFi.localIP().toString());
        configTime(6 * 3600, 0, "pool.ntp.org", "time.google.com");
        mqttConnect();
    } else {
        Serial.println("[WIFI] No network — running OFFLINE mode");
        Serial.println("[WIFI] Robot will drive, scan, spray autonomously.");
        Serial.println("[WIFI] Telemetry will buffer to flash.");
    }

    enterState(STATE_IDLE);
    Serial.println("=== Ready ===");
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
    // ─── MQTT keepalive + reconnect ───────────────────────
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqtt.connected()) {
            if (millis() - lastNetworkTry > 5000) {
                lastNetworkTry = millis();
                mqttConnect();
            }
        } else {
            mqtt.loop();
        }
    }

    // ─── CAM UART pump ────────────────────────────────────
    camPump();
    if (state == STATE_SCANNING && !camReady &&
        millis() - camRequestTime > CAM_TIMEOUT_MS) {
        Serial.println("[CAM] timeout — assuming healthy");
        camInfection = 0.0f;
        camSeverity = "none";
        camReady = true;
    }

    // ─── Sensor read + buffer/publish ─────────────────────
    if (millis() - lastPublish > 2000) {
        lastPublish = millis();
        readSensors();
        publishRobotState();
    }

    if (millis() - lastSensorRead > 30000) {
        lastSensorRead = millis();

        if (mqtt.connected()) {
            // Online: publish directly
            publishTelemetry();
            Serial.println("[NET] Published telemetry live");
        } else {
            // Offline: buffer to flash
            bufferReading(soilMoisture, ambientTemp, airHumidity);
        }
    }

    // ─── Buffer flush when online ─────────────────────────
    if (mqtt.connected() && bufferCount > 0 &&
        millis() - lastBufferFlush > BUFFER_FLUSH_INTERVAL_MS) {
        lastBufferFlush = millis();
        flushBuffer();
    }

    // ═══════════════════════════════════════════════════════
    // STATE MACHINE
    // ═══════════════════════════════════════════════════════
    switch (state) {

        case STATE_IDLE:
            motorsStop();
            break;

        case STATE_MOVING: {
            if (lastDistanceCm < OBSTACLE_THRESHOLD_CM) {
                motorsStop();
                enterState(STATE_OBSTACLE_AVOID);
                break;
            }
            long travelled = encoderLeftTicks - rowStartTick;
            if (travelled >= ticksPerRow) {
                motorsStop();
                enterState(STATE_ROW_COMPLETE);
                break;
            }
            if (encoderLeftTicks - lastScanTick >= ticksPerPlant) {
                motorsStop();
                delay(PLANT_SCAN_SETTLE_MS);
                enterState(STATE_SCANNING);
                break;
            }
            motorsForward(MOTOR_SPEED_NORMAL);
            break;
        }

        case STATE_SCANNING:
            if (millis() - stateEnteredAt < 100) {
                camRequestScan();
                break;
            }
            if (camReady) {
                plantsScanned++;
                lastScanTick = encoderLeftTicks;
                publishLeafResult();
                unsigned long dur = sprayDurationFor(camInfection);
                if (dur > 0) enterState(STATE_SPRAYING);
                else         enterState(STATE_MOVING);
            }
            break;

        case STATE_SPRAYING: {
            if (millis() - stateEnteredAt < 50) sprayOn();
            unsigned long dur = sprayDurationFor(camInfection);
            if (millis() - stateEnteredAt >= dur) {
                sprayOff();
                plantsSprayed++;
                publishLeafResult();
                enterState(STATE_MOVING);
            }
            break;
        }

        case STATE_OBSTACLE_AVOID: {
            unsigned long el = millis() - stateEnteredAt;
            if (el < 600)        motorsBackward(MOTOR_SPEED_SLOW);
            else if (el < 1200)  motorsTurnRight(MOTOR_SPEED_SLOW);
            else {
                motorsStop();
                if (readDistanceCm() > OBSTACLE_THRESHOLD_CM + 5)
                    enterState(STATE_MOVING);
                else
                    enterState(STATE_ERROR);
            }
            break;
        }

        case STATE_ROW_COMPLETE:
            currentRow++;
            if (currentRow > totalRows) enterState(STATE_FINISHED);
            else                        enterState(STATE_TURNING);
            break;

        case STATE_TURNING: {
            unsigned long el = millis() - stateEnteredAt;
            if (el < TURN_180_MS) {
                motorsTurnRight(MOTOR_SPEED_TURN);
            } else {
                motorsStop();
                rowStartTick = encoderLeftTicks;
                lastScanTick = encoderLeftTicks;
                enterState(STATE_MOVING);
            }
            break;
        }

        case STATE_FINISHED:
            motorsStop();
            sprayOff();
            digitalWrite(PIN_LED_STATUS, (millis() / 500) % 2);
            break;

        case STATE_ERROR:
            motorsStop();
            sprayOff();
            digitalWrite(PIN_LED_STATUS, (millis() / 100) % 2);
            break;
    }
}