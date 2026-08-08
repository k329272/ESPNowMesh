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
    onPathFoundCallback(nullptr),
    _configSyncEnabled(true),
    _configSynced(false),
    _configSignature(0),
    _hasConfigSource(false) {
  instance = this;
  memset(_configSourceMac, 0, sizeof(_configSourceMac));
  memset(&_meshConfig, 0, sizeof(_meshConfig));
}

ESPNowMesh::~ESPNowMesh() {
  stopDiscovery();
  esp_now_deinit();
}

void ESPNowMesh::begin(const char* deviceName, 
                      uint8_t meshMaxDevices,
                      uint32_t meshDiscoveryInterval,
                      int16_t meshRSSIThreshold,
                      uint8_t meshMaxHops,
                      uint32_t wifiEnableDuration,
                      bool enableConfigSync) {
  
  // Store the configuration variables
  _deviceName = String(deviceName);
  _maxDevices = meshMaxDevices;
  _discoveryInterval = meshDiscoveryInterval;
  _rssiThreshold = meshRSSIThreshold;
  _maxHops = meshMaxHops;
  _wifiEnableDuration = wifiEnableDuration;
  _configSyncEnabled = enableConfigSync;
  _configSynced = false;

  _meshConfig.maxDevices = meshMaxDevices;
  _meshConfig.discoveryInterval = meshDiscoveryInterval;
  _meshConfig.rssiThreshold = meshRSSIThreshold;
  _meshConfig.maxHops = meshMaxHops;
  _meshConfig.wifiEnableDuration = wifiEnableDuration;
  _meshConfig.configFlags = enableConfigSync ? 0x01 : 0x00;
  _meshConfig.configVersion = (_meshConfig.maxDevices == 20 &&
                               _meshConfig.discoveryInterval == 5000 &&
                               _meshConfig.rssiThreshold == -85 &&
                               _meshConfig.maxHops == 10 &&
                               _meshConfig.wifiEnableDuration == 10000)
                              ? 1
                              : 2;
  _configSignature = meshConfigSignature(_meshConfig);

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
  #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  esp_now_register_recv_cb((esp_now_recv_cb_t)espNowOnReceive);
  esp_now_register_send_cb((esp_now_send_cb_t)espNowOnSent);
  #else
  esp_now_register_recv_cb(espNowOnReceive);
  esp_now_register_send_cb(espNowOnSent);
  #endif
  
  Serial.print("[MESH] Initialized with MAC: ");
  char macStr[18];
  macToString(myMAC, macStr);
  Serial.println(macStr);

  if (_configSyncEnabled) {
    requestMeshConfigSync();
  }
}

void ESPNowMesh::startDiscovery() {
  if (discoveryRunning) return;
  
  discoveryRunning = true;
  Serial.println("[MESH] Starting discovery");
  
  discoveryTimer = xTimerCreate(
    "MeshDiscovery",
    pdMS_TO_TICKS(_discoveryInterval),
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
  msg.messageID = millis();
  packMeshConfig(msg, MSG_DISCOVERY_PROBE);
  
  // Broadcast to all peers
  uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  esp_now_send(broadcastMAC, (uint8_t*)&msg, sizeof(MeshMessage));
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void ESPNowMesh::espNowOnReceive(const esp_now_recv_info_t* recv_info, const uint8_t* incomingData, int len) {
  if (instance == nullptr) return;
  const uint8_t* mac = recv_info->src_addr; // Extract the source MAC address
#else
void ESPNowMesh::espNowOnReceive(const uint8_t* mac, const uint8_t* incomingData, int len) {
  if (instance == nullptr) return;
#endif

  if (len < sizeof(MeshMessage)) return;
  
  MeshMessage* msg = (MeshMessage*)incomingData;
  
  switch (msg->messageType) {
    case MSG_DISCOVERY_PROBE:
      instance->handleDiscoveryProbe(mac, msg);
      break;
    case MSG_DISCOVERY_RESPONSE:
      instance->handleDiscoveryResponse(mac, msg);
      break;
    case MSG_CONFIG_ADVERTISEMENT:
      instance->handleConfigAdvertisement(mac, msg);
      break;
    case MSG_CONFIG_REQUEST:
      instance->handleConfigRequest(mac, msg);
      break;
    case MSG_CONFIG_SYNC:
      instance->handleConfigSync(mac, msg);
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

  if (_configSyncEnabled) {
    handleConfigAdvertisement(senderMAC, msg);
  }
  
  // Enforce the maximum hop limit
  if (msg->hopCount >= _maxHops) {
    return; 
  }
  
  // Send discovery response
  MeshMessage response;
  response.messageType = MSG_DISCOVERY_RESPONSE;
  copyMac(response.sourceMAC, myMAC);
  copyMac(response.destMAC, senderMAC);
  response.hopCount = msg->hopCount + 1;
  response.messageID = msg->messageID;
  packMeshConfig(response, MSG_DISCOVERY_RESPONSE);
  
  // Add peer if not exists
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, senderMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  esp_now_send(senderMAC, (uint8_t*)&response, sizeof(MeshMessage));
}

void ESPNowMesh::handleDiscoveryResponse(const uint8_t* senderMAC, const MeshMessage* msg) {
  if (_configSyncEnabled) {
    handleConfigAdvertisement(senderMAC, msg);
  }

  int16_t rssi = -60;  // Placeholder
  
  // Enforce RSSI Threshold
  if (rssi < _rssiThreshold) {
    Serial.println("[MESH] Device ignored due to low signal strength");
    return;
  }
  
  // Add or update device
  addOrUpdateDevice(senderMAC, rssi);
  
  char macStr[18];
  macToString(senderMAC, macStr);
  Serial.printf("[MESH] Discovered device: %s (RSSI: %d)\n", macStr, rssi);
}

MeshConfig ESPNowMesh::getMeshConfig() const {
  return _meshConfig;
}

void ESPNowMesh::setMeshConfig(const MeshConfig& config, bool broadcast) {
  _meshConfig = config;
  _meshConfig.configFlags = (_configSyncEnabled ? (_meshConfig.configFlags | 0x01) : (_meshConfig.configFlags & ~0x01));
  _configSignature = meshConfigSignature(_meshConfig);
  _configSynced = true;
  _hasConfigSource = true;
  copyMac(_configSourceMac, myMAC);

  _maxDevices = _meshConfig.maxDevices;
  _discoveryInterval = _meshConfig.discoveryInterval;
  _rssiThreshold = _meshConfig.rssiThreshold;
  _maxHops = _meshConfig.maxHops;
  _wifiEnableDuration = _meshConfig.wifiEnableDuration;

  if (broadcast && _configSyncEnabled) {
    broadcastMeshConfig(false);
  }

  refreshDiscoveryTimer();
}

void ESPNowMesh::requestMeshConfigSync() {
  broadcastMeshConfig(true);
}

void ESPNowMesh::broadcastMeshConfig(bool requestOnly) {
  if (!_configSyncEnabled) return;

  MeshMessage msg;
  memset(&msg, 0, sizeof(msg));
  copyMac(msg.sourceMAC, myMAC);
  memset(msg.destMAC, 0xFF, 6);
  msg.hopCount = 0;
  msg.messageID = millis();

  if (requestOnly) {
    msg.messageType = MSG_CONFIG_REQUEST;
    msg.payloadSize = 0;
  } else {
    packMeshConfig(msg, MSG_CONFIG_ADVERTISEMENT);
  }

  uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  esp_now_add_peer(&peerInfo);
  esp_now_send(broadcastMAC, (uint8_t*)&msg, sizeof(MeshMessage));
}

void ESPNowMesh::handleConfigAdvertisement(const uint8_t* senderMAC, const MeshMessage* msg) {
  if (!_configSyncEnabled) return;
  if (!msg || msg->payloadSize < sizeof(MeshConfig)) return;

  MeshConfig incoming;
  if (!unpackMeshConfig(msg, incoming)) return;

  applyMeshConfig(incoming, senderMAC, false);
}

void ESPNowMesh::handleConfigRequest(const uint8_t* senderMAC, const MeshMessage* msg) {
  (void)msg;
  if (!_configSyncEnabled) return;
  if (compareMac(senderMAC, myMAC)) return;

  MeshMessage response;
  memset(&response, 0, sizeof(response));
  response.messageType = MSG_CONFIG_SYNC;
  copyMac(response.sourceMAC, myMAC);
  copyMac(response.destMAC, senderMAC);
  response.hopCount = 0;
  response.messageID = millis();
  packMeshConfig(response, MSG_CONFIG_SYNC);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, senderMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  esp_now_add_peer(&peerInfo);
  esp_now_send(senderMAC, (uint8_t*)&response, sizeof(MeshMessage));
}

void ESPNowMesh::handleConfigSync(const uint8_t* senderMAC, const MeshMessage* msg) {
  handleConfigAdvertisement(senderMAC, msg);
}

void ESPNowMesh::refreshDiscoveryTimer() {
  if (!discoveryRunning || discoveryTimer == nullptr) return;

  xTimerStop(discoveryTimer, 0);
  xTimerDelete(discoveryTimer, 0);
  discoveryTimer = xTimerCreate(
    "MeshDiscovery",
    pdMS_TO_TICKS(_discoveryInterval),
    pdTRUE,
    (void*)this,
    [](TimerHandle_t xTimer) {
      ESPNowMesh* mesh = (ESPNowMesh*)pvTimerGetTimerID(xTimer);
      mesh->performDiscovery();
    }
  );
  xTimerStart(discoveryTimer, 0);
}

void ESPNowMesh::applyMeshConfig(const MeshConfig& config, const uint8_t* sourceMac, bool forceApply) {
  if (!_configSyncEnabled) return;

  uint32_t incomingSignature = meshConfigSignature(config);
  bool incomingIsNewer = config.configVersion > _meshConfig.configVersion;
  bool sameConfig = incomingSignature == _configSignature;

  if (sameConfig && !forceApply) {
    _configSynced = true;
    if (sourceMac) {
      copyMac(_configSourceMac, sourceMac);
      _hasConfigSource = true;
    }
    return;
  }

  bool shouldApply = forceApply;

  if (!shouldApply) {
    if (!_configSynced) {
      shouldApply = true;
    } else if (incomingIsNewer) {
      shouldApply = true;
    } else if (config.configVersion == _meshConfig.configVersion && sourceMac) {
      if (!_hasConfigSource || memcmp(sourceMac, _configSourceMac, 6) < 0) {
        shouldApply = true;
      }
    }
  }

  if (!shouldApply) return;

  _meshConfig = config;
  _meshConfig.configFlags = (_configSyncEnabled ? (_meshConfig.configFlags | 0x01) : (_meshConfig.configFlags & ~0x01));
  _configSignature = meshConfigSignature(_meshConfig);
  _configSynced = true;
  if (sourceMac) {
    copyMac(_configSourceMac, sourceMac);
    _hasConfigSource = true;
  }

  _maxDevices = _meshConfig.maxDevices;
  _discoveryInterval = _meshConfig.discoveryInterval;
  _rssiThreshold = _meshConfig.rssiThreshold;
  _maxHops = _meshConfig.maxHops;
  _wifiEnableDuration = _meshConfig.wifiEnableDuration;

  Serial.printf("[MESH] Synced config v%lu from %02X:%02X:%02X:%02X:%02X:%02X\n",
                (unsigned long)_meshConfig.configVersion,
                sourceMac ? sourceMac[0] : 0,
                sourceMac ? sourceMac[1] : 0,
                sourceMac ? sourceMac[2] : 0,
                sourceMac ? sourceMac[3] : 0,
                sourceMac ? sourceMac[4] : 0,
                sourceMac ? sourceMac[5] : 0);

  refreshDiscoveryTimer();
}

bool ESPNowMesh::unpackMeshConfig(const MeshMessage* msg, MeshConfig& config) {
  if (!msg || msg->payloadSize < sizeof(MeshConfig)) return false;
  memcpy(&config, msg->payload, sizeof(MeshConfig));
  return true;
}

void ESPNowMesh::packMeshConfig(MeshMessage& msg, MeshMessageType messageType) {
  msg.messageType = messageType;
  memset(msg.payload, 0, sizeof(msg.payload));
  memcpy(msg.payload, &_meshConfig, sizeof(MeshConfig));
  msg.payloadSize = sizeof(MeshConfig);
}

uint32_t ESPNowMesh::meshConfigSignature(const MeshConfig& config) const {
  MeshConfig normalized = config;
  normalized.configFlags &= ~0x01;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&normalized);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(MeshConfig); i++) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
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
    
    for (auto& neighbor : deviceMap) {
      if (!visited[neighbor.first] && neighbor.second.isActive) {
        int16_t alt = distances[current] - neighbor.second.rssi;
        if (alt < distances[neighbor.first]) {
          distances[neighbor.first] = alt;
          previous[neighbor.first] = deviceMap[current].macAddress; 
        }
      }
    }
  }

  // Reconstruct path to destination
  char destStr[18];
  macToString(destination, destStr);

  if (distances[destStr] != INT16_MAX) {
    route.signalStrength = distances[destStr];
    
    std::vector<uint8_t*> tempPath;
    std::string currStr = destStr;
    
    while (currStr != myMacStr && previous.find(currStr) != previous.end()) {
      tempPath.push_back(previous[currStr]);
      char prevStr[18];
      macToString(previous[currStr], prevStr);
      currStr = prevStr;
    }
    
    // Reverse the path so it goes from source to destination
    route.path.push_back((uint8_t*)myMAC);
    for (auto it = tempPath.rbegin(); it != tempPath.rend(); ++it) {
      route.path.push_back(*it);
    }
    route.path.push_back((uint8_t*)destination);
    route.hopCount = route.path.size() - 1;
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
  
  _wifiEnableDuration = durationMs;
  Serial.printf("[MESH] WiFi enabled for %d ms\n", durationMs);
}

void ESPNowMesh::printNetworkGraphML() {
  Serial.println("--- BEGIN GRAPHML ---");
  
  // Print standard GraphML header and schema definitions
  Serial.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  Serial.println("<graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\"");
  Serial.println("         xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"");
  Serial.println("         xsi:schemaLocation=\"http://graphml.graphdrawing.org/xmlns http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd\">");
  
  // Define attributes for node labels and edge weights (RSSI)
  Serial.println("  <key id=\"d0\" for=\"node\" attr.name=\"label\" attr.type=\"string\"/>");
  Serial.println("  <key id=\"d1\" for=\"edge\" attr.name=\"rssi\" attr.type=\"int\"/>");
  
  Serial.println("  <graph id=\"G\" edgedefault=\"directed\">");
  
  // 1. Print our local device node
  char myMacStr[18];
  macToString(myMAC, myMacStr);
  Serial.printf("    <node id=\"%s\">\n", myMacStr);
  Serial.printf("      <data key=\"d0\">Self (%s)</data>\n", myMacStr);
  Serial.println("    </node>");
  
  // 2. Print neighbor nodes and edges
  int edgeId = 0;
  for (auto const& [macStr, device] : deviceMap) {
    if (!device.isActive) continue;
    
    // Print peer node
    Serial.printf("    <node id=\"%s\">\n", macStr.c_str());
    Serial.printf("      <data key=\"d0\">%s</data>\n", macStr.c_str());
    Serial.println("    </node>");
    
    // Print directed edge from self to peer with RSSI value
    Serial.printf("    <edge id=\"e%d\" source=\"%s\" target=\"%s\">\n", edgeId++, myMacStr, macStr.c_str());
    Serial.printf("      <data key=\"d1\">%d</data>\n", device.rssi);
    Serial.println("    </edge>");
  }
  
  // Close structural elements
  Serial.println("  </graph>");
  Serial.println("</graphml>");
  
  Serial.println("--- END GRAPHML ---");
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
  
  // Enforce Max Devices limit for new devices
  if (deviceMap.find(macStr) == deviceMap.end() && deviceMap.size() >= _maxDevices) {
    Serial.println("[MESH] Maximum device limit reached. Cannot add new peer.");
    return;
  }
  
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
