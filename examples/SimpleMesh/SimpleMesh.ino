#include "ESPNowMesh.h"

ESPNowMesh mesh;

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n=== Simple Auto-Connecting Mesh ===\n");
  
  // Initialize mesh
  mesh.begin("SimpleNode");
  mesh.startDiscovery();
  mesh.enableAutoConnect();
  
  // Register callback
  mesh.onDeviceConnected([](const MeshDevice& device) {
    Serial.println("[AUTO-CONNECT] Device connected!");
  });
}

void loop() {
  delay(2000);
  
  // Print connection status
  uint8_t total = mesh.getDeviceCount();
  uint8_t connected = mesh.getConnectedDeviceCount();
  
  Serial.printf("Devices: %d/%d connected\n", connected, total);
  
  // Get all devices
  auto devices = mesh.getConnectedDevices();
  for (auto& dev : devices) {
    Serial.printf("  Connected to device with RSSI: %d dBm\n", dev.rssi);
  }
}