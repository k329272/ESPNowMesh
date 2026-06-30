#include "ESPNowMesh.h"

ESPNowMesh mesh;

struct DiagnosticData {
  uint32_t uptime;
  uint8_t totalDevices;
  uint8_t connectedDevices;
  int16_t avgSignal;
  int32_t timeOffset;
  uint8_t timeSyncQuality;
  uint16_t inboxSize;
};

DiagnosticData getCurrentDiagnostics() {
  DiagnosticData diag;
  diag.uptime = millis();
  diag.totalDevices = mesh.getDeviceCount();
  diag.connectedDevices = mesh.getConnectedDeviceCount();
  diag.avgSignal = mesh.getAverageSignalStrength();
  diag.timeOffset = mesh.getTimeOffset();
  diag.timeSyncQuality = mesh.getTimeSyncQuality();
  diag.inboxSize = mesh.getInboxSize();
  return diag;
}

void printDiagnosticsDashboard() {
  DiagnosticData diag = getCurrentDiagnostics();
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   ESPNowMesh Diagnostic Dashboard     ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.printf("║ Uptime:              %13d ms   ║\n", diag.uptime);
  Serial.printf("║ Total Devices:       %14d    ║\n", diag.totalDevices);
  Serial.printf("║ Connected Devices:   %14d    ║\n", diag.connectedDevices);
  Serial.printf("║ Average Signal:      %14d dBm ║\n", diag.avgSignal);
  Serial.printf("║ Time Offset:         %14d ms  ║\n", diag.timeOffset);
  Serial.printf("║ Sync Quality:        %14d %% ║\n", diag.timeSyncQuality);
  Serial.printf("║ Inbox Messages:      %14d    ║\n", diag.inboxSize);
  Serial.println("╚════════════════════════════════════════╝\n");
}

void printDetailedDeviceInfo() {
  auto devices = mesh.getDevices();
  
  Serial.println("\n┌─ Connected Devices ─────────────────────┐");
  for (size_t i = 0; i < devices.size(); i++) {
    if (devices[i].isConnected) {
      Serial.printf("│ Device %d:\n", i + 1);
      Serial.printf("│   RSSI: %d dBm\n", devices[i].rssi);
      Serial.printf("│   Hops: %d\n", devices[i].hopCount);
      Serial.printf("│   Connected: %d ms ago\n", 
        millis() - devices[i].connectedSince);
    }
  }
  Serial.println("└─────────────────────────────────────────┘\n");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n=== Debug & Diagnostic Dashboard ===\n");
  
  mesh.begin("DiagnosticNode");
  mesh.startDiscovery();
  mesh.enableAutoConnect();
  mesh.enableTimeSync();
  mesh.enableInboxSystem();
}

void loop() {
  delay(5000);
  
  printDiagnosticsDashboard();
  printDetailedDeviceInfo();
  
  // Also export as JSON for external monitoring
  char jsonBuffer[512];
  if (mesh.exportConnectionMapAsJSON(jsonBuffer, sizeof(jsonBuffer))) {
    Serial.println("Connection Map (JSON):");
    Serial.println(jsonBuffer);
  }
}