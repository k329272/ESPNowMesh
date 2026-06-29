/*
  Basic Setup with Discovery

  This example demonstrates:
  - Initializing the mesh network
  - Starting periodic discovery
  - Monitoring connected devices
  - Printing network topology
*/

#include "ESPNowMesh.h"

ESPNowMesh mesh;
unsigned long lastTopologyPrint = 0;
const unsigned long TOPOLOGY_PRINT_INTERVAL = 10000;  // Print every 10 seconds

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n[SETUP] ESP-NOW Mesh Network Example 1: Basic Setup");

  // Initialize mesh with device name
  mesh.begin("ESP32-Device-1");

  // Register callbacks
  mesh.onDeviceDiscovered([](const MeshDevice & device) {
    Serial.print("[CALLBACK] Device discovered: ");
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X - RSSI: %d dBm\n",
                  device.macAddress[0], device.macAddress[1], device.macAddress[2],
                  device.macAddress[3], device.macAddress[4], device.macAddress[5],
                  device.rssi
                 );
  });

  // Start mesh discovery
  mesh.startDiscovery();

  Serial.println("[SETUP] Mesh initialized and discovery started");
  Serial.println("[SETUP] Waiting for device discovery...\n");
}

void loop() {
  delay(100);

  // Print network topology periodically
  if (millis() - lastTopologyPrint > TOPOLOGY_PRINT_INTERVAL) {
    lastTopologyPrint = millis();
    mesh.printNetworkTopology();
  }
}
