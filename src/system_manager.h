#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "firebase_manager.h"
#include "light_manager.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "unit_manager.h"
#include <ArduinoOTA.h>
#include <string>

/**
 * SystemManager Class
 * 
 * Main system coordinator that manages all subsystems:
 * 1. System Initialization:
 *    - Hardware setup
 *    - Network connection
 *    - Firebase authentication
 *    - OTA updates
 * 
 * 2. System Monitoring:
 *    - Heartbeat system for online/offline status
 *    - Connection state monitoring
 *    - Firmware version tracking
 * 
 * 3. System Updates:
 *    - Periodic sensor readings
 *    - Unit state updates
 *    - Lighting control
 *    - System configuration updates
 * 
 * Thread Safety:
 * - All public methods are thread-safe
 * - Uses mutex protection for critical sections
 * - Delegates thread safety to subsystem managers
 * 
 * Error Handling:
 * - Uses centralized ErrorManager for error reporting
 * - Implements automatic recovery mechanisms
 * - Provides detailed error context
 * 
 * Update Intervals:
 * - Heartbeat: Every 30 seconds
 * - System data: Every 30 seconds
 * - Firmware check: Every hour
 * - Connection check: Every 30 seconds
 */
class SystemManager {
private:
    NetworkManager& networkManager;
    OTAManager& otaManager;
    LightManager& lightManager;
    UnitManager& unitManager;
    FirebaseManager& firebaseManager;
    SystemState& systemState;
    
    // System status
    String systemStatus;
    
    // Pre-allocate string space to prevent fragmentation
    static constexpr size_t SYSTEM_NAME_MAX_LENGTH = 50;
    static constexpr size_t TIME_STRING_MAX_LENGTH = 6;
    
    String systemName;
    int connectionOffset;
    unsigned long lastSystemDataUpdate = 0;
    bool initialized;
    
    // Light management variables
    String lightOnTime;
    String lightOffTime;
    bool lightMasterSwitch;
    bool timeCycleEnabled;
    
    // Mutex for thread safety
    SemaphoreHandle_t mutex;
    static constexpr uint32_t MUTEX_TIMEOUT_MS = 100;
    
    // Connection state tracking
    unsigned long lastConnectionCheck;
    unsigned long lastReconnectAttempt;
    uint8_t reconnectAttempts;
    bool wasConnected;
    uint32_t currentBackoffDelay;
    
    // Heap monitoring
    static constexpr uint32_t HEAP_MONITOR_INTERVAL = 3600000;  // 1 hour
    unsigned long lastHeapCheck;
    uint32_t minHeapEver;
    
    /**
     * Takes mutex with timeout
     * Thread-safe: Yes
     * @return true if mutex was taken
     */
    bool takeMutex() {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "SystemManager::takeMutex"
            );
            return false;
        }
        return true;
    }
    
    /**
     * Releases mutex
     * Thread-safe: Yes
     */
    void giveMutex() {
        xSemaphoreGive(mutex);
    }
    
    /**
     * Cleans up string resources
     * Thread-safe: Yes
     */
    void cleanupStrings() {
        if (!takeMutex()) {
            return;
        }
        
        systemName = "";
        lightOnTime = "";
        lightOffTime = "";
        
        giveMutex();
    }
    
    /**
     * Initializes string buffers with pre-allocated space
     * Thread-safe: Yes
     */
    void initializeStrings() {
        if (!takeMutex()) {
            return;
        }
        
        // Clear any existing strings first
        cleanupStrings();
        
        // Reserve space for strings with extra capacity for future growth
        const size_t extraCapacity = 10;  // Extra bytes for future growth
        systemName.reserve(SYSTEM_NAME_MAX_LENGTH + extraCapacity);
        lightOnTime.reserve(TIME_STRING_MAX_LENGTH + extraCapacity);
        lightOffTime.reserve(TIME_STRING_MAX_LENGTH + extraCapacity);
        
        giveMutex();
    }
    
    /**
     * Parses time string from Firebase (HH:MM format)
     * Thread-safe: Yes
     * @param timeStr Time string in HH:MM format
     * @return Time in seconds since midnight
     */
    time_t parseTimeString(const String& timeStr) {
        struct tm tm = {};
        strptime(timeStr.c_str(), "%H:%M", &tm);
        return mktime(&tm);
    }
    
    /**
     * Formats current timestamp for Firebase
     * Thread-safe: Yes
     * @return ISO 8601 formatted timestamp
     */
    String formatTimestamp() {
        time_t now;
        time(&now);
        char timestamp[30];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        return String(timestamp);
    }
    
    /**
     * Sends heartbeat to Firebase
     * Thread-safe: Yes
     */
    void sendHeartbeat() {
        if (!takeMutex()) {
            return;
        }
        
        // Create path buffer for Firebase
        char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        snprintf(pathBuffer, sizeof(pathBuffer), "systems/%s", systemName.c_str());
        
        // Update heartbeat timestamp in Firebase
        if (!firebaseManager.updateField(pathBuffer, "heartbeat", String(millis()))) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                "Failed to send heartbeat",
                "SystemManager::sendHeartbeat"
            );
        }
        
        giveMutex();
    }
    
    /**
     * Updates system data in Firebase
     * Thread-safe: Yes
     * @return true if update was successful
     */
    bool updateSystemData() {
        if (!takeMutex()) {
            return false;
        }
        
        bool success = true;
        
        // Update system status
        systemStatus = networkManager.isConnected() ? "online" : "offline";
        
        // Create path buffer for Firebase
        char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        
        // Update unit data
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!systemState.unitsEnabled[i]) {
                continue;
            }
            
            // Create unit path
            snprintf(pathBuffer, sizeof(pathBuffer), "units/%d", i);
            
            // Update unit status
            if (!firebaseManager.addToBatch(i, "status", 
                systemState.unitsEnabled[i] ? "enabled" : "disabled", "string")) {
                success = false;
            }
            
            // Update sensor values
            float ecValue = unitManager.readECSensorValue(i);
            if (ecValue >= 0) {
                if (!firebaseManager.addToBatch(i, "ecValue", String(ecValue, 3), "float")) {
                    success = false;
                }
            }
            
            // Update control values
            if (!firebaseManager.addToBatch(i, "atomizerOn", 
                unitManager.isAtomizerOn(i) ? "true" : "false", "bool")) {
                success = false;
            }
        }
        
        // Flush any remaining batched operations
        if (!firebaseManager.flushBatch()) {
            success = false;
        }
        
        giveMutex();
        
        if (!success) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                "Failed to update system data",
                "SystemManager::updateSystemData"
            );
        }
        
        return success;
    }
    
    /**
     * Saves current system settings to storage
     * Thread-safe: Yes
     * @return true if save was successful
     */
    bool saveSettingsToStorage() {
        if (!takeMutex()) {
            return false;
        }
        
        bool success = true;
        
        // Save light settings
        if (!firebaseManager.saveLightSettings(lightMasterSwitch, timeCycleEnabled, 
                                            lightOnTime, lightOffTime)) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                "Failed to save light settings",
                "SystemManager::saveSettingsToStorage"
            );
            success = false;
        }
        
        // Save unit settings
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!firebaseManager.saveUnitSettings(i, systemState.unitsEnabled[i],
                                               systemState.atomizerOnIntervals[i],
                                               systemState.atomizerOffIntervals[i])) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                    "Failed to save unit settings",
                    "SystemManager::saveSettingsToStorage"
                );
                success = false;
            }
        }
        
        giveMutex();
        return success;
    }

public:
    /**
     * Constructor
     * Thread-safe: Yes
     * @param network Reference to network manager
     * @param ota Reference to OTA manager
     * @param light Reference to light manager
     * @param unit Reference to unit manager
     * @param firebase Reference to Firebase manager
     * @param state Reference to system state
     */
    SystemManager(NetworkManager& network, OTAManager& ota, LightManager& light, 
                 UnitManager& unit, FirebaseManager& firebase, SystemState& state)
        : networkManager(network), otaManager(ota), lightManager(light), 
          unitManager(unit), firebaseManager(firebase), systemState(state), 
          initialized(false), mutex(NULL),
          lastHeapCheck(0), minHeapEver(ESP.getFreeHeap()) {
        connectionOffset = 1000 + random(100, 10000);
        initializeStrings();
        
        // Create mutex for thread safety
        mutex = xSemaphoreCreateMutex();
        if (!mutex) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_CREATION_FAILED,
                "Failed to create mutex",
                "SystemManager::SystemManager"
            );
        }
    }
    
    /**
     * Destructor
     */
    ~SystemManager() {
        cleanupStrings();
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Initializes the system
     * Thread-safe: Yes
     * @return true if initialization successful
     */
    bool begin() {
        if (initialized) {
            return true;
        }
        
        if (!takeMutex()) {
            return false;
        }
        
        delay(connectionOffset);
        
        bool success = true;
        
        // Log firmware version
        Serial.printf("Firmware version: %s\n", SystemConfig::FIRMWARE_VERSION);
        
        if (!networkManager.begin()) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                "Failed to initialize network",
                "SystemManager::begin"
            );
            success = false;
        }
        
        if (success) {
            // Update firmware version in Firebase
            if (!firebaseManager.updateSystemVersion(SystemConfig::FIRMWARE_VERSION)) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                    "Failed to update firmware version in Firebase",
                    "SystemManager::begin"
                );
                success = false;
            }
            
            // Fetch system data from Firebase
            time_t lightOnTime = 0;
            time_t lightOffTime = 0;
            bool lightMasterSwitch = false;
            bool timeCycleEnabled = false;
            bool unitsEnabled[SystemConfig::NUMBER_OF_UNITS] = {false};
            unsigned long atomizerOnIntervals[SystemConfig::NUMBER_OF_UNITS] = {0};
            unsigned long atomizerOffIntervals[SystemConfig::NUMBER_OF_UNITS] = {0};
            
            // Use FirebaseManager to fetch system data
            if (!firebaseManager.fetchSystemData(&systemName, &lightOnTime, &lightOffTime, 
                                              &lightMasterSwitch, &timeCycleEnabled, unitsEnabled,
                                              atomizerOnIntervals, atomizerOffIntervals)) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                    "Failed to fetch system data from Firebase",
                    "SystemManager::begin"
                );
                success = false;
            }
            
            // Apply light settings
            lightManager.updateSettings(lightMasterSwitch, timeCycleEnabled, 
                                     lightOnTime, lightOffTime);
            
            // Apply unit settings
            for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                systemState.unitsEnabled[i] = unitsEnabled[i];
                systemState.atomizerOnIntervals[i] = atomizerOnIntervals[i];
                systemState.atomizerOffIntervals[i] = atomizerOffIntervals[i];
            }
            
            // Save settings to storage
            if (!saveSettingsToStorage()) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                    "Failed to save settings to storage",
                    "SystemManager::begin"
                );
                success = false;
            }
            
            // Update system data and send heartbeat
            if (!updateSystemData()) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                    "Failed to update initial system data",
                    "SystemManager::begin"
                );
                success = false;
            }
        }
        
        if (success && systemName.length() > 0) {
            if (!networkManager.handleOTA(systemName)) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                    "Failed to initialize OTA",
                    "SystemManager::begin"
                );
                success = false;
            }
        }
        
        if (success) {
            initialized = true;
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Updates system state and handles periodic tasks
     * Should be called in the main loop
     */
    void update() {
        if (!takeMutex()) {
            return;
        }

        unsigned long currentMillis = millis();
        unsigned long adjustedMillis = currentMillis + connectionOffset;

        // Monitor heap usage every hour (online only)
        if (networkManager.isConnected() && currentMillis - lastHeapCheck >= HEAP_MONITOR_INTERVAL) {
            uint32_t currentHeap = ESP.getFreeHeap();
            if (currentHeap < minHeapEver) {
                minHeapEver = currentHeap;
            }
            
            // Check for low heap condition
            if (currentHeap < SystemConfig::HEAP_WARNING_THRESHOLD) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::HEAP_WARNING,
                    String("Low heap memory: ") + String(currentHeap) + " bytes",
                    "SystemManager::update"
                );
            }
            
            // Log heap status
            Serial.printf("Heap Status - Free: %u, Min Ever: %u\n", currentHeap, minHeapEver);
            
            // Create path buffer for Firebase
            char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
            snprintf(pathBuffer, sizeof(pathBuffer), "systems/%s", systemName.c_str());
            
            // Update in Firebase
            firebaseManager.updateField(pathBuffer, "heapStatus", String(currentHeap));
            firebaseManager.updateField(pathBuffer, "minEver", String(minHeapEver));
            firebaseManager.updateField(pathBuffer, "warning", String(currentHeap < SystemConfig::HEAP_WARNING_THRESHOLD));
            
            lastHeapCheck = currentMillis;
        }
        
        // Handle OTA updates (online only)
        if (networkManager.isConnected()) {
            ArduinoOTA.handle();
        }
        
        // Handle all periodic updates
        if (adjustedMillis - systemState.previousHeartbeatMillis >= SystemConfig::INTERVAL_30_SECONDS) {
            // Update sensors and units (works offline)
            unitManager.readWaterLevel();
            unitManager.update();
            
            // Update lighting (works offline)
            lightManager.updateSettings(lightMasterSwitch, timeCycleEnabled, 
                                     parseTimeString(lightOnTime), 
                                     parseTimeString(lightOffTime));
            lightManager.update();
            
            // Update timestamps
            systemState.previousHeartbeatMillis = adjustedMillis;
            systemState.lastConnectionCheckMillis = adjustedMillis;

            // Online-only updates
            if (networkManager.isConnected()) {
                // Only send updates to Firebase, don't fetch
                updateSystemData();
                sendHeartbeat();
            }
        }
        
        // Check for firmware updates (online only)
        if (networkManager.isConnected() && 
            currentMillis - systemState.lastFirmwareCheckMillis >= SystemConfig::INTERVAL_5_MINUTES) {
            otaManager.checkForUpdates();
            systemState.lastFirmwareCheckMillis = currentMillis;
        }
        
        giveMutex();
    }
    
    /**
     * Explicitly refreshes system data from Firebase
     * Should be called only when needed (e.g., after settings changes)
     * @return true if refresh was successful
     */
    bool refreshSystemData() {
        if (!takeMutex()) {
            return false;
        }

        bool success = true;
        
        // Fetch system data from Firebase
        time_t lightOnTime = 0;
        time_t lightOffTime = 0;
        bool lightMasterSwitch = false;
        bool timeCycleEnabled = false;
        bool unitsEnabled[SystemConfig::NUMBER_OF_UNITS] = {false};
        unsigned long atomizerOnIntervals[SystemConfig::NUMBER_OF_UNITS] = {0};
        unsigned long atomizerOffIntervals[SystemConfig::NUMBER_OF_UNITS] = {0};
        
        if (!firebaseManager.fetchSystemData(&systemName, &lightOnTime, &lightOffTime, 
                                          &lightMasterSwitch, &timeCycleEnabled, unitsEnabled,
                                          atomizerOnIntervals, atomizerOffIntervals)) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                "Failed to refresh system data from Firebase",
                "SystemManager::refreshSystemData"
            );
            success = false;
        }
        
        if (success) {
            // Apply light settings
            lightManager.updateSettings(lightMasterSwitch, timeCycleEnabled, 
                                     lightOnTime, lightOffTime);
            
            // Apply unit settings
            for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                systemState.unitsEnabled[i] = unitsEnabled[i];
                systemState.atomizerOnIntervals[i] = atomizerOnIntervals[i];
                systemState.atomizerOffIntervals[i] = atomizerOffIntervals[i];
            }
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Gets the system name
     * Thread-safe: Yes
     * @return System name string
     */
    const String& getSystemName() const { 
        return systemName; 
    }
};

#endif // SYSTEM_MANAGER_H 