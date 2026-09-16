/*
 * NIPAS_Calibration.ino — Standalone sensor calibration tool
 * Target: ESP32 Dev Board (with sensors wired, but motors optional)
 *
 * Flash this FIRST to a bare ESP32 to find calibration values.
 * Open Serial Monitor at 115200 baud.
 *
 * Prints every second:
 *   - Raw soil moisture ADC value (hold in air, then in water)
 *   - Ultrasonic distance in cm (test with a ruler)
 *   - Encoder tick count (roll wheel by hand)
 *   - Battery voltage (if voltage divider wired)
 *   - Servo sweep test (0° → 90° → 0°)
 *
 * Use the printed values to fill in NIPAS_Config.h and NIPAS_Robot.ino.
 */

// ═══════════════════════════════════════════════════════════
// PINS — must match NIPAS_Robot.ino
// ═══════════════════════════════════════════════════════════
#define PIN_SOIL_MOISTURE   34
#define PIN_TRIG             5
#define PIN_ECHO            18
#define PIN_ENCODER_L       35
#define PIN_ENCODER_R       36
#define PIN_BATTERY         39
#define PIN_SERVO           13

// Set to 1 to test the servo sweep (uses GPIO 13)
#define TEST_SERVO          0

// ═══════════════════════════════════════════════════════════
// ENCODER TICKS
// ═══════════════════════════════════════════════════════════
volatile long encoderLeftTicks  = 0;
volatile long encoderRightTicks = 0;

void IRAM_ATTR isrEncoderLeft()  { encoderLeftTicks++; }
void IRAM_ATTR isrEncoderRight() { encoderRightTicks++; }

// ═══════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════
float readSoilRaw() {
    uint32_t sum = 0;
    for (int i = 0; i < 10; i++) { sum += analogRead(PIN_SOIL_MOISTURE); delay(5); }
    return sum / 10.0f;
}

float readDistanceCm() {
    digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    long dur = pulseIn(PIN_ECHO, HIGH, 25000);
    if (dur == 0) return -1;
    return dur * 0.0343f / 2.0f;
}

float readBatteryVoltage() {
    // Assumes a 2:1 or 4:1 voltage divider — adjust multiplier
    int raw = analogRead(PIN_BATTERY);
    return raw * 3.3f / 4095.0f * 4.0f;
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n═══════════════════════════════════════════");
    Serial.println("  NIPAS Calibration Tool");
    Serial.println("═══════════════════════════════════════════");
    Serial.println("Open Serial Monitor at 115200 baud.");
    Serial.println("");

    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    pinMode(PIN_ENCODER_L, INPUT_PULLUP);
    pinMode(PIN_ENCODER_R, INPUT_PULLUP);
    pinMode(PIN_SOIL_MOISTURE, INPUT);
    pinMode(PIN_BATTERY, INPUT);

    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_L), isrEncoderLeft,  RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_R), isrEncoderRight, RISING);

    Serial.println("Sensors ready. Reading every 1 second...\n");
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
unsigned long lastPrint = 0;

void loop() {
    if (millis() - lastPrint >= 1000) {
        lastPrint = millis();

        float soil   = readSoilRaw();
        float dist   = readDistanceCm();
        float batt   = readBatteryVoltage();

        Serial.println("──────────────────────────────────────────");
        Serial.printf("  Soil moisture raw :  %.0f\n", soil);
        Serial.printf("  Soil percent      :  %.1f %%\n",
                      constrain((3200.0f - soil) * 100.0f / (3200.0f - 1200.0f), 0, 100));
        Serial.printf("  Distance (cm)     :  %.1f\n", dist);
        Serial.printf("  Encoder L ticks   :  %ld\n", encoderLeftTicks);
        Serial.printf("  Encoder R ticks   :  %ld\n", encoderRightTicks);
        Serial.printf("  Battery voltage   :  %.2f V\n", batt);
        Serial.println("");

        Serial.println("  Tip: hold moisture sensor in AIR → note raw value (DRY)");
        Serial.println("       dip in WATER → note raw value (WET)");
        Serial.println("       update NIPAS_Robot.ino readSensors() accordingly");
        Serial.println("");
    }

#if TEST_SERVO
    // Sweep servo from 0° to 90° and back
    static int angle = 0;
    static int dir = 1;
    static unsigned long lastServo = 0;
    if (millis() - lastServo >= 20) {
        lastServo = millis();
        angle += dir * 2;
        if (angle >= 90) { angle = 90; dir = -1; }
        if (angle <= 0)  { angle = 0;  dir = 1;  }
        // Servo control requires ESP32Servo library — comment out if not installed
        // servo.write(angle);
    }
#endif
}