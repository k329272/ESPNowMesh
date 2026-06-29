/*
  Pathfinding & WiFi Communication
  
  This example demonstrates:
  - Finding optimal paths to target devices
  - Enabling WiFi on devices in the optimal path
  - Sending data through the established path
  - Handling received messages
*/

#include "ESPNowMesh.h"

ESPNowMesh mesh;

// Target device to communicate with (set this to a known device MAC)
uint8_t targetDevice[6] = {0x24, 0x0A, 0xC4, 0x12, 0x34, 0x56};

unsigned long lastPathQuery = 0;
const unsigned long PATH_QUERY_INTERVAL = 15000;  // Query path every 15 seconds

unsigned long lastDataSend = 0;
const unsigned long DATA_SEND_INTERVAL = 5000;   // Send data every 5 seconds

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n[SETUP] ESP-NOW Mesh Network Example 2: Pathfinding");

    // Initialize mesh
    mesh.begin("ESP32-Device-2");

    // Register data received callback
    mesh.onMeshData([](const uint8_t* sourceMac, const uint8_t* data, uint16_t length) {
    Serial.print("[DATA RECEIVED] From: ");
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X - Message: ",
        sourceMac[0], sourceMac[1], sourceMac[2],
        sourceMac[3], sourceMac[4], sourceMac[5]
    );

    // Print data as string
    for (uint16_t i = 0; i < length; i++) {
        Serial.write(data[i]);
    }
    Serial.println();
    });
}
  
void update() {
    return;
}
