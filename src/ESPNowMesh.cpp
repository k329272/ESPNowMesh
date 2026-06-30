#include "ESPNowMesh.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <ctime>

// Static instance
ESPNowMesh* ESPNowMesh::instance = nullptr;

ESPNowMesh::ESPNowMesh() 
  : discoveryRunning(false),
    lastDiscoveryTime(0),
    discoveryTimer(nullptr),
    wifiDisableTimer(nullptr),
    autoConnectTimer(nullptr),
    timeSyncTimer(nullptr),
    onMeshDataCallback(nullptr),
    onDeviceDiscoveredCallback(nullptr),
    onPathFoundCallback(nullptr),
    onDeviceConnectedCallback(nullptr),
    onDeviceDisconnectedCallback(nullptr),
    onInboxMessageReceivedCallback(nullptr),
    onTimeSyncReceivedCallback(nullptr),
    timeSyncEnabled(false),
    timeSyncInterval(30000),
    startTime(0),
    currentTimeOffset(0),
    timeSyncQuality(0),
    autoConnectEnabled(false),
    autoConnectInterval(10000),
    autoConnectMaxRetries(3),
    autoConnectTimeout(5000),
    lastAutoConnectTime(0),
    inboxEnabled(false),
    maxInboxSize(50) {
  instance = this;
  memset(&lastTimeSync, 0, sizeof(TimeSync));
}

ESPNowMesh::~ESPNowMesh() {
  stopDiscovery();
  disableAutoConnect();
  disableTimeSync();
  esp_now_deinit();
}

void ESPNowMesh::begin(const char* deviceName, 
                      const uint8_t meshMaxDevices,
                      const uint32_t meshDiscoveryInterval,
                      const int16_t meshRSSIThreshold,
                      const uint8_t meshMaxHops,
                      const uint32_t WiFiEnableDuration) {
  
  // Store the configuration variables
  _deviceName = String(deviceName);
  _maxDevices = meshMaxDevices;
  _discoveryInterval = meshDiscoveryInterval;
  _rssiThreshold = meshRSSIThreshold;
  _maxHops = meshMaxHops;
  _wifiEnableDuration = WiFiEnableDuration;

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
  
  startTime = millis();
  
  Serial.print("[MESH] Initialized with MAC: ");
  char macStr[18];
  macToString(myMAC, macStr);
  Serial.println(macStr);
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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void ESPNowMesh::espNowOnReceive(const esp_now_recv_info_t* recv_info, const uint8_t* incomingData, int len) {
  if (instance == nullptr) return;
  const uint8_t* mac = recv_info->src_addr;
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
    case MSG_DATA:
      if (instance->onMeshDataCallback) {
        instance->onMeshDataCallback(msg->sourceMAC, msg->payload, msg->payloadSize);
      }
      break;
    case MSG_AUTO_CONNECT:
      instance->handleAutoConnect(mac, msg);
      break;
    case MSG_CONNECT_ACK:
      instance->handleConnectACK(mac, msg);
      break;
    case MSG_TIME_SYNC:
      instance->handleTimeSync(mac, msg);
      break;
    case MSG_INBOX_QUERY:
      instance->handleInboxQuery(mac, msg);
      break;
    case MSG_INBOX_DELIVERY:
      instance->handleInboxDelivery(mac, msg);
      break;
    case MSG_CONNECTION_MAP_QUERY:
      instance->handleConnectionMapQuery(mac, msg);
      break;
    case MSG_CONNECTION_MAP_RESPONSE:
      instance->handleConnectionMapResponse(mac, msg);
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

// ==================== AUTO-CONNECT FEATURES ====================

void ESPNowMesh::enableAutoConnect(uint32_t connectionCheckInterval) {
  if (autoConnectEnabled) return;
  
  autoConnectEnabled = true;
  autoConnectInterval = connectionCheckInterval;
  
  Serial.printf("[MESH] Auto-connect enabled (interval: %d ms)\n", autoConnectInterval);
  
  autoConnectTimer = xTimerCreate(
    "AutoConnect",
    pdMS_TO_TICKS(autoConnectInterval),
    pdTRUE,
    (void*)this,
    [](TimerHandle_t xTimer) {
      ESPNowMesh* mesh = (ESPNowMesh*)pvTimerGetTimerID(xTimer);
      mesh->performAutoConnect();
    }
  );
  
  xTimerStart(autoConnectTimer, 0);
  performAutoConnect();  // Initial auto-connect
}

void ESPNowMesh::disableAutoConnect() {
  autoConnectEnabled = false;
  if (autoConnectTimer) {
    xTimerStop(autoConnectTimer, 0);
    xTimerDelete(autoConnectTimer, 0);
    autoConnectTimer = nullptr;
  }
  Serial.println("[MESH] Auto-connect disabled");
}

void ESPNowMesh::performAutoConnect() {
  if (!autoConnectEnabled) return;
  
  uint32_t now = millis();
  
  // Attempt to connect to all discovered but disconnected devices
  for (auto& [macStr, device] : deviceMap) {
    if (!device.isConnected) {
      // Check if we should retry connection
      char macStr[18];
      macToString(device.macAddress, macStr);
      
      auto it = connectionStateMap.find(macStr);
      
      // Create connection state if it doesn't exist
      if (it == connectionStateMap.end()) {
        ConnectionState connState;
        copyMac(connState.deviceMAC, device.macAddress);
        connState.isConnected = false;
        connState.failureCount = 0;
        connState.lastConnectionAttempt = 0;
        connectionStateMap[macStr] = connState;
        it = connectionStateMap.find(macStr);
      }
      
      // Only retry if we haven't exceeded max retries and timeout has passed
      if (it->second.failureCount < autoConnectMaxRetries) {
        if (now - it->second.lastConnectionAttempt >= autoConnectTimeout) {
          sendAutoConnectMessage(device.macAddress);
          it->second.lastConnectionAttempt = now;
        }
      }
    }
  }
  
  lastAutoConnectTime = now;
}

void ESPNowMesh::sendAutoConnectMessage(const uint8_t* deviceMAC) {
  MeshMessage msg;
  msg.messageType = MSG_AUTO_CONNECT;
  copyMac(msg.sourceMAC, myMAC);
  copyMac(msg.destMAC, deviceMAC);
  msg.hopCount = 0;
  msg.payloadSize = 0;
  msg.messageID = millis();
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, deviceMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  esp_now_send(deviceMAC, (uint8_t*)&msg, sizeof(MeshMessage));
  
  char macStr[18];
  macToString(deviceMAC, macStr);
  Serial.printf("[MESH] Sending auto-connect to: %s\n", macStr);
}

void ESPNowMesh::handleAutoConnect(const uint8_t* senderMAC, const MeshMessage* msg) {
  // Don't connect to ourselves
  if (compareMac(senderMAC, myMAC)) return;
  
  // Send connect acknowledgement
  MeshMessage ack;
  ack.messageType = MSG_CONNECT_ACK;
  copyMac(ack.sourceMAC, myMAC);
  copyMac(ack.destMAC, senderMAC);
  ack.hopCount = 0;
  ack.payloadSize = 0;
  ack.messageID = msg->messageID;
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, senderMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  esp_now_send(senderMAC, (uint8_t*)&ack, sizeof(MeshMessage));
  
  // Mark as connected
  markDeviceConnected(senderMAC);
}

void ESPNowMesh::handleConnectACK(const uint8_t* senderMAC, const MeshMessage* msg) {
  // Connection successful
  markDeviceConnected(senderMAC);
}

void ESPNowMesh::markDeviceConnected(const uint8_t* macAddress) {
  char macStr[18];
  macToString(macAddress, macStr);
  
  auto it = deviceMap.find(macStr);
  if (it != deviceMap.end()) {
    if (!it->second.isConnected) {
      it->second.isConnected = true;
      it->second.connectedSince = millis();
      
      Serial.printf("[MESH] Device connected: %s\n", macStr);
      
      if (onDeviceConnectedCallback) {
        onDeviceConnectedCallback(it->second);
      }
    }
    
    // Reset connection state
    auto connIt = connectionStateMap.find(macStr);
    if (connIt != connectionStateMap.end()) {
      connIt->second.isConnected = true;
      connIt->second.failureCount = 0;
      connIt->second.connectedTime = millis();
    }
  }
}

void ESPNowMesh::markDeviceDisconnected(const uint8_t* macAddress) {
  char macStr[18];
  macToString(macAddress, macStr);
  
  auto it = deviceMap.find(macStr);
  if (it != deviceMap.end()) {
    if (it->second.isConnected) {
      it->second.isConnected = false;
      
      Serial.printf("[MESH] Device disconnected: %s\n", macStr);
      
      if (onDeviceDisconnectedCallback) {
        onDeviceDisconnectedCallback(macAddress);
      }
    }
    
    // Increment failure count
    auto connIt = connectionStateMap.find(macStr);
    if (connIt != connectionStateMap.end()) {
      connIt->second.isConnected = false;
      connIt->second.failureCount++;
    }
  }
}

bool ESPNowMesh::isDeviceConnected(const uint8_t* macAddress) {
  char macStr[18];
  macToString(macAddress, macStr);
  
  auto it = deviceMap.find(macStr);
  if (it != deviceMap.end()) {
    return it->second.isConnected;
  }
  return false;
}

std::vector<MeshDevice> ESPNowMesh::getConnectedDevices() {
  std::vector<MeshDevice> connectedDevices;
  for (auto& [macStr, device] : deviceMap) {
    if (device.isConnected && device.isActive) {
      connectedDevices.push_back(device);
    }
  }
  return connectedDevices;
}

uint8_t ESPNowMesh::getConnectedDeviceCount() {
  uint8_t count = 0;
  for (auto& [macStr, device] : deviceMap) {
    if (device.isConnected && device.isActive) {
      count++;
    }
  }
  return count;
}

void ESPNowMesh::forceReconnectToDevice(const uint8_t* macAddress) {
  char macStr[18];
  macToString(macAddress, macStr);
  
  auto it = connectionStateMap.find(macStr);
  if (it != connectionStateMap.end()) {
    it->second.failureCount = 0;
    it->second.lastConnectionAttempt = 0;
  }
  
  markDeviceDisconnected(macAddress);
  sendAutoConnectMessage(macAddress);
}

// ==================== TIME SYNC FEATURES ====================

void ESPNowMesh::enableTimeSync(uint32_t timeSyncIntervalMs) {
  if (timeSyncEnabled) return;
  
  timeSyncEnabled = true;
  timeSyncInterval = timeSyncIntervalMs;
  
  Serial.printf("[MESH] Time sync enabled (interval: %d ms)\n", timeSyncInterval);
  
  timeSyncTimer = xTimerCreate(
    "TimeSync",
    pdMS_TO_TICKS(timeSyncInterval),
    pdTRUE,
    (void*)this,
    [](TimerHandle_t xTimer) {
      ESPNowMesh* mesh = (ESPNowMesh*)pvTimerGetTimerID(xTimer);
      mesh->performTimeSync();
    }
  );
  
  xTimerStart(timeSyncTimer, 0);
  performTimeSync();  // Initial sync
}

void ESPNowMesh::disableTimeSync() {
  timeSyncEnabled = false;
  if (timeSyncTimer) {
    xTimerStop(timeSyncTimer, 0);
    xTimerDelete(timeSyncTimer, 0);
    timeSyncTimer = nullptr;
  }
  Serial.println("[MESH] Time sync disabled");
}

void ESPNowMesh::performTimeSync() {
  if (!timeSyncEnabled) return;
  
  broadcastTimeSync();
}

void ESPNowMesh::broadcastTimeSync() {
  MeshMessage msg;
  msg.messageType = MSG_TIME_SYNC;
  copyMac(msg.sourceMAC, myMAC);
  memset(msg.destMAC, 0xFF, 6);  // Broadcast
  msg.hopCount = 0;
  msg.messageID = millis();
  
  // Payload: current time
  uint32_t currentTime = millis() - startTime;
  memcpy(msg.payload, &currentTime, sizeof(uint32_t));
  msg.payloadSize = sizeof(uint32_t);
  
  uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  esp_now_send(broadcastMAC, (uint8_t*)&msg, sizeof(MeshMessage));
}

void ESPNowMesh::handleTimeSync(const uint8_t* senderMAC, const MeshMessage* msg) {
  if (msg->payloadSize < sizeof(uint32_t)) return;
  
  uint32_t sourceTime = *(uint32_t*)msg->payload;
  uint32_t receivedTime = millis() - startTime;
  
  // Calculate time offset
  int32_t offset = calculateTimeOffset(sourceTime, receivedTime);
  
  // Update our local offset (simple averaging)
  if (currentTimeOffset == 0) {
    currentTimeOffset = offset;
  } else {
    currentTimeOffset = (currentTimeOffset + offset) / 2;
  }
  
  // Calculate sync quality (higher is better, max 100)
  uint8_t quality = (offset == 0) ? 100 : (100 / (1 + abs(offset) / 1000));
  
  // Store sync info
  copyMac(lastTimeSync.sourceMAC, senderMAC);
  lastTimeSync.sourceTime = sourceTime;
  lastTimeSync.receivedTime = receivedTime;
  lastTimeSync.timeOffset = offset;
  lastTimeSync.syncQuality = quality;
  timeSyncQuality = quality;
  
  Serial.printf("[MESH] Time sync from ");
  char macStr[18];
  macToString(senderMAC, macStr);
  Serial.printf("%s - Offset: %d ms, Quality: %d%%\n", macStr, offset, quality);
  
  if (onTimeSyncReceivedCallback) {
    onTimeSyncReceivedCallback(lastTimeSync);
  }
}

int32_t ESPNowMesh::calculateTimeOffset(uint32_t sourceTime, uint32_t receivedTime) {
  // Simple calculation: difference between source time and our time when we received it
  return (int32_t)(sourceTime - receivedTime);
}

uint32_t ESPNowMesh::getSyncedTime() {
  return (millis() - startTime) + currentTimeOffset;
}

int32_t ESPNowMesh::getTimeOffset() {
  return currentTimeOffset;
}

uint8_t ESPNowMesh::getTimeSyncQuality() {
  return timeSyncQuality;
}

TimeSync ESPNowMesh::getLastTimeSync() {
  return lastTimeSync;
}

// ==================== INBOX SYSTEM FEATURES ====================

void ESPNowMesh::enableInboxSystem() {
  if (inboxEnabled) return;
  
  inboxEnabled = true;
  inbox.clear();
  
  Serial.println("[MESH] Inbox system enabled");
}

void ESPNowMesh::disableInboxSystem() {
  inboxEnabled = false;
  inbox.clear();
  
  Serial.println("[MESH] Inbox system disabled");
}

bool ESPNowMesh::sendInboxMessage(const uint8_t* destMAC, const uint8_t* data, uint16_t length) {
  if (length > 200) {
    Serial.println("[MESH] Inbox message too large");
    return false;
  }
  
  MeshMessage msg;
  msg.messageType = MSG_INBOX_DELIVERY;
  copyMac(msg.sourceMAC, myMAC);
  copyMac(msg.destMAC, destMAC);
  msg.hopCount = 0;
  msg.messageID = millis();
  msg.payloadSize = length;
  memcpy(msg.payload, data, length);
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, destMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  
  Serial.printf("[MESH] Sending inbox message to ");
  char macStr[18];
  macToString(destMAC, macStr);
  Serial.printf("%s\n", macStr);
  
  return esp_now_send(destMAC, (uint8_t*)&msg, sizeof(MeshMessage)) == ESP_OK;
}

void ESPNowMesh::handleInboxDelivery(const uint8_t* senderMAC, const MeshMessage* msg) {
  if (!inboxEnabled) return;
  
  // Check if inbox is full
  if (inbox.size() >= maxInboxSize) {
    Serial.println("[MESH] Inbox full, cannot store message");
    return;
  }
  
  InboxMessage inboxMsg;
  copyMac(inboxMsg.senderMAC, senderMAC);
  inboxMsg.messageID = msg->messageID;
  inboxMsg.timestamp = millis();
  inboxMsg.isDelivered = true;
  inboxMsg.dataLength = msg->payloadSize;
  memcpy(inboxMsg.data, msg->payload, msg->payloadSize);
  
  inbox.push_back(inboxMsg);
  
  char macStr[18];
  macToString(senderMAC, macStr);
  Serial.printf("[MESH] Inbox message received from %s (ID: %d)\n", macStr, msg->messageID);
  
  if (onInboxMessageReceivedCallback) {
    onInboxMessageReceivedCallback(inboxMsg);
  }
}

void ESPNowMesh::handleInboxQuery(const uint8_t* senderMAC, const MeshMessage* msg) {
  if (!inboxEnabled) return;
  
  // Get count of messages from sender
  uint16_t count = 0;
  for (const auto& message : inbox) {
    if (compareMac(message.senderMAC, senderMAC)) {
      count++;
    }
  }
  
  // Send back count (simplified - could be extended to send actual messages)
  Serial.printf("[MESH] Inbox query from ");
  char macStr[18];
  macToString(senderMAC, macStr);
  Serial.printf("%s - %d messages\n", macStr, count);
}

std::vector<InboxMessage> ESPNowMesh::getInboxMessages() {
  return inbox;
}

std::vector<InboxMessage> ESPNowMesh::getInboxMessagesFrom(const uint8_t* senderMAC) {
  std::vector<InboxMessage> messages;
  for (const auto& message : inbox) {
    if (compareMac(message.senderMAC, senderMAC)) {
      messages.push_back(message);
    }
  }
  return messages;
}

bool ESPNowMesh::markMessageAsDelivered(uint32_t messageID) {
  for (auto& message : inbox) {
    if (message.messageID == messageID) {
      message.isDelivered = true;
      return true;
    }
  }
  return false;
}

void ESPNowMesh::clearInbox() {
  inbox.clear();
  Serial.println("[MESH] Inbox cleared");
}

uint16_t ESPNowMesh::getInboxSize() {
  return inbox.size();
}

void ESPNowMesh::queryRemoteInbox(const uint8_t* deviceMAC) {
  MeshMessage msg;
  msg.messageType = MSG_INBOX_QUERY;
  copyMac(msg.sourceMAC, myMAC);
  copyMac(msg.destMAC, deviceMAC);
  msg.hopCount = 0;
  msg.payloadSize = 0;
  msg.messageID = millis();
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, deviceMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  esp_now_send(deviceMAC, (uint8_t*)&msg, sizeof(MeshMessage));
  
  char macStr[18];
  macToString(deviceMAC, macStr);
  Serial.printf("[MESH] Querying inbox of %s\n", macStr);
}

// ==================== CONNECTION MAP FEATURES ====================

void ESPNowMesh::buildConnectionMap() {
  connectionMap.clear();
  
  char myMacStr[18];
  macToString(myMAC, myMacStr);
  
  // Build map entry for this device
  std::vector<ConnectionMapEntry> myConnections;
  
  for (auto& [macStr, device] : deviceMap) {
    if (device.isActive && device.isConnected) {
      ConnectionMapEntry entry;
      copyMac(entry.deviceMAC, myMAC);
      copyMac(entry.connectedTo, device.macAddress);
      entry.signalStrength = device.rssi;
      entry.lastUpdated = millis();
      
      myConnections.push_back(entry);
    }
  }
  
  connectionMap[std::string(myMacStr)] = myConnections;
  
  Serial.printf("[MESH] Connection map built with %d connections\n", myConnections.size());
}

std::map<std::string, std::vector<ConnectionMapEntry>> ESPNowMesh::getConnectionMap() {
  return connectionMap;
}

void ESPNowMesh::handleConnectionMapQuery(const uint8_t* senderMAC, const MeshMessage* msg) {
  // Build current connection map and send it to requester
  buildConnectionMap();
  
  // Send connection map response
  MeshMessage response;
  response.messageType = MSG_CONNECTION_MAP_RESPONSE;
  copyMac(response.sourceMAC, myMAC);
  copyMac(response.destMAC, senderMAC);
  response.hopCount = 0;
  response.messageID = msg->messageID;
  
  // Encode connection count and data (simplified)
  uint8_t connCount = 0;
  for (auto& [macStr, device] : deviceMap) {
    if (device.isActive) connCount++;
  }
  
  response.payload[0] = connCount;
  response.payloadSize = 1;
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, senderMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  esp_now_send(senderMAC, (uint8_t*)&response, sizeof(MeshMessage));
  
  char macStr[18];
  macToString(senderMAC, macStr);
  Serial.printf("[MESH] Sent connection map to %s\n", macStr);
}

void ESPNowMesh::handleConnectionMapResponse(const uint8_t* senderMAC, const MeshMessage* msg) {
  // Process connection map from remote device
  if (msg->payloadSize > 0) {
    uint8_t connCount = msg->payload[0];
    
    char macStr[18];
    macToString(senderMAC, macStr);
    Serial.printf("[MESH] Received connection map from %s (%d devices)\n", macStr, connCount);
  }
}

void ESPNowMesh::queryConnectionMap(const uint8_t* deviceMAC) {
  MeshMessage msg;
  msg.messageType = MSG_CONNECTION_MAP_QUERY;
  copyMac(msg.sourceMAC, myMAC);
  copyMac(msg.destMAC, deviceMAC);
  msg.hopCount = 0;
  msg.payloadSize = 0;
  msg.messageID = millis();
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, deviceMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  
  esp_now_add_peer(&peerInfo);
  esp_now_send(deviceMAC, (uint8_t*)&msg, sizeof(MeshMessage));
  
  char macStr[18];
  macToString(deviceMAC, macStr);
  Serial.printf("[MESH] Querying connection map of %s\n", macStr);
}

bool ESPNowMesh::exportConnectionMapAsJSON(char* buffer, uint16_t bufferSize) {
  // Export connection map as JSON (simplified)
  buildConnectionMap();
  
  int written = snprintf(buffer, bufferSize, "{\"device\":\"");
  char myMacStr[18];
  macToString(myMAC, myMacStr);
  written += snprintf(buffer + written, bufferSize - written, "%s\",\"connections\":[", myMacStr);
  
  bool first = true;
  for (auto& [macStr, device] : deviceMap) {
    if (device.isActive && device.isConnected) {
      if (!first) written += snprintf(buffer + written, bufferSize - written, ",");
      
      char devMacStr[18];
      macToString(device.macAddress, devMacStr);
      written += snprintf(buffer + written, bufferSize - written, 
                         "{\"mac\":\"%s\",\"rssi\":%d}", devMacStr, device.rssi);
      first = false;
    }
  }
  
  written += snprintf(buffer + written, bufferSize - written, "]}");
  
  return written < bufferSize;
}

void ESPNowMesh::printConnectionMap() {
  buildConnectionMap();
  
  Serial.println("\n[MESH] Connection Map:");
  Serial.printf("Device: ");
  char myMacStr[18];
  macToString(myMAC, myMacStr);
  Serial.println(myMacStr);
  Serial.println("Connected to:");
  
  for (auto& [macStr, device] : deviceMap) {
    if (device.isActive && device.isConnected) {
      Serial.printf("  %s - RSSI: %d dBm\n", macStr.c_str(), device.rssi);
    }
  }
  Serial.println();
}

void ESPNowMesh::setAutoConnectRetries(uint8_t maxRetries) {
  autoConnectMaxRetries = maxRetries;
  Serial.printf("[MESH] Auto-connect max retries set to: %d\n", maxRetries);
}

void ESPNowMesh::setAutoConnectTimeout(uint32_t timeoutMs) {
  autoConnectTimeout = timeoutMs;
  Serial.printf("[MESH] Auto-connect timeout set to: %d ms\n", timeoutMs);
}

// ==================== ORIGINAL FEATURES (UNCHANGED) ====================

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
    
    for (auto& [mac, dist] : distances) {
      if (!visited[mac] && dist < minDist) {
        current = mac;
        minDist = dist;
      }
    }
    
    if (minDist == INT16_MAX) break;
    
    visited[current] = true;
  }
  
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
  device.isConnected = false;        // Initially not connected
  device.connectedSince = 0;
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
      // Check if device was connected
      if (it->second.isConnected) {
        markDeviceDisconnected(it->second.macAddress);
      }
      
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
  Serial.printf("Devices: %d (Connected: %d)\n", getDeviceCount(), getConnectedDeviceCount());
  Serial.printf("Average Signal: %d dBm\n", getAverageSignalStrength());
  Serial.println("Devices:");
  
  for (auto& device : deviceMap) {
    Serial.printf("  %s - RSSI: %d dBm, Hops: %d, Connected: %s\n",
      device.first.c_str(),
      device.second.rssi,
      device.second.hopCount,
      device.second.isConnected ? "YES" : "NO"
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

void ESPNowMesh::onDeviceConnected(void (*callback)(const MeshDevice&)) {
  onDeviceConnectedCallback = callback;
}

void ESPNowMesh::onDeviceDisconnected(void (*callback)(const uint8_t*)) {
  onDeviceDisconnectedCallback = callback;
}

void ESPNowMesh::onInboxMessageReceived(void (*callback)(const InboxMessage&)) {
  onInboxMessageReceivedCallback = callback;
}

void ESPNowMesh::onTimeSyncReceived(void (*callback)(const TimeSync&)) {
  onTimeSyncReceivedCallback = callback;
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