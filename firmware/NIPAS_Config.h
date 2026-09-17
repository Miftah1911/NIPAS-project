/*
 * NIPAS_Config.h — shared configuration for both ESP32s
 * Edit these values and re-upload to both boards.
 */

#ifndef NIPAS_CONFIG_H
#define NIPAS_CONFIG_H

// ═══════════════════════════════════════════════════════════
// DEVICE IDENTITY
// ═══════════════════════════════════════════════════════════
#define DEVICE_ID       "nipas-robot-01"

// ═══════════════════════════════════════════════════════════
// NETWORK — WiFi + MQTT broker
// ═══════════════════════════════════════════════════════════
#define WIFI_SSID       "YourHomeWiFi"
#define WIFI_PASS       "YourWiFiPassword"

#define MQTT_HOST       "5245c93e5ad84e0085374820f1764e9a.s1.eu.hivemq.cloud"
#define MQTT_PORT       8883
#define MQTT_USER       "Isam1911"
#define MQTT_PASS       "Isam@191110"

// ═══════════════════════════════════════════════════════════
// MQTT TOPICS (auto-derived from DEVICE_ID)
// ═══════════════════════════════════════════════════════════
#define TOPIC_TELEMETRY "nipas/" DEVICE_ID "/telemetry"
#define TOPIC_STATUS    "nipas/" DEVICE_ID "/status"
#define TOPIC_ROBOT     "nipas/" DEVICE_ID "/robot"
#define TOPIC_LEAF      "nipas/" DEVICE_ID "/leaf"
#define TOPIC_CMD       "nipas/" DEVICE_ID "/cmd"
#define TOPIC_LWT       "nipas/" DEVICE_ID "/lwt"

// ═══════════════════════════════════════════════════════════
// MOTION PARAMETERS
// ═══════════════════════════════════════════════════════════
#define MOTOR_SPEED_NORMAL      140     // PWM 0-255
#define MOTOR_SPEED_SLOW        100
#define MOTOR_SPEED_TURN        130

#define WHEEL_DIAMETER_CM       6.5f    // MEASURE YOUR WHEEL
#define ENCODER_TICKS_PER_REV   20      // slotted holes per rotation
#define CM_PER_TICK             (3.14159f * WHEEL_DIAMETER_CM / ENCODER_TICKS_PER_REV)

#define OBSTACLE_THRESHOLD_CM   15
#define TURN_180_MS             1800    // CALIBRATE THIS

#define PLANT_SCAN_SETTLE_MS    400
#define SPRAY_BASE_MS           800
#define SPRAY_MAX_MS            2500

// ═══════════════════════════════════════════════════════════
// FIELD DEFAULTS (can be overridden from dashboard)
// ═══════════════════════════════════════════════════════════
#define DEFAULT_PLANT_SPACING_CM 40
#define DEFAULT_ROW_LENGTH_CM    500
#define DEFAULT_TOTAL_ROWS       5

// ═══════════════════════════════════════════════════════════
// CAM UART (ESP32-CAM link)
// ═══════════════════════════════════════════════════════════
#define CAM_RX_PIN              16
#define CAM_TX_PIN              17
#define CAM_BAUD                115200
#define CAM_TIMEOUT_MS          3000

#endif