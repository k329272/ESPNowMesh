#ifndef ESP_NOW_MESH_H
#define ESP_NOW_MESH_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <map>
#include <queue>
#include <algorithm>

// Configuration constants
#define MESH_MAX_DEVICES 20
#define MESH_DISCOVERY_INTERVAL 5000  // 5 seconds
#define MESH_RSSI_THRESHOLD -85       // Minimum signal strength to consider
#define MESH_MAX_HOPS 10
#define WIFI_ENABLE_DURATION 10000    // 10 seconds

// Message types for ESP-NOW communication
enum MeshMessageType {
  MSG_DISCOVERY_PROBE = 1,
  MSG_DISCOVERY_RESPONSE = 2,
  MSG_PATH_QUERY = 3,
  MSG_PATH_RESPONSE = 4,
  MSG_DATA = 5,
  MSG_ACK = 6
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

    // Initialize the mesh network
    void begin(const char* deviceName);

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
    void enableWiFiForPath(const MeshRoute& route, uint32_t durationMs = WIFI_ENABLE_DURATION);
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

  private:
    // Discovery management
    void performDiscovery();
    void broadcastDiscoveryProbe();
    void handleDiscoveryProbe(const uint8_t* senderMAC, const MeshMessage* msg);
    void handleDiscoveryResponse(const uint8_t* senderMAC, const MeshMessage* msg);

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
};

#endif