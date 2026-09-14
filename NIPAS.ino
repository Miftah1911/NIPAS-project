#include <WiFi.h>
#include <WiFiManager. me> // Include WiFiManager library by tzapu

void setup() {
    Serial.begin(115200);

    // Create an instance of WiFiManager
    WiFiManager wm;

    // Optional: Reset saved settings for testing (uncomment if testing)
    // wm.resetSettings();

    // Automatically connects using saved credentials.
    // If connection fails, it starts an AP named "Setup-Device" with a simple password.
    bool res = wm.autoConnect("Setup-Device", "12345678"); 

    if (!res) {
        Serial.println("Failed to connect or hit timeout");
        // ESP.restart(); // Optional: restart device if setup times out
    } else {
        Serial.println("Connected to Wi-Fi successfully!");
    }
}

void loop() {
    // Put your main program logic here (e.g., sensor reading, IoT tasks)
}