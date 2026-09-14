#include <WiFi.h>
#include <WiFiManager.h> // Includes captive portal functionality
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// Pin definitions for rover motor driver
const int IN1 = 26;
const int IN2 = 27;
const int ENA = 33;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

void stopRover() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  ledcWrite(0, 0); 
}

void moveForward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  ledcWrite(0, 200);
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    data[len] = 0;
    String command = String((char*)data);
    
    if (command == "FORWARD" || command == "PROBE_DROP") {
      moveForward();
    } else if (command == "STOP") {
      stopRover();
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  
  // ESP32 PWM configuration
  ledcSetup(0, 5000, 8);
  ledcAttachPin(ENA, 0);
  stopRover();

  // WiFiManager Captive Portal setup
  WiFiManager wm;
  bool res = wm.autoConnect("NIPAS-Setup", "12345678");

  if(!res) {
    Serial.println("Failed to connect or hit timeout");
  } else {
    Serial.println("Connected to Wi-Fi successfully!");
  }

  // WebSocket Server setup
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.begin();
}

void loop() {
  ws.cleanupClients();
}