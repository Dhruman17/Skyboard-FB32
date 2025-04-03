#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "firebase_manager.h"
#include "light_manager.h"
#include "mutex_manager.h"
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
class SystemManager : public MutexManager {
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
     * Cleans up string resources
     * Thread-safe: Yes
     */
    void cleanupStrings() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return;
        }
        
        systemName = "";
        lightOnTime = "";
        lightOffTime = "";
    }
    
    /**
     * Initializes string buffers with pre-allocated space
     * Thread-safe: Yes
     */
    void initializeStrings() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return;
        }
        
        // Clear any existing strings first
        cleanupStrings();
        
        // Reserve space for strings with extra capacity for future growth
        const size_t extraCapacity = 10;  // Extra bytes for future growth
        systemName.reserve(SYSTEM_NAME_MAX_LENGTH + extraCapacity);
        lightOnTime.reserve(TIME_STRING_MAX_LENGTH + extraCapacity);
        lightOffTime.reserve(TIME_STRING_MAX_LENGTH + extraCapacity);
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
        // Create path buffer for Firebase outside of mutex
        char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        
        // Get system name with minimal mutex time
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return;
            }
            snprintf(pathBuffer, sizeof(pathBuffer), "systems/%s", systemName.c_str());
        }
        
        // Update heartbeat timestamp in Firebase outside of mutex
        if (!firebaseManager.updateDocument(pathBuffer, String(millis()), "number")) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                "Failed to send heartbeat",
                "SystemManager::sendHeartbeat"
            );
        }
        
        // Allow other tasks to run
        yield();
    }
    
    /**
     * Updates system data in Firebase
     * Thread-safe: Yes
     * @return true if update was successful
     */
    bool updateSystemData() {
        bool success = true;
        
        // Get system status with minimal mutex time
        bool isConnected = false;
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return false;
            }
            isConnected = networkManager.isConnected();
        }
        
        // Update system status outside of mutex
        String status = isConnected ? "online" : "offline";
        
        // Process one unit at a time to avoid long mutex holds
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            // Check if unit is enabled with minimal mutex time
            bool unitEnabled = false;
            {
                ScopedLock lock(*this);
                if (!lock.isLocked()) {
                    return false;
                }
                unitEnabled = systemState.unitsEnabled[i];
            }
            
            if (!unitEnabled) {
                continue;
            }
            
            // Create unit path outside of mutex
            char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
            snprintf(pathBuffer, sizeof(pathBuffer), "units/%d", i);
            
            // Update unit status outside of mutex
            if (!firebaseManager.addToBatch(i, "status", "enabled", "string")) {
                success = false;
            }
            
            // Get sensor values outside of mutex
            float ecValue = unitManager.readECSensorValue(i);
            if (ecValue >= 0) {
                if (!firebaseManager.addToBatch(i, "ecValue", String(ecValue, 3), "float")) {
                    success = false;
                }
            }
            
            // Get atomizer state outside of mutex
            bool atomizerOn = unitManager.isAtomizerOn(i);
            if (!firebaseManager.addToBatch(i, "atomizerOn", atomizerOn ? "true" : "false", "bool")) {
                success = false;
            }
            
            // Allow other tasks to run between units
            yield();
        }
        
        // Flush any remaining batched operations outside of mutex
        if (!firebaseManager.flushBatch()) {
            success = false;
        }
        
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
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
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
        
        return success;
    }

    void checkHeap() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return;
        }
        
        uint32_t currentHeap = ESP.getFreeHeap();
        if (currentHeap < SystemConfig::HEAP_WARNING_THRESHOLD) {
            char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
            snprintf(pathBuffer, sizeof(pathBuffer), SystemConfig::UNIT_PATH_FORMAT, 
                SystemConfig::SERIAL_NUMBER, 0);
            
            firebaseManager.updateDocument(pathBuffer, String(currentHeap < SystemConfig::HEAP_WARNING_THRESHOLD), "boolean");
        }
    }

public:
    /**
     * Constructor
     * @param networkManager Reference to NetworkManager
     * @param otaManager Reference to OTAManager
     * @param lightManager Reference to LightManager
     * @param unitManager Reference to UnitManager
     * @param firebaseManager Reference to FirebaseManager
     * @param systemState Reference to SystemState
     */
    SystemManager(NetworkManager& networkManager, OTAManager& otaManager, 
                 LightManager& lightManager, UnitManager& unitManager,
                 FirebaseManager& firebaseManager, SystemState& systemState)
        : networkManager(networkManager), otaManager(otaManager),
          lightManager(lightManager), unitManager(unitManager),
          firebaseManager(firebaseManager), systemState(systemState),
          systemStatus("initializing"), systemName(""), connectionOffset(0),
          initialized(false), lightMasterSwitch(false), timeCycleEnabled(false),
          lastConnectionCheck(0), lastReconnectAttempt(0), reconnectAttempts(0),
          wasConnected(false), currentBackoffDelay(1000), lastHeapCheck(0),
          minHeapEver(UINT32_MAX) {
        
        Serial.println("[SystemManager] Starting constructor");
        
        // Initialize string buffers without taking mutex
        Serial.println("[SystemManager] Initializing string buffers");
        systemName.reserve(SYSTEM_NAME_MAX_LENGTH + 10);
        lightOnTime.reserve(TIME_STRING_MAX_LENGTH + 10);
        lightOffTime.reserve(TIME_STRING_MAX_LENGTH + 10);
        Serial.println("[SystemManager] Constructor completed successfully");
    }
    
    /**
     * Destructor
     * Cleans up resources
     */
    ~SystemManager() {
        Serial.println("[SystemManager] Starting destructor");
        Serial.println("[SystemManager] Cleaning up strings");
        cleanupStrings();
        Serial.println("[SystemManager] Destructor completed");
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
        
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
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
        
        return success;
    }
    
    /**
     * Updates system state and handles periodic tasks
     * Should be called in the main loop
     */
    void update() {
        unsigned long currentMillis = millis();
        unsigned long adjustedMillis = currentMillis + connectionOffset;

        // Only take mutex when we need to update something
        bool needsUpdate = false;
        
        // Check if we need to update heap status
        if (networkManager.isConnected() && currentMillis - lastHeapCheck >= HEAP_MONITOR_INTERVAL) {
            needsUpdate = true;
        }
        
        // Check if we need to do periodic updates
        if (adjustedMillis - systemState.previousHeartbeatMillis >= SystemConfig::INTERVAL_30_SECONDS) {
            needsUpdate = true;
        }
        
        // Check if we need to check for firmware updates
        if (networkManager.isConnected() && 
            currentMillis - systemState.lastFirmwareCheckMillis >= SystemConfig::INTERVAL_5_MINUTES) {
            needsUpdate = true;
        }
        
        // If no updates needed, return early
        if (!needsUpdate) {
            delay(100);  // Small delay to prevent tight loop
            return;
        }
        
        // Use ScopedLock for automatic mutex management
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            delay(100);  // Small delay if we can't take mutex
            return;
        }

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
            firebaseManager.updateDocument(pathBuffer, String(currentHeap), "number");
            firebaseManager.updateDocument(pathBuffer, String(minHeapEver), "number");
            firebaseManager.updateDocument(pathBuffer, String(currentHeap < SystemConfig::HEAP_WARNING_THRESHOLD), "boolean");
            
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
        
        delay(100);  // Small delay after updates to prevent tight loop
    }
    
    /**
     * Explicitly refreshes system data from Firebase
     * Should be called only when needed (e.g., after settings changes)
     * @return true if refresh was successful
     */
    bool refreshSystemData() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
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

    /**
     * Resets WiFi Manager settings if enabled in config
     * Thread-safe: Yes
     * @return true if reset was successful
     */
    bool resetWiFiManager() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }

        return networkManager.resetWiFiManager();
    }
};

#endif // SYSTEM_MANAGER_H 