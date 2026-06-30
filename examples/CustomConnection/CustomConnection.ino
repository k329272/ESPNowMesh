#include "ESPNowMesh.h"

ESPNowMesh mesh;

// Custom connection manager for specific use cases
class SmartConnectionManager {
private:
  std::map<std::string, uint32_t> connectionTimeouts;
  
public:
  void updateStrategy() {
    auto devices = mesh.getDevices();
    
    for (auto& device : devices) {
      if (!device.isConnected && device.rssi > -70) {
        // Strong signal but not connected - reconnect
        mesh.forceReconnectToDevice(device.macAddress);
      } else if (device.isConnected && device.rssi < -85) {
        // Connected but weak signal - monitor closely
        Serial.println("[STRATEGY] Warning: weak signal on connected device");
      }
    }
  }
  
  bool shouldPrioritizeDevice(const MeshDevice& device) {
    // Prioritize strongest signal devices
    return device.rssi > -70 && device.isConnected;
  }
};

SmartConnectionManager connManager;

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n=== Smart Connection Strategy ===\n");
  
  mesh.begin("SmartNode");
  mesh.startDiscovery();
  mesh.enableAutoConnect(5000);
}

void loop() {
  delay(3000);
  
  // Update connection strategy periodically
  connManager.updateStrategy();
  
  // Get best device to communicate with
  auto devices = mesh.getDevices();
  for (auto& device : devices) {
    if (connManager.shouldPrioritizeDevice(device)) {
      Serial.printf("[STRATEGY] Prioritizing device with RSSI: %d\n", device.rssi);
      break;
    }
  }
}