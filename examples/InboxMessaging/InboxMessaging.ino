#include "ESPNowMesh.h"

ESPNowMesh mesh;

// Battery-optimized configuration
const uint32_t DISCOVERY_INTERVAL = 30000;    // 30 seconds
const uint32_t AUTO_CONNECT_INTERVAL = 60000; // 60 seconds
const uint32_t TIME_SYNC_INTERVAL = 120000;   // 2 minutes
const uint32_t SLEEP_DURATION = 10000;        // 10 seconds between tasks

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n=== Battery-Optimized Mesh Node ===\n");
  
  mesh.begin("BatteryNode");
  
  // Use long intervals to save power
  mesh.startDiscovery();
  mesh.enableAutoConnect(AUTO_CONNECT_INTERVAL);
  mesh.enableTimeSync(TIME_SYNC_INTERVAL);
  mesh.enableInboxSystem();
  
  // Configure connection retries for robustness
  mesh.setAutoConnectRetries(5);
  mesh.setAutoConnectTimeout(2000);
  
  Serial.println("[BATTERY] Low-power mode activated");
}

void loop() {
  // Long delays to reduce CPU activity
  delay(SLEEP_DURATION);
  
  // Only send data when connected
  if (mesh.getConnectedDeviceCount() > 0) {
    // Perform minimal necessary operations
    Serial.printf("[BATTERY] Connected to %d device(s)\n", 
      mesh.getConnectedDeviceCount());
    
    // Process any queued inbox messages
    auto inbox = mesh.getInboxMessages();
    if (inbox.size() > 0) {
      Serial.printf("[BATTERY] Processing %d inbox messages\n", inbox.size());
      mesh.clearInbox();
    }
  }
  
  // Print status less frequently to reduce Serial I/O
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus > 60000) {
    Serial.printf("[BATTERY] Time offset: %d ms, Quality: %d%%\n",
      mesh.getTimeOffset(),
      mesh.getTimeSyncQuality()
    );
    lastStatus = millis();
  }
}