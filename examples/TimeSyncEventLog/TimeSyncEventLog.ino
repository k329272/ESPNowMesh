#include "ESPNowMesh.h"

ESPNowMesh mesh;

struct LogEntry {
  uint32_t timestamp;
  const char* message;
};

std::vector<LogEntry> eventLog;

void onTimeSyncReceived(const TimeSync& sync) {
  Serial.printf("[TIME-SYNC] Quality: %d%%, Offset: %d ms\n", 
    sync.syncQuality, sync.timeOffset);
}

void logEvent(const char* message) {
  LogEntry entry;
  entry.timestamp = mesh.getSyncedTime();
  entry.message = message;
  eventLog.push_back(entry);
  
  Serial.printf("[%d ms] %s\n", entry.timestamp, message);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n=== Time-Synchronized Event Logger ===\n");
  
  mesh.begin("EventLogger");
  mesh.startDiscovery();
  mesh.enableAutoConnect();
  mesh.enableTimeSync(20000);  // Sync every 20 seconds
  
  mesh.onTimeSyncReceived(onTimeSyncReceived);
  
  logEvent("System started");
}

void loop() {
  delay(5000);
  
  // Log simulated events
  static uint32_t lastEvent = 0;
  if (millis() - lastEvent > 10000) {
    logEvent("Periodic event");
    lastEvent = millis();
  }
  
  // Print log with synchronized timestamps
  if (mesh.getConnectedDeviceCount() > 0) {
    Serial.println("\n=== Event Log ===");
    for (auto& entry : eventLog) {
      Serial.printf("[%d ms] %s\n", entry.timestamp, entry.message);
    }
  }
  
  // Verify sync quality
  uint8_t quality = mesh.getTimeSyncQuality();
  if (quality > 80) {
    Serial.println("[TIME-SYNC] High quality sync achieved");
  }
}