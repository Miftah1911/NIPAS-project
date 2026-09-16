/*
 * NIPAS_CAM.ino — ESP32-CAM vision module
 *
 * Waits for "SCAN\n" from the main ESP32 over UART.
 * Sweeps an SG90 servo 120° left-to-right.
 * Captures a frame at each position.
 * Runs HSV color analysis to detect chlorosis (yellow) and necrosis (brown).
 * Sends back a JSON result over UART.
 */

#include "esp_camera.h"
#include <ESP32Servo.h>
#include "NIPAS_Config.h"

// ═══════════════════════════════════════════════════════════
// AI-Thinker ESP32-CAM pins
// ═══════════════════════════════════════════════════════════
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define SERVO_PIN         12
#define SERVO_LEFT        30
#define SERVO_CENTER      90
#define SERVO_RIGHT       150

Servo camServo;
String uartBuf = "";

// ═══════════════════════════════════════════════════════════
// RGB → HSV
// ═══════════════════════════════════════════════════════════
void rgbToHsv(uint8_t r, uint8_t g, uint8_t b,
              float &h, float &s, float &v) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float mx = max(rf, max(gf, bf));
    float mn = min(rf, min(gf, bf));
    float d  = mx - mn;
    if (d == 0)         h = 0;
    else if (mx == rf)  h = 60 * fmod(((gf - bf) / d), 6);
    else if (mx == gf)  h = 60 * (((bf - rf) / d) + 2);
    else                h = 60 * (((rf - gf) / d) + 4);
    if (h < 0) h += 360;
    s = (mx == 0) ? 0 : (d / mx);
    v = mx;
}

// ═══════════════════════════════════════════════════════════
// HSV CLASSIFIERS for tomato / chili / brinjal
// ═══════════════════════════════════════════════════════════
bool isLeaf(float h, float s, float v) {
    // green leaf tissue
    return (h >= 70 && h <= 160) && s > 0.25f && v > 0.18f;
}
bool isChlorosis(float h, float s, float v) {
    // yellowing — H 40–75
    return (h >= 40 && h <= 75) && s > 0.35f && v > 0.35f;
}
bool isNecrosis(float h, float s, float v) {
    // brown/dark spots — H 10–40, low value
    return (h >= 10 && h <= 40) && (v < 0.55f || s < 0.40f);
}

// ═══════════════════════════════════════════════════════════
// ANALYSIS
// ═══════════════════════════════════════════════════════════
struct ScanResult {
    float leaf;
    float chlorosis;
    float necrosis;
};

ScanResult analyzeFrame(camera_fb_t* fb) {
    ScanResult r = {0, 0, 0};
    int leafPx = 0, chlorPx = 0, necrPx = 0;
    // PIXFORMAT_RGB565: 2 bytes per pixel
    for (size_t y = 0; y < fb->height; y += 2) {
        for (size_t x = 0; x < fb->width; x += 2) {
            size_t idx = (y * fb->width + x) * 2;
            if (idx + 1 >= fb->len) continue;
            uint16_t px = (fb->buf[idx] << 8) | fb->buf[idx + 1];
            uint8_t rr = ((px >> 11) & 0x1F) << 3;
            uint8_t gg = ((px >> 5)  & 0x3F) << 2;
            uint8_t bb = ( px        & 0x1F) << 3;
            float h, s, v;
            rgbToHsv(rr, gg, bb, h, s, v);
            if (isLeaf(h, s, v))      leafPx++;
            if (isChlorosis(h, s, v)) { leafPx++; chlorPx++; }
            if (isNecrosis(h, s, v))  { leafPx++; necrPx++;  }
        }
    }
    float denom = leafPx > 0 ? (float)leafPx : 1.0f;
    r.leaf      = leafPx;
    r.chlorosis = chlorPx / denom;
    r.necrosis  = necrPx  / denom;
    return r;
}

// ═══════════════════════════════════════════════════════════
// SWEEP + SCAN
// ═══════════════════════════════════════════════════════════
ScanResult scanAt(int angle) {
    camServo.write(angle);
    delay(400);                          // settle
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { ScanResult empty = {0,0,0}; return empty; }
    ScanResult r = analyzeFrame(fb);
    esp_camera_fb_return(fb);
    return r;
}

void doFullScan() {
    Serial.println("[CAM] scanning...");
    ScanResult L = scanAt(SERVO_LEFT);
    ScanResult C = scanAt(SERVO_CENTER);
    ScanResult R = scanAt(SERVO_RIGHT);
    camServo.write(SERVO_CENTER);

    // Worst-case (highest infection) side
    float chlor = max(L.chlorosis, max(C.chlorosis, R.chlorosis));
    float necr  = max(L.necrosis,  max(C.necrosis,  R.necrosis));
    float total = chlor + necr;

    const char* severity =
        total > 0.30f ? "high" :
        total > 0.15f ? "moderate" :
        total > 0.05f ? "low" : "none";

    Serial1.printf(
        "{\"infection\":%.3f,\"chlorosis\":%.3f,\"necrosis\":%.3f,\"severity\":\"%s\"}\n",
        total, chlor, necr, severity);
    Serial.printf("[CAM] → infection=%.3f severity=%s\n", total, severity);
}

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial1.begin(CAM_BAUD, SERIAL_8N1, 3, 1);   // RX=3, TX=1 on ESP32-CAM

    camServo.attach(SERVO_PIN);
    camServo.write(SERVO_CENTER);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_RGB565;   // required for HSV pixel loop
    config.frame_size   = FRAMESIZE_QQVGA;    // 160×120 — fast + fits RAM
    config.jpeg_quality = 12;
    config.fb_count     = 1;

    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("[CAM] init FAILED");
        return;
    }
    Serial.println("[CAM] ready");
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
    while (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
            uartBuf.trim();
            if (uartBuf == "SCAN") doFullScan();
            uartBuf = "";
        } else if (c != '\r' && uartBuf.length() < 60) {
            uartBuf += c;
        }
    }
}