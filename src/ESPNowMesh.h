#ifndef ESP_NOW_MESH_H
#define ESP_NOW_MESH_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <map>
#include <queue>
#include <algorithm>
#include <cstring>

// Message types for ESP-NOW communication
enum MeshMessageType {
  MSG_DISCOVERY_PROBE = 1,
  MSG_DISCOVERY_RESPONSE = 2,
  MSG_PATH_QUERY = 3,
  MSG_PATH_RESPONSE = 4,
  MSG_DATA = 5,
  MSG_ACK = 6,
  MSG_AUTO_CONNECT = 7,
  MSG_CONNECT_ACK = 8,
  MSG_TIME_SYNC = 9,
  MSG_INBOX_QUERY = 10,
  MSG_INBOX_DELIVERY = 11,
  MSG_CONNECTION_MAP_QUERY = 12,
  MSG_CONNECTION_MAP_RESPONSE = 13
};

// Structure for device information
struct MeshDevice {
  uint8_t macAddress[6];
  int16_t rssi;
  uint32_t lastSeen;
  uint8_t hopCount;
  bool isActive;
  bool hasWiFiCapability;
  bool isConnected;        // New: connection state
  uint32_t connectedSince; // New: connection timestamp
};

// Structure for a route
struct MeshRoute {
  std::vector<uint8_t*> path;  // Array of MAC addresses
  int16_t signalStrength;
  uint8_t hopCount;
  uint32_t timestamp;
};

// Structure for time synchronization
struct TimeSync {
  uint8_t sourceMAC[6];
  uint32_t sourceTime;      // Time on source device
  uint32_t receivedTime;    // Time when received
  int32_t timeOffset;       // Calculated offset
  uint8_t syncQuality;      // Quality of sync (0-100)
};

// Structure for inbox message
struct InboxMessage {
  uint8_t senderMAC[6];
  uint32_t messageID;
  uint32_t timestamp;
  bool isDelivered;
  uint8_t data[200];
  uint16_t dataLength;
};

// Structure for connection map entry
struct ConnectionMapEntry {
  uint8_t deviceMAC[6];
  uint8_t connectedTo[6];   // MAC of connected device
  int16_t signalStrength;
  uint32_t lastUpdated;
};

// Mesh message structure for ESP-NOW
struct MeshMessage {
  uint8_t messageType;
  uint8_t sourceMAC[6];
  uint8_t destMAC[6];
  uint8_t hopCount;
  uint16_t payloadSize;
  uint8_t payload[200];
  uint32_t messageID;
};

// Connection state tracker
struct ConnectionState {
  uint8_t deviceMAC[6];
  bool isConnected;
  uint32_t connectedTime;
  uint8_t failureCount;
  uint32_t lastConnectionAttempt;
};

// Main mesh manager class
class ESPNowMesh {
  public:
    ESPNowMesh();
    ~ESPNowMesh();
    
    void begin(const char* deviceName,
              const uint8_t meshMaxDevices = 20,
              const uint32_t meshDiscoveryInterval = 5000,
              const int16_t meshRSSIThreshold = -85,
              const uint8_t meshMaxHops = 10,
              const uint32_t WiFiEnableDuration = 10000);

    void enableWiFiForPath(const MeshRoute& route, uint32_t durationMs = 10000);
    
    // Start periodic discovery
    void startDiscovery();
    void stopDiscovery();

    // Get network topology
    std::vector<MeshDevice> getDevices();
    MeshDevice getDeviceInfo(const uint8_t* macAddress);

    // Pathfinding operations
    MeshRoute findOptimalPath(const uint8_t* destinationMAC);
    std::vector<MeshRoute> findAlternativePaths(const uint8_t* destinationMAC, uint8_t numPaths = 3);

    // WiFi management
    void disableWiFiForDevice();

    // Send data through mesh
    bool sendData(const uint8_t* destMAC, const uint8_t* data, uint16_t length);

    // Get current device MAC address
    void getMyMAC(uint8_t* macBuffer);

    // Callback registration
    void onMeshData(void (*callback)(const uint8_t* sourceMac, const uint8_t* data, uint16_t length));
    void onDeviceDiscovered(void (*callback)(const MeshDevice& device));
    void onPathFound(void (*callback)(const MeshRoute& route));
    void onDeviceConnected(void (*callback)(const MeshDevice& device));
    void onDeviceDisconnected(void (*callback)(const uint8_t* macAddress));
    void onInboxMessageReceived(void (*callback)(const InboxMessage& message));
    void onTimeSyncReceived(void (*callback)(const TimeSync& sync));

    // Network statistics
    uint8_t getDeviceCount();
    int16_t getAverageSignalStrength();
    void printNetworkTopology();
    void printNetworkGraphML();

    // ======== AUTOMATIC CONNECTION FEATURES ========
    void enableAutoConnect(uint32_t connectionCheckInterval = 10000);
    void disableAutoConnect();
    bool isDeviceConnected(const uint8_t* macAddress);
    std::vector<MeshDevice> getConnectedDevices();
    uint8_t getConnectedDeviceCount();
    void forceReconnectToDevice(const uint8_t* macAddress);

    // ======== TIME SYNC FEATURES ========
    void enableTimeSync(uint32_t timeSyncInterval = 30000);
    void disableTimeSync();
    uint32_t getSyncedTime();  // Get time synchronized across mesh
    int32_t getTimeOffset();   // Get offset from last sync
    uint8_t getTimeSyncQuality();
    TimeSync getLastTimeSync();

    // ======== INBOX SYSTEM FEATURES ========
    void enableInboxSystem();
    void disableInboxSystem();
    bool sendInboxMessage(const uint8_t* destMAC, const uint8_t* data, uint16_t length);
    std::vector<InboxMessage> getInboxMessages();
    std::vector<InboxMessage> getInboxMessagesFrom(const uint8_t* senderMAC);
    bool markMessageAsDelivered(uint32_t messageID);
    void clearInbox();
    uint16_t getInboxSize();
    void queryRemoteInbox(const uint8_t* deviceMAC);

    // ======== CONNECTION MAP FEATURES ========
    void buildConnectionMap();
    std::map<std::string, std::vector<ConnectionMapEntry>> getConnectionMap();
    void queryConnectionMap(const uint8_t* deviceMAC);
    bool exportConnectionMapAsJSON(char* buffer, uint16_t bufferSize);
    void printConnectionMap();

    // Manual auto-connect configuration
    void setAutoConnectRetries(uint8_t maxRetries);
    void setAutoConnectTimeout(uint32_t timeoutMs);

  private:
    // Discovery management
    void performDiscovery();
    void broadcastDiscoveryProbe();
    void handleDiscoveryProbe(const uint8_t* senderMAC, const MeshMessage* msg);
    void handleDiscoveryResponse(const uint8_t* senderMAC, const MeshMessage* msg);

    // Auto-connect management
    void performAutoConnect();
    void sendAutoConnectMessage(const uint8_t* deviceMAC);
    void handleAutoConnect(const uint8_t* senderMAC, const MeshMessage* msg);
    void handleConnectACK(const uint8_t* senderMAC, const MeshMessage* msg);
    bool attemptConnection(const uint8_t* macAddress);
    void markDeviceConnected(const uint8_t* macAddress);
    void markDeviceDisconnected(const uint8_t* macAddress);

    // Time sync management
    void performTimeSync();
    void broadcastTimeSync();
    void handleTimeSync(const uint8_t* senderMAC, const MeshMessage* msg);
    int32_t calculateTimeOffset(uint32_t sourceTime, uint32_t receivedTime);

    // Inbox management
    void handleInboxQuery(const uint8_t* senderMAC, const MeshMessage* msg);
    void handleInboxDelivery(const uint8_t* senderMAC, const MeshMessage* msg);

    // Connection map management
    void handleConnectionMapQuery(const uint8_t* senderMAC, const MeshMessage* msg);
    void handleConnectionMapResponse(const uint8_t* senderMAC, const MeshMessage* msg);

    // Pathfinding (Dijkstra's algorithm)
    MeshRoute calculateShortestPath(const uint8_t* destination);
    int16_t calculatePathQuality(const MeshRoute& route);

    // WiFi control
    void enableWiFi();
    void disableWiFi();
    static void wifiEnableTimeout(void* param);

    // ESP-NOW callbacks
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    static void espNowOnReceive(const esp_now_recv_info_t* recv_info, const uint8_t* incomingData, int len);
    static void espNowOnSent(const uint8_t* mac, esp_now_send_status_t status);
    #else
    static void espNowOnReceive(const uint8_t* mac, const uint8_t* incomingData, int len);
    static void espNowOnSent(const uint8_t* mac, esp_now_send_status_t status);
    #endif

    // Device management
    void addOrUpdateDevice(const uint8_t* macAddress, int16_t rssi);
    void removeStaleDevices();
    bool isMacAddressValid(const uint8_t* mac);

    // Utility
    void macToString(const uint8_t* mac, char* buffer);
    void copyMac(uint8_t* dest, const uint8_t* src);
    bool compareMac(const uint8_t* mac1, const uint8_t* mac2);

    // Member variables
    std::map<std::string, MeshDevice> deviceMap;
    std::map<std::string, ConnectionState> connectionStateMap;
    std::vector<InboxMessage> inbox;
    std::map<std::string, std::vector<ConnectionMapEntry>> connectionMap;
    
    uint8_t myMAC[6];
    bool discoveryRunning;
    uint32_t lastDiscoveryTime;
    TimerHandle_t discoveryTimer;
    TimerHandle_t wifiDisableTimer;
    TimerHandle_t autoConnectTimer;
    TimerHandle_t timeSyncTimer;

    // Time sync tracking
    TimeSync lastTimeSync;
    bool timeSyncEnabled;
    uint32_t timeSyncInterval;
    uint32_t startTime;
    int32_t currentTimeOffset;
    uint8_t timeSyncQuality;

    // Auto-connect tracking
    bool autoConnectEnabled;
    uint32_t autoConnectInterval;
    uint8_t autoConnectMaxRetries;
    uint32_t autoConnectTimeout;
    uint32_t lastAutoConnectTime;

    // Inbox system
    bool inboxEnabled;
    uint16_t maxInboxSize;

    // Callbacks
    void (*onMeshDataCallback)(const uint8_t*, const uint8_t*, uint16_t);
    void (*onDeviceDiscoveredCallback)(const MeshDevice&);
    void (*onPathFoundCallback)(const MeshRoute&);
    void (*onDeviceConnectedCallback)(const MeshDevice&);
    void (*onDeviceDisconnectedCallback)(const uint8_t*);
    void (*onInboxMessageReceivedCallback)(const InboxMessage&);
    void (*onTimeSyncReceivedCallback)(const TimeSync&);

    // Static instance for callbacks
    static ESPNowMesh* instance;
    String _deviceName;
    uint8_t _maxDevices;
    uint32_t _discoveryInterval;
    int16_t _rssiThreshold;
    uint8_t _maxHops;
    uint32_t _wifiEnableDuration;
};

#endif