#include "ESPNowMesh.h"

ESPNowMesh mesh;

enum SystemState {
  STATE_IDLE = 0,
  STATE_SYNCING = 1,
  STATE_READY = 2,
  STATE_OPERATING = 3
};

SystemState currentState = STATE_IDLE;

void transitionState(SystemState newState) {
  currentState = newState;
  
  switch(newState) {
    case STATE_IDLE:
      Serial.println("[STATE] Idle - Discovering devices...");
      break;
    case STATE_SYNCING:
      Serial.println("[STATE] Syncing - Establishing time sync...");
      break;
    case STATE_READY:
      Serial.println("[STATE] Ready - All systems connected and synced");
      break;
    case STATE_OPERATING:
      Serial.println("[STATE] Operating - System running");
      break;
  }
}

void onDeviceConnected(const MeshDevice& device) {
  Serial.println("[CONNECT] Device connected");
  
  // Transition to syncing when we have connections
  if (mesh.getConnectedDeviceCount() > 0 && currentState == STATE_IDLE) {
    transitionState(STATE_SYNCING);
  }
}

void onTimeSyncReceived(const TimeSync& sync) {
  // When sync quality is good, transition to ready
  if (sync.syncQuality > 80 && currentState == STATE_SYNCING) {
    transitionState(STATE_READY);
    transitionState(STATE_OPERATING);  // Auto-transition to operating
  }
}

void performOperatingTasks() {
  static uint32_t lastTask = 0;
  uint32_t now = millis();
  
  if (now - lastTask > 5000) {
    Serial.println("[TASK] Executing scheduled task");
    
    // Send status to all connected devices
    auto connected = mesh.getConnectedDevices();
    for (auto& device : connected) {
      const char* status = "System OK";
      mesh.sendInboxMessage(device.macAddress, (uint8_t*)status, strlen(status));
    }
    
    lastTask = now;
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n=== Coordinated Multi-Device System ===\n");
  
  mesh.begin("CoordNode");
  mesh.startDiscovery();
  mesh.enableAutoConnect(5000);
  mesh.enableTimeSync(30000);
  mesh.enableInboxSystem();
  
  mesh.onDeviceConnected(onDeviceConnected);
  mesh.onTimeSyncReceived(onTimeSyncReceived);
  
  transitionState(STATE_IDLE);
}

void loop() {
  delay(1000);
  
  switch(currentState) {
    case STATE_IDLE:
      // Waiting for connections
      if (mesh.getConnectedDeviceCount() > 0) {
        transitionState(STATE_SYNCING);
      }
      break;
      
    case STATE_SYNCING:
      // Waiting for time sync
      if (mesh.getTimeSyncQuality() > 80) {
        transitionState(STATE_READY);
        transitionState(STATE_OPERATING);
      }
      break;
      
    case STATE_OPERATING:
      performOperatingTasks();
      break;
      
    default:
      break;
  }
  
  // Print status every 10 seconds
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 10000) {
    Serial.printf("\n[STATUS] State: %d | Devices: %d | Quality: %d%%\n",
      currentState,
      mesh.getConnectedDeviceCount(),
      mesh.getTimeSyncQuality()
    );
    lastPrint = millis();
  }
}