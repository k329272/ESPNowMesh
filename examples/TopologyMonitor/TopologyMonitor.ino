#include "ESPNowMesh.h"

ESPNowMesh mesh;

void printNetworkTopology() {
  Serial.println("\n========== NETWORK TOPOLOGY ==========");
  
  uint8_t total = mesh.getDeviceCount();
  uint8_t connected = mesh.getConnectedDeviceCount();
  uint16_t avgSignal = mesh.getAverageSignalStrength();
  
  Serial.printf("Total Devices: %d\n", total);
  Serial.printf("Connected: %d\n", connected);
  Serial.printf("Average Signal: %d dBm\n", avgSignal);
  Serial.println();
  
  // List all devices
  auto devices = mesh.getDevices();
  for (size_t i = 0; i < devices.size(); i++) {
    Serial.printf("%d. ", i + 1);
    Serial.printf("RSSI: %d dBm | ", devices[i].rssi);
    Serial.printf("Hops: %d | ", devices[i].hopCount);
    Serial.printf("Connected: %s\n", 
      devices[i].isConnected ? "YES" : "NO");
  }
  
  // Print connection map
  Serial.println("\n--- Connection Map ---");
  mesh.printConnectionMap();
}

void exportTopology() {
  char jsonBuffer[1024];
  
  if (mesh.exportConnectionMapAsJSON(jsonBuffer, sizeof(jsonBuffer))) {
    Serial.println("\n--- Network as JSON ---");
    Serial.println(jsonBuffer);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n=== Network Topology Monitor ===\n");
  
  mesh.begin("TopologyMonitor");
  mesh.startDiscovery();
  mesh.enableAutoConnect(8000);  // Check every 8 seconds
}

void loop() {
  delay(10000);
  
  printNetworkTopology();
  
  // Try to query remote devices' maps
  auto connectedDevices = mesh.getConnectedDevices();
  if (connectedDevices.size() > 0) {
    // Query first connected device for its map
    mesh.queryConnectionMap(connectedDevices[0].macAddress);
  }
  
  exportTopology();
}
