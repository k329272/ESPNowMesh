#include "ESPNowMesh.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

// Static instance
ESPNowMesh* ESPNowMesh::instance = nullptr;

ESPNowMesh::ESPNowMesh() 
  : discoveryRunning(false),
    lastDiscoveryTime(0),
    discoveryTimer(nullptr),
    wifiDisableTimer(nullptr),
    onMeshDataCallback(nullptr),
    onDeviceDiscoveredCallback(nullptr),
    onPathFoundCallback(nullptr) {
  instance = this;
}

ESPNowMesh::~ESPNowMesh() {
  stopDiscovery();
  esp_now_deinit();
}

void ESPNowMesh::begin(const char* deviceName) {
  // Initialize WiFi in STA mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  // Get device MAC address
  WiFi.macAddress(myMAC);
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[MESH] Failed to initialize ESP-NOW");
    return;
  }
  
  // Register callbacks
  esp_now_register_recv_cb(espNowOnReceive);
  esp_now_register_send_cb(espNowOnSent);
  
  Serial.print("[MESH] Initialized with MAC: ");
  char macStr[18];
  macToString(myMAC, macStr);
  Serial.println(macStr);
}

void ESPNowMesh::startDiscovery() {
  if (discoveryRunning) return;
  
  discoveryRunning = true;
  Serial.println("[MESH] Starting discovery");
  
  // Create timer for periodic discovery
  discoveryTimer = xTimerCreate(
    "MeshDiscovery",
    pdMS_TO_TICKS(MESH_DISCOVERY_INTERVAL),
    pdTRUE,
    (void*)this,
    [](TimerHandle_t xTimer) {
      ESPNowMesh* mesh = (ESPNowMesh*)pvTimerGetTimerID(xTimer);
      mesh->performDiscovery();
    }
  );
  
  xTimerStart(discoveryTimer, 0);
  performDiscovery();  // Initial discovery
}

void ESPNowMesh::stopDiscovery() {
  discoveryRunning = false;
  if (discoveryTimer) {
    xTimerStop(discoveryTimer, 0);
    xTimerDelete(discoveryTimer, 0);
    discoveryTimer = nullptr;
  }
  Serial.println("[MESH] Stopped discovery");
}

void ESPNowMesh::performDiscovery() {
  // Remove stale devices
  removeStaleDevices();
  
  // Broadcast discovery probe
  broadcastDiscoveryProbe();
  
  lastDiscoveryTime = millis();
}

void ESPNowMesh::broadcastDiscoveryProbe() {
  MeshMessage msg;
  msg.messageType = MSG_DISCOVERY_PROBE;
  copyMac(msg.sourceMAC, myMAC);
  memset(msg.destMAC, 0xFF, 6);  // Broadcast
  msg.hopCount = 0;
  msg.payloadSize = 0;
  msg.messageID = millis();
  
  // Broadcast to all peers
  uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  esp_now_send(broadcastMAC, (uint8_t*)&msg, sizeof(MeshMessage));
}

void ESPNowMesh::espNowOnReceive(const uint8_t* mac, const uint8_t* incomingData, int len) {
  if (instance == nullptr) return;
  
  if (len < sizeof(MeshMessage)) return;
  
  MeshMessage* msg = (MeshMessage*)incomingData;
  
  switch (msg->messageType) {
    case MSG_DISCOVERY_PROBE:
      instance->handleDiscoveryProbe(mac, msg);
      break;
    case MSG_DISCOVERY_RESPONSE:
      instance->handleDiscoveryResponse(mac, msg);
      break;
    case MSG_DATA:
      if (instance->onMeshDataCallback) {
        instance->onMeshDataCallback(msg->sourceMAC, msg->payload, msg->payloadSize);
      }
      break;
  }
}

void ESPNowMesh::espNowOnSent(const uint8_t* mac, esp_now_send_status_t status) {
  // Handle send completion
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.print("[MESH] Send failed to: ");
    char macStr[18];
    instance->macToString(mac, macStr);
    Serial.println(macStr);
  }
}

void ESPNowMesh::handleDiscoveryProbe(const uint8_t* senderMAC, const MeshMessage* msg) {
  // Don't respond to our own probes
  if (compareMac(senderMAC, myMAC)) return;
  
  // Send discovery response
  MeshMessage response;
  response.messageType = MSG_DISCOVERY_RESPONSE;
  copyMac(response.sourceMAC, myMAC);
  copyMac(response.destMAC, senderMAC);
  response.hopCount = msg->hopCount + 1;
  response.payloadSize = 0;
  response.messageID = msg->messageID;
  
  // Add peer if not exists
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, senderMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  esp_now_send(senderMAC, (uint8_t*)&response, sizeof(MeshMessage));
}

void ESPNowMesh::handleDiscoveryResponse(const uint8_t* senderMAC, const MeshMessage* msg) {
  // Get RSSI from WiFi scan (simplified - would need actual implementation)
  int16_t rssi = -60;  // Placeholder
  
  // Add or update device
  addOrUpdateDevice(senderMAC, rssi);
  
  char macStr[18];
  macToString(senderMAC, macStr);
  Serial.printf("[MESH] Discovered device: %s (RSSI: %d)\n", macStr, rssi);
}

MeshRoute ESPNowMesh::findOptimalPath(const uint8_t* destinationMAC) {
  // Check if destination exists
  char destStr[18];
  macToString(destinationMAC, destStr);
  
  auto it = deviceMap.find(destStr);
  if (it == deviceMap.end()) {
    Serial.printf("[MESH] Destination not found: %s\n", destStr);
    return MeshRoute();
  }
  
  // Calculate shortest path using Dijkstra's algorithm
  MeshRoute route = calculateShortestPath(destinationMAC);
  
  if (!route.path.empty() && onPathFoundCallback) {
    onPathFoundCallback(route);
  }
  
  return route;
}

std::vector<MeshRoute> ESPNowMesh::findAlternativePaths(const uint8_t* destinationMAC, uint8_t numPaths) {
  std::vector<MeshRoute> routes;
  
  // Simplified: return multiple paths with different hop counts
  for (uint8_t i = 0; i < numPaths; i++) {
    MeshRoute route = calculateShortestPath(destinationMAC);
    if (!route.path.empty()) {
      routes.push_back(route);
    }
  }
  
  return routes;
}

MeshRoute ESPNowMesh::calculateShortestPath(const uint8_t* destination) {
  MeshRoute route;
  route.path.push_back((uint8_t*)myMAC);  // Start with self
  route.hopCount = 1;
  route.timestamp = millis();
  
  // Dijkstra's algorithm implementation
  std::map<std::string, int16_t> distances;
  std::map<std::string, uint8_t*> previous;
  std::map<std::string, bool> visited;
  
  // Initialize distances
  for (auto& device : deviceMap) {
    distances[device.first] = INT16_MAX;
    visited[device.first] = false;
  }
  
  char myMacStr[18];
  macToString(myMAC, myMacStr);
  distances[myMacStr] = 0;
  
  // Dijkstra's main loop
  for (size_t i = 0; i < deviceMap.size(); i++) {
    // Find unvisited node with minimum distance
    std::string current;
    int16_t minDist = INT16_MAX;
    
    for (auto& dist : distances) {
      if (!visited[dist.first] && dist.second < minDist) {
        minDist = dist.second;
        current = dist.first;
      }
    }
    
    if (minDist == INT16_MAX) break;
    
    visited[current] = true;
    
    // Update neighbors
    for (auto& neighbor : deviceMap) {
      if (!visited[neighbor.first] && neighbor.second.isActive) {
        int16_t alt = distances[current] - neighbor.second.rssi;
        if (alt < distances[neighbor.first]) {
          distances[neighbor.first] = alt;
          previous[neighbor.first] = (uint8_t*)myMAC;
        }
      }
    }
  }
  
  // Reconstruct path to destination
  char destStr[18];
  macToString(destination, destStr);
  
  if (distances[destStr] != INT16_MAX) {
    route.signalStrength = distances[destStr];
    route.path.push_back((uint8_t*)destination);
  }
  
  return route;
}

int16_t ESPNowMesh::calculatePathQuality(const MeshRoute& route) {
  // Calculate overall path quality based on signal strength and hop count
  if (route.path.empty()) return 0;
  
  int16_t quality = 100;
  quality -= (route.hopCount * 10);  // Penalty for each hop
  quality += route.signalStrength;   // Bonus for signal strength
  
  return quality;
}

void ESPNowMesh::enableWiFiForPath(const MeshRoute& route, uint32_t durationMs) {
  enableWiFi();
  
  // Create timer to disable WiFi after duration
  if (wifiDisableTimer) {
    xTimerStop(wifiDisableTimer, 0);
    xTimerDelete(wifiDisableTimer, 0);
  }
  
  wifiDisableTimer = xTimerCreate(
    "WiFiDisable",
    pdMS_TO_TICKS(durationMs),
    pdFALSE,
    (void*)this,
    [](TimerHandle_t xTimer) {
      ESPNowMesh* mesh = (ESPNowMesh*)pvTimerGetTimerID(xTimer);
      mesh->disableWiFi();
    }
  );
  
  xTimerStart(wifiDisableTimer, 0);
  
  Serial.printf("[MESH] WiFi enabled for %d ms\n", durationMs);
}

void ESPNowMesh::enableWiFi() {
  WiFi.mode(WIFI_STA);
  Serial.println("[MESH] WiFi enabled");
}

void ESPNowMesh::disableWiFi() {
  WiFi.mode(WIFI_OFF);
  Serial.println("[MESH] WiFi disabled");
}

bool ESPNowMesh::sendData(const uint8_t* destMAC, const uint8_t* data, uint16_t length) {
  if (length > 200) {
    Serial.println("[MESH] Data too large");
    return false;
  }
  
  MeshMessage msg;
  msg.messageType = MSG_DATA;
  copyMac(msg.sourceMAC, myMAC);
  copyMac(msg.destMAC, destMAC);
  msg.hopCount = 0;
  msg.payloadSize = length;
  memcpy(msg.payload, data, length);
  msg.messageID = millis();
  
  // Add peer if not exists
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, destMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  
  return esp_now_send(destMAC, (uint8_t*)&msg, sizeof(MeshMessage)) == ESP_OK;
}

void ESPNowMesh::getMyMAC(uint8_t* macBuffer) {
  memcpy(macBuffer, myMAC, 6);
}

void ESPNowMesh::addOrUpdateDevice(const uint8_t* macAddress, int16_t rssi) {
  char macStr[18];
  macToString(macAddress, macStr);
  
  MeshDevice device;
  device.rssi = rssi;
  device.lastSeen = millis();
  device.hopCount = 1;
  device.isActive = true;
  device.hasWiFiCapability = true;  // Assume all have WiFi
  copyMac(device.macAddress, macAddress);
  
  deviceMap[macStr] = device;
  
  if (onDeviceDiscoveredCallback) {
    onDeviceDiscoveredCallback(device);
  }
}

void ESPNowMesh::removeStaleDevices() {
  uint32_t now = millis();
  const uint32_t STALE_TIMEOUT = 30000;  // 30 seconds
  
  for (auto it = deviceMap.begin(); it != deviceMap.end(); ) {
    if (now - it->second.lastSeen > STALE_TIMEOUT) {
      Serial.printf("[MESH] Removing stale device: %s\n", it->first.c_str());
      it = deviceMap.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<MeshDevice> ESPNowMesh::getDevices() {
  std::vector<MeshDevice> devices;
  for (auto& device : deviceMap) {
    if (device.second.isActive) {
      devices.push_back(device.second);
    }
  }
  return devices;
}

MeshDevice ESPNowMesh::getDeviceInfo(const uint8_t* macAddress) {
  char macStr[18];
  macToString(macAddress, macStr);
  
  auto it = deviceMap.find(macStr);
  if (it != deviceMap.end()) {
    return it->second;
  }
  
  return MeshDevice();
}

uint8_t ESPNowMesh::getDeviceCount() {
  return deviceMap.size();
}

int16_t ESPNowMesh::getAverageSignalStrength() {
  if (deviceMap.empty()) return 0;
  
  int32_t sum = 0;
  for (auto& device : deviceMap) {
    sum += device.second.rssi;
  }
  
  return sum / deviceMap.size();
}

void ESPNowMesh::printNetworkTopology() {
  Serial.println("\n[MESH] Network Topology:");
  Serial.printf("Devices: %d\n", getDeviceCount());
  Serial.printf("Average Signal: %d dBm\n", getAverageSignalStrength());
  Serial.println("Devices:");
  
  for (auto& device : deviceMap) {
    Serial.printf("  %s - RSSI: %d dBm, Hops: %d\n",
      device.first.c_str(),
      device.second.rssi,
      device.second.hopCount
    );
  }
  Serial.println();
}

void ESPNowMesh::onMeshData(void (*callback)(const uint8_t*, const uint8_t*, uint16_t)) {
  onMeshDataCallback = callback;
}

void ESPNowMesh::onDeviceDiscovered(void (*callback)(const MeshDevice&)) {
  onDeviceDiscoveredCallback = callback;
}

void ESPNowMesh::onPathFound(void (*callback)(const MeshRoute&)) {
  onPathFoundCallback = callback;
}

// Utility functions
void ESPNowMesh::macToString(const uint8_t* mac, char* buffer) {
  sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void ESPNowMesh::copyMac(uint8_t* dest, const uint8_t* src) {
  memcpy(dest, src, 6);
}

bool ESPNowMesh::compareMac(const uint8_t* mac1, const uint8_t* mac2) {
  return memcmp(mac1, mac2, 6) == 0;
}

bool ESPNowMesh::isMacAddressValid(const uint8_t* mac) {
  // Check if not all zeros and not all ones
  uint8_t sum = 0;
  for (int i = 0; i < 6; i++) {
    sum |= mac[i];
  }
  
  return sum != 0 && sum != 0xFF;
}