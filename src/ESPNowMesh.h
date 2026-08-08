#ifndef ESP_NOW_MESH_H
#define ESP_NOW_MESH_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <map>
#include <queue>
#include <algorithm>

// Message types for ESP-NOW communication
enum MeshMessageType {
  MSG_DISCOVERY_PROBE = 1,
  MSG_DISCOVERY_RESPONSE = 2,
  MSG_PATH_QUERY = 3,
  MSG_PATH_RESPONSE = 4,
  MSG_DATA = 5,
  MSG_ACK = 6,
  MSG_CONFIG_ADVERTISEMENT = 7,
  MSG_CONFIG_REQUEST = 8,
  MSG_CONFIG_SYNC = 9
};

// Mesh-wide runtime configuration shared between peers
struct MeshConfig {
  uint32_t configVersion;
  uint8_t maxDevices;
  uint32_t discoveryInterval;
  int16_t rssiThreshold;
  uint8_t maxHops;
  uint32_t wifiEnableDuration;
  uint8_t configFlags;
};

// Structure for device information
struct MeshDevice {
  uint8_t macAddress[6];
  int16_t rssi;
  uint32_t lastSeen;
  uint8_t hopCount;
  bool isActive;
  bool hasWiFiCapability;
};

// Structure for a route
struct MeshRoute {
  std::vector<uint8_t*> path;  // Array of MAC addresses
  int16_t signalStrength;
  uint8_t hopCount;
  uint32_t timestamp;
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

// Main mesh manager class
class ESPNowMesh {
  public:
    ESPNowMesh();
    ~ESPNowMesh();
    void begin(const char* deviceName,
               uint8_t meshMaxDevices = 20,
               uint32_t meshDiscoveryInterval = 5000,
               int16_t meshRSSIThreshold = -85,
               uint8_t meshMaxHops = 10,
               uint32_t wifiEnableDuration = 10000,
               bool enableConfigSync = true);

    void enableWiFiForPath(const MeshRoute& route, uint32_t durationMs = 10000);
    // Start periodic discovery
    void startDiscovery();
    void stopDiscovery();

    // Mesh config synchronization
    MeshConfig getMeshConfig() const;
    void setMeshConfig(const MeshConfig& config, bool broadcast = true);
    void requestMeshConfigSync();

    // Get network topology
    std::vector<MeshDevice> getDevices();
    MeshDevice getDeviceInfo(const uint8_t* macAddress);

    // Pathfinding operations
    MeshRoute findOptimalPath(const uint8_t* destinationMAC);
    std::vector<MeshRoute> findAlternativePaths(const uint8_t* destinationMAC, uint8_t numPaths = 3);

    void disableWiFiForDevice();

    // Send data through mesh
    bool sendData(const uint8_t* destMAC, const uint8_t* data, uint16_t length);

    // Get current device MAC address
    void getMyMAC(uint8_t* macBuffer);

    // Callback registration
    void onMeshData(void (*callback)(const uint8_t* sourceMac, const uint8_t* data, uint16_t length));
    void onDeviceDiscovered(void (*callback)(const MeshDevice& device));
    void onPathFound(void (*callback)(const MeshRoute& route));

    // Network statistics
    uint8_t getDeviceCount();
    int16_t getAverageSignalStrength();
    void printNetworkTopology();
    void printNetworkGraphML();

  private:
    // Discovery management
    void performDiscovery();
    void broadcastDiscoveryProbe();
    void handleDiscoveryProbe(const uint8_t* senderMAC, const MeshMessage* msg);
    void handleDiscoveryResponse(const uint8_t* senderMAC, const MeshMessage* msg);
    void handleConfigAdvertisement(const uint8_t* senderMAC, const MeshMessage* msg);
    void handleConfigRequest(const uint8_t* senderMAC, const MeshMessage* msg);
    void handleConfigSync(const uint8_t* senderMAC, const MeshMessage* msg);
    void broadcastMeshConfig(bool requestOnly = false);
    void refreshDiscoveryTimer();
    void applyMeshConfig(const MeshConfig& config, const uint8_t* sourceMac, bool forceApply);
    bool unpackMeshConfig(const MeshMessage* msg, MeshConfig& config);
    void packMeshConfig(MeshMessage& msg, MeshMessageType messageType);
    uint32_t meshConfigSignature(const MeshConfig& config) const;

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
    uint8_t myMAC[6];
    bool discoveryRunning;
    uint32_t lastDiscoveryTime;
    TimerHandle_t discoveryTimer;
    TimerHandle_t wifiDisableTimer;

    // Callbacks
    void (*onMeshDataCallback)(const uint8_t*, const uint8_t*, uint16_t);
    void (*onDeviceDiscoveredCallback)(const MeshDevice&);
    void (*onPathFoundCallback)(const MeshRoute&);

    // Static instance for callbacks
    static ESPNowMesh* instance;
    String _deviceName;
    uint8_t _maxDevices;
    uint32_t _discoveryInterval;
    int16_t _rssiThreshold;
    uint8_t _maxHops;
    uint32_t _wifiEnableDuration;
    MeshConfig _meshConfig;
    bool _configSyncEnabled;
    bool _configSynced;
    uint32_t _configSignature;
    uint8_t _configSourceMac[6];
    bool _hasConfigSource;
};
#endif
