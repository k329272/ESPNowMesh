/*
  Multi-Device Network with Power Management

  This example demonstrates:
  - Managing a mesh network with multiple devices
  - Power-efficient WiFi activation
  - Routing decisions based on signal quality
  - Network health monitoring
*/

#include "ESPNowMesh.h"

ESPNowMesh mesh;

// Device configuration
const char* DEVICE_NAME = "ESP32-Hub";
const uint32_t WIFI_ACTIVE_DURATION = 10000;  // 10 seconds per communication window
const uint32_t DISCOVERY_INTERVAL = 5000;     // 5 seconds

// Timing variables
unsigned long lastDiscoveryCheck = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastStatsReport = 0;

struct DeviceStats {
  uint8_t deviceCount;
  int16_t avgSignal;
  uint32_t discoveryInterval;
  uint32_t lastUpdate;
} stats = {0, 0, DISCOVERY_INTERVAL, 0};

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n[SETUP] ESP-NOW Mesh Network Example 3: Multi-Device Network");
  Serial.printf("[SETUP] Device: %s\n", DEVICE_NAME);
  Serial.printf("[SETUP] WiFi will be activated for %d ms per communication\n", WIFI_ACTIVE_DURATION);

  // Initialize mesh
  mesh.begin(DEVICE_NAME);

  // Setup device discovery callback
  mesh.onDeviceDiscovered([](const MeshDevice & device) {
    Serial.print("[EVENT] New device in range: ");
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
                  device.macAddress[0], device.macAddress[1], device.macAddress[2],
                  device.macAddress[3], device.macAddress[4], device.macAddress[5]
                 );
    Serial.printf("        Signal: %d dBm | Hops: %d\n",
                  device.rssi, device.hopCount
                 );
  });

  // Setup path found callback with WiFi activation
  mesh.onPathFound([](const MeshRoute & route) {
    if (!route.path.empty()) {
      Serial.print("[EVENT] Optimal path established with ");
      Serial.printf("%d hops (Signal: %d)\n",
                    route.hopCount, route.signalStrength
                   );
      Serial.println("[EVENT] Activating WiFi for communication window...");
    }
  });

  // Start discovery
  mesh.startDiscovery();

  Serial.println("[SETUP] Setup complete. Starting network monitoring.\n");
  stats.lastUpdate = millis();
}

void loop() {
  uint32_t now = millis();

  // Check network health every 20 seconds
  if (now - lastStatsReport > 20000) {
    lastStatsReport = now;
    reportNetworkStats();
  }

  // Demonstrate automatic WiFi activation for discovered devices
  if (now - lastWiFiCheck > 30000) {
    lastWiFiCheck = now;
    manageCommunicationWindow();
  }

  delay(100);
}

void reportNetworkStats() {
  Serial.println("\n========== NETWORK STATUS REPORT ==========");

  // Update statistics
  stats.deviceCount = mesh.getDeviceCount();
  stats.avgSignal = mesh.getAverageSignalStrength();
  stats.lastUpdate = millis();

  Serial.printf("Timestamp: %lu ms\n", stats.lastUpdate);
  Serial.printf("Devices in range: %d\n", stats.deviceCount);
  Serial.printf("Average signal strength: %d dBm\n", stats.avgSignal);

  if (stats.deviceCount > 0) {
    Serial.println("\nDevice Details:");
    std::vector<MeshDevice> devices = mesh.getDevices();

    for (const auto& device : devices) {
      Serial.printf("  - %02X:%02X:%02X:%02X:%02X:%02X\n",
                    device.macAddress[0], device.macAddress[1], device.macAddress[2],
                    device.macAddress[3], device.macAddress[4], device.macAddress[5]
                   );
      Serial.printf("    RSSI: %d dBm | Hops: %d | Active: %s\n",
                    device.rssi, device.hopCount,
                    device.isActive ? "Yes" : "No"
                   );
    }
  } else {
    Serial.println("No devices detected in mesh.");
  }

  Serial.println("\nNetwork topology\n");
  mesh.printNetworkTopology();

  Serial.println("\nNetwork GraphML (copy and paste into a .GRAPHML)\n");
  mesh.printNetworkGraphML();

  Serial.println("==========================================\n");
}

void manageCommunicationWindow() {
  Serial.println("\n[COMM] Starting communication window...");

  std::vector<MeshDevice> devices = mesh.getDevices();

  if (devices.empty()) {
    Serial.println("[COMM] No devices available for communication");
    return;
  }

  // Find the strongest connected device
  MeshDevice bestDevice = devices[0];
  for (const auto& device : devices) {
    if (device.rssi > bestDevice.rssi) {
      bestDevice = device;
    }
  }

  Serial.print("[COMM] Best device: ");
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X (RSSI: %d)\n",
                bestDevice.macAddress[0], bestDevice.macAddress[1], bestDevice.macAddress[2],
                bestDevice.macAddress[3], bestDevice.macAddress[4], bestDevice.macAddress[5],
                bestDevice.rssi
               );

  // Find path to best device
  MeshRoute route = mesh.findOptimalPath(bestDevice.macAddress);

  if (!route.path.empty()) {
    // Enable WiFi for this communication window
    mesh.enableWiFiForPath(route, WIFI_ACTIVE_DURATION);

    // Send test data
    const char* testData = "Health check from hub";
    if (mesh.sendData(bestDevice.macAddress, (uint8_t*)testData, strlen(testData))) {
      Serial.printf("[COMM] Sent: %s\n", testData);
    } else {
      Serial.println("[COMM] Failed to send health check");
    }
  } else {
    Serial.println("[COMM] No route available to best device");
  }

  Serial.printf("[COMM] Communication window closed (WiFi disabled in %d ms)\n",
                WIFI_ACTIVE_DURATION);
}
