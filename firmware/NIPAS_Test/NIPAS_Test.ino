/*
 * NIPAS_Test.ino — Hardware diagnostic tool
 * Target: ESP32 Dev Board (with all hardware wired)
 *
 * Runs a sequence of tests, one per second:
 *   1. Blink status LED 3×
 *   2. Beep buzzer 3×
 *   3. Pulse relay 3× (listen for click)
 *   4. Sweep servo 0° → 90° → 0°
 *   5. Drive motors forward 1s, backward 1s, stop
 *   6. Read ultrasonic 5× (place an object at known distance)
 *   7. Read soil moisture 5× (test in air and water)
 *   8. Print encoder tick counts (roll the wheels by hand)
 *
 * Serial output tells you exactly which test is running.
 * If something fails, you know exactly where to look.
 */

#include <ESP32Servo.h>

// ═══════════════════════════════════════════════════════════
// PINS — must match NIPAS_Robot.ino
// ═══════════════════════════════════════════════════════════
#define PIN_L298_IN1    25
#define PIN_L298_IN2    26
#define PIN_L298_IN3    27
#define PIN_L298_IN4    14
#define PIN_L298_ENA    32
#define PIN_L298_ENB    33
#define PIN_TRIG         5
#define PIN_ECHO        18
#define PIN_ENCODER_L   34
#define PIN_ENCODER_R   35
#define PIN_RELAY_PUMP  13
#define PIN_LED_STATUS   2
#define PIN_BUZZER       4
#define PIN_SERVO       12

Servo probeServo;

volatile long encL = 0, encR = 0;
void IRAM_ATTR isrL() { encL++; }
void IRAM_ATTR isrR() { encR++; }

// ═══════════════════════════════════════════════════════════
// MOTOR HELPERS
// ═══════════════════════════════════════════════════════════
void motorsStop() {
    digitalWrite(PIN_L298_IN1, LOW); digitalWrite(PIN_L298_IN2, LOW);
    digitalWrite(PIN_L298_IN3, LOW); digitalWrite(PIN_L298_IN4, LOW);
    analogWrite(PIN_L298_ENA, 0);    analogWrite(PIN_L298_ENB, 0);
}
void motorsForward() {
    digitalWrite(PIN_L298_IN1, HIGH); digitalWrite(PIN_L298_IN2, LOW);
    digitalWrite(PIN_L298_IN3, HIGH); digitalWrite(PIN_L298_IN4, LOW);
    analogWrite(PIN_L298_ENA, 140);   analogWrite(PIN_L298_ENB, 140);
}
void motorsBackward() {
    digitalWrite(PIN_L298_IN1, LOW);  digitalWrite(PIN_L298_IN2, HIGH);
    digitalWrite(PIN_L298_IN3, LOW);  digitalWrite(PIN_L298_IN4, HIGH);
    analogWrite(PIN_L298_ENA, 140);   analogWrite(PIN_L298_ENB, 140);
}

// ═══════════════════════════════════════════════════════════
// ULTRASONIC
// ═══════════════════════════════════════════════════════════
float readDistanceCm() {
    digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    long dur = pulseIn(PIN_ECHO, HIGH, 25000);
    if (dur == 0) return -1;
    return dur * 0.0343f / 2.0f;
}

// ═══════════════════════════════════════════════════════════
// INDIVIDUAL TESTS
// ═══════════════════════════════════════════════════════════
void testLED() {
    Serial.println("─── TEST 1 · Status LED ───");
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_LED_STATUS, HIGH);
        Serial.println("    LED → ON");
        delay(300);
        digitalWrite(PIN_LED_STATUS, LOW);
        Serial.println("    LED → OFF");
        delay(300);
    }
    Serial.println("    ✓ LED test done\n");
}

void testBuzzer() {
    Serial.println("─── TEST 2 · Buzzer ───");
    for (int i = 0; i < 3; i++) {
        tone(PIN_BUZZER, 1000);
        delay(150);
        noTone(PIN_BUZZER);
        delay(150);
    }
    Serial.println("    ✓ Buzzer test done — did you hear 3 beeps?\n");
}

void testRelay() {
    Serial.println("─── TEST 3 · Relay / Pump ───");
    for (int i = 0; i < 3; i++) {
        digitalWrite(PIN_RELAY_PUMP, HIGH);
        Serial.println("    Relay → ON (listen for click)");
        delay(500);
        digitalWrite(PIN_RELAY_PUMP, LOW);
        Serial.println("    Relay → OFF");
        delay(500);
    }
    Serial.println("    ✓ Relay test done — did you hear 3 clicks?\n");
}

void testServo() {
    Serial.println("─── TEST 4 · Servo Sweep ───");
    Serial.println("    Servo → 0°");
    probeServo.write(0);   delay(800);
    Serial.println("    Servo → 90°");
    probeServo.write(90);  delay(800);
    Serial.println("    Servo → 45°");
    probeServo.write(45);  delay(800);
    Serial.println("    Servo → 0°");
    probeServo.write(0);   delay(800);
    Serial.println("    ✓ Servo test done — did it move?\n");
}

void testMotors() {
    Serial.println("─── TEST 5 · Motors ───");
    Serial.println("    Forward (1s)...");
    motorsForward();
    delay(1000);
    motorsStop();
    delay(500);
    Serial.println("    Backward (1s)...");
    motorsBackward();
    delay(1000);
    motorsStop();
    Serial.println("    ✓ Motor test done — did both wheels spin?\n");
}

void testUltrasonic() {
    Serial.println("─── TEST 6 · Ultrasonic ───");
    Serial.println("    Place an object 20 cm ahead of the sensor");
    for (int i = 0; i < 5; i++) {
        float d = readDistanceCm();
        if (d < 0) Serial.println("    ✗ No echo — check wiring");
        else       Serial.printf("    Distance: %.1f cm\n", d);
        delay(400);
    }
    Serial.println("    ✓ Ultrasonic test done\n");
}

void testMoisture() {
    Serial.println("─── TEST 7 · Soil Moisture ───");
    for (int i = 0; i < 5; i++) {
        int raw = analogRead(34);
        Serial.printf("    Raw ADC: %d\n", raw);
        delay(300);
    }
    Serial.println("    (Hold in air → note DRY value)");
    Serial.println("    (Dip in water → note WET value)");
    Serial.println("    ✓ Moisture test done\n");
}

void testEncoders() {
    Serial.println("─── TEST 8 · Encoders ───");
    Serial.println("    Roll both wheels by hand for 5 seconds...");
    long startL = encL, startR = encR;
    delay(5000);
    Serial.printf("    Left encoder ticks  : %ld\n", encL - startL);
    Serial.printf("    Right encoder ticks : %ld\n", encR - startR);
    if (encL - startL > 0 && encR - startR > 0)
        Serial.println("    ✓ Both encoders working\n");
    else
        Serial.println("    ✗ One or both encoders not counting — check wiring\n");
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n═══════════════════════════════════════════");
    Serial.println("  NIPAS Hardware Diagnostic");
    Serial.println("═══════════════════════════════════════════\n");

    pinMode(PIN_L298_IN1, OUTPUT); pinMode(PIN_L298_IN2, OUTPUT);
    pinMode(PIN_L298_IN3, OUTPUT); pinMode(PIN_L298_IN4, OUTPUT);
    pinMode(PIN_L298_ENA, OUTPUT); pinMode(PIN_L298_ENB, OUTPUT);
    pinMode(PIN_TRIG, OUTPUT);     pinMode(PIN_ECHO, INPUT);
    pinMode(PIN_RELAY_PUMP, OUTPUT);
    pinMode(PIN_LED_STATUS, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_ENCODER_L, INPUT_PULLUP);
    pinMode(PIN_ENCODER_R, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_L), isrL, RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_R), isrR, RISING);

    probeServo.attach(PIN_SERVO);
    probeServo.write(0);

    motorsStop();
    digitalWrite(PIN_RELAY_PUMP, LOW);

    delay(1000);
    Serial.println("Starting tests in 3 seconds...\n");
    delay(3000);
}

// ═══════════════════════════════════════════════════════════
// LOOP — run all tests, then pause and repeat
// ═══════════════════════════════════════════════════════════
void loop() {
    testLED();
    testBuzzer();
    testRelay();
    testServo();
    testMotors();
    testUltrasonic();
    testMoisture();
    testEncoders();

    Serial.println("═══════════════════════════════════════════");
    Serial.println("  All tests complete");
    Serial.println("  Repeating in 10 seconds...");
    Serial.println("═══════════════════════════════════════════\n");
    delay(10000);
}