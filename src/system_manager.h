#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include "config.h"
#include "light_manager.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "unit_manager.h"
#include <Firebase_ESP_Client.h>
#include <ArduinoOTA.h>

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
    FirebaseData& fbdo;
    SystemState& systemState;
    
    // Pre-allocate string space to prevent fragmentation
    static constexpr size_t SYSTEM_NAME_MAX_LENGTH = 50;
    static constexpr size_t TIME_STRING_MAX_LENGTH = 6;
    static constexpr size_t UNIT_NAME_MAX_LENGTH = 50;
    
    // Firebase JSON objects (reused to prevent memory fragmentation)
    FirebaseJson systemJson;
    FirebaseJson unitJson;
    FirebaseJsonData jsonData;
    
    String systemName;
    String unitNames[SystemConfig::NUMBER_OF_UNITS];
    int connectionOffset;
    unsigned long lastSystemDataUpdate = 0;
    bool initialized;
    
    // Light management variables
    String lightOnTime;
    String lightOffTime;
    bool lightMasterSwitch;
    bool timeCycleEnabled;
    
    /**
     * Cleans up string resources
     */
    void cleanupStrings() {
        systemName = "";
        lightOnTime = "";
        lightOffTime = "";
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            unitNames[i] = "";
        }
    }
    
    /**
     * Cleans up Firebase JSON objects
     */
    void cleanupJson() {
        systemJson.clear();
        unitJson.clear();
        jsonData.clear();
    }
    
    /**
     * Initializes string buffers with pre-allocated space
     */
    void initializeStrings() {
        // Clear any existing strings first
        cleanupStrings();
        
        // Reserve space for strings
        systemName.reserve(SYSTEM_NAME_MAX_LENGTH);
        lightOnTime.reserve(TIME_STRING_MAX_LENGTH);
        lightOffTime.reserve(TIME_STRING_MAX_LENGTH);
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            unitNames[i].reserve(UNIT_NAME_MAX_LENGTH);
        }
    }
    
    /**
     * Parses time string from Firebase (HH:MM format)
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
     * Sends heartbeat to Firebase to indicate system is online
     */
    void sendHeartbeat() {
        char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        snprintf(pathBuffer, sizeof(pathBuffer), SystemConfig::UNIT_PATH_FORMAT, SystemConfig::SERIAL_NUMBER);
        
        FirebaseJson systemJson;
        systemJson.set(SystemConfig::SYSTEM_LAST_SEEN_PATH, formatTimestamp());
        
        if (Firebase.Firestore.patchDocument(&fbdo, SystemConfig::FIREBASE_PROJECT_ID, "", pathBuffer, systemJson.raw(), "lastSeen")) {
            Serial.printf("Heartbeat sent successfully\n");
        } else {
            Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "sendHeartbeat", fbdo.errorReason().c_str());
        }
    }
    
    /**
     * Updates system configuration from Firebase
     * Reads system name, lighting settings, and unit names
     */
    void updateSystemData() {
        char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        snprintf(pathBuffer, sizeof(pathBuffer), SystemConfig::UNIT_PATH_FORMAT, SystemConfig::SERIAL_NUMBER);
        
        if (Firebase.Firestore.getDocument(&fbdo, SystemConfig::FIREBASE_PROJECT_ID, "", pathBuffer)) {
            // Process system data
            if (fbdo.payload().length() > 0) {
                systemJson.clear();
                systemJson.setJsonData(fbdo.payload());
                
                // Update system configuration
                if (systemJson.get(jsonData, "fields/systemName/stringValue")) {
                    systemName = jsonData.stringValue;
                }
                // Add other system data processing as needed
            }
        } else {
            Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "updateSystemData", fbdo.errorReason().c_str());
        }
    }
    
    /**
     * Helper method to safely create Firebase paths
     * @param buffer Buffer to store the path
     * @param format Format string for snprintf
     * @param ... Additional arguments for snprintf
     * @return true if path was created successfully
     */
    bool createFirebasePath(char* buffer, size_t bufferSize, const char* format, ...) {
        va_list args;
        va_start(args, format);
        int written = vsnprintf(buffer, bufferSize, format, args);
        va_end(args);
        
        if (written >= bufferSize) {
            Serial.println("Error: Path buffer overflow");
            return false;
        }
        return true;
    }
    
    /**
     * Updates a unit's EC value in Firebase
     * @param unitIndex Index of the unit
     * @param ecValue EC value to update
     */
    void updateUnitECValue(int unitIndex, float ecValue) {
        if (unitIndex < 0 || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return;
        }
        
        char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        snprintf(pathBuffer, sizeof(pathBuffer), SystemConfig::UNIT_PATH_FORMAT, SystemConfig::SERIAL_NUMBER, unitIndex);
        
        FirebaseJson unitJson;
        unitJson.set(SystemConfig::UNIT_EC_VALUE_PATH, ecValue);
        unitJson.set(SystemConfig::UNIT_EC_LAST_UPDATED_PATH, formatTimestamp());
        
        if (Firebase.Firestore.patchDocument(&fbdo, SystemConfig::FIREBASE_PROJECT_ID, "", pathBuffer, unitJson.raw(), "ecValue,ecLastUpdated")) {
            Serial.printf("Updated EC value for unit %d\n", unitIndex);
        } else {
            Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "updateUnitECValue", fbdo.errorReason().c_str());
        }
    }

public:
    /**
     * Constructor
     * @param network Reference to network manager
     * @param ota Reference to OTA manager
     * @param light Reference to light manager
     * @param unit Reference to unit manager
     * @param fbdo Reference to Firebase data object
     * @param state Reference to system state
     */
    SystemManager(NetworkManager& network, OTAManager& ota, LightManager& light, 
                 UnitManager& unit, FirebaseData& fbdo, SystemState& state)
        : networkManager(network), otaManager(ota), lightManager(light), 
          unitManager(unit), fbdo(fbdo), systemState(state), initialized(false) {
        connectionOffset = 1000 + random(100, 10000);
        initializeStrings();
    }
    
    /**
     * Destructor
     * Cleans up system resources
     */
    ~SystemManager() {
        cleanupStrings();
        cleanupJson();
    }
    
    /**
     * Initializes the system
     * Sets up network, Firebase, and hardware components
     * @return true if initialization successful
     */
    bool begin() {
        if (initialized) {
            return true;
        }
        
        delay(connectionOffset);
        
        if (!networkManager.begin()) {
            return false;
        }
        
        // First update system data to get the system name
        updateSystemData();
        
        // Initialize OTA if we have a system name
        if (systemName.length() > 0) {
            if (!networkManager.handleOTA(systemName)) {
                Serial.println("Failed to initialize OTA");
                return false;
            }
        }
        
        // Update system version in Firebase
        otaManager.updateSystemVersion();
        
        initialized = true;
        return true;
    }
    
    /**
     * Main system update loop
     * Handles all periodic tasks and system monitoring
     */
    void update() {
        if (!networkManager.isConnected()) {
            return;  // Skip updates if not connected
        }
        
        unsigned long currentMillis = millis();
        unsigned long adjustedMillis = currentMillis + connectionOffset;
        
        // Handle OTA updates
        ArduinoOTA.handle();
        
        // Handle all periodic updates
        if (adjustedMillis - systemState.previousHeartbeatMillis >= SystemConfig::INTERVAL_30_SECONDS) {
            // Update system data and send heartbeat
            updateSystemData();
            sendHeartbeat();
            
            // Update sensors and units
            unitManager.readWaterLevel();
            // Read EC from all units
            for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                // Only read EC for units that are enabled and have their atomizer off
                if (unitNames[i] != "" && systemState.unitsEnabled[i] && !unitManager.isAtomizerOn(i)) {
                    float ecValue = unitManager.readECSensorValue(i);
                    if (ecValue >= 0 && ecValue <= 1.0f) {  // Validate EC value is in valid range
                        updateUnitECValue(i, ecValue);
                    } else {
                        Serial.println("Invalid EC value for unit " + String(i) + ": " + String(ecValue, 3));
                    }
                }
            }
            unitManager.update();
            
            // Update lighting
            lightManager.updateSettings(lightMasterSwitch, timeCycleEnabled, 
                                     parseTimeString(lightOnTime), 
                                     parseTimeString(lightOffTime));
            lightManager.update();
            
            // Update timestamps and handle OTA
            systemState.previousHeartbeatMillis = adjustedMillis;
            systemState.lastConnectionCheckMillis = adjustedMillis;
            otaManager.handle();
        }
        
        // Check for firmware updates (using unadjusted time since it's independent of connection offset)
        if (currentMillis - systemState.lastFirmwareCheckMillis >= SystemConfig::FIRMWARE_CHECK_INTERVAL) {
            Serial.println("Checking for firmware updates...");
            otaManager.checkForUpdates();
            systemState.lastFirmwareCheckMillis = currentMillis;
        }
    }
    
    /**
     * Gets the system name
     * @return System name string
     */
    const String& getSystemName() const { return systemName; }
};

#endif // SYSTEM_MANAGER_H 