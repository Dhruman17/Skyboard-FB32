#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <MCP3X21.h>
#include <Wire.h>
#include <time.h>

/**
 * Firebase Data Structure
 * 
 * Root path: "systems/{systemId}"
 * 
 * System-level fields:
 * - lastSeen: timestampValue - Last system heartbeat timestamp
 * - systeName: stringValue - Name of the system (TODO: Update backend to use systemName)
 * - firmwareVersion: stringValue - Current firmware version
 * 
 * Lighting control fields:
 * - lightOnTime: stringValue - Format "HH:MM" (24-hour)
 * - lightOffTime: stringValue - Format "HH:MM" (24-hour)
 * - lightMasterSwitch: booleanValue - Manual light control when timeCycleEnabled is false
 * - timeCycleEnabled: booleanValue - Enable/disable automatic lighting schedule
 * 
 * Units subcollection: "systems/{systemId}/units/{unitId}"
 * Each unit has fields:
 * - unitName: stringValue - Name of the unit
 * - unitState: booleanValue - Enable/disable unit irrigation
 * - Interval_On: integerValue - Irrigation on time in seconds
 * - Interval_Off: integerValue - Irrigation off time in seconds
 * - waterLevel: doubleValue - Current water level reading
 * - waterLevelState: booleanValue - Water level sensor state
 * - ecValue: doubleValue - Current EC reading
 */

/**
 * System Configuration
 * Contains all hardware and software configuration parameters
 */
namespace SystemConfig {
    // System Configuration
    static constexpr int NUMBER_OF_UNITS = 3;  // Number of vertical farming units
    static constexpr const char* FIRMWARE_VERSION = "1.5";  // Current firmware version
    
    // System Identification
    // This serial number is used for:
    // 1. Firebase document paths
    // 2. OTA update identification
    // 3. System identification in logs
    static constexpr const char* SERIAL_NUMBER = "TEST123456789";  // System serial number
    
    // Firebase Configuration
    static constexpr const char* FIREBASE_PROJECT_ID = "skyboard-fb32";  // Firebase project ID
    static constexpr const char* systemPath = "systems";  // Base path for system data
    static constexpr const char* UNIT_PATH_FORMAT = "systems/%s/units/%d";
    static constexpr const char* SYSTEM_PATH_FORMAT = "systems/%s";
    static constexpr const char* SYSTEM_LAST_SEEN_PATH = "lastSeen";  // Path for last seen timestamp
    static constexpr uint32_t FIREBASE_TIMEOUT = 10000;  // 10 seconds
    static constexpr uint32_t FIREBASE_CHECK_INTERVAL = 300000;  // 5 minutes
    static constexpr uint32_t FIREBASE_RETRY_DELAY = 5000;  // 5 seconds
    static constexpr uint8_t FIREBASE_MAX_RETRIES = 3;
    static constexpr uint32_t FIREBASE_RETRY_DELAY_MS = 5000;  // 5 seconds
    static constexpr uint8_t MAX_FIREBASE_RETRIES = 3;  // Maximum number of Firebase operation retries
    
    // Sensor Configuration
    static constexpr const char* SENSOR_TYPE_WATER = "water";  // Water level sensor type
    static constexpr const char* SENSOR_TYPE_EC = "ec";  // EC sensor type
    static constexpr float WATER_LEVEL_MIN = 0.0f;  // Minimum water level value
    static constexpr float WATER_LEVEL_MAX = 1023.0f;  // Maximum water level value
    static constexpr float EC_MAX = 1.0f;  // Maximum EC value
    
    // I2C Configuration
    static constexpr int I2C_SDA = 21;  // I2C SDA pin
    static constexpr int I2C_SCL = 22;  // I2C SCL pin
    
    // Atomizer Pin Configuration
    static constexpr int ATOMIZER_PIN_1 = 25;  // Atomizer 1 PWM pin
    static constexpr int ATOMIZER_PIN_2 = 26;  // Atomizer 2 PWM pin
    static constexpr int ATOMIZER_PIN_3 = 27;  // Atomizer 3 PWM pin
    static constexpr int ATOMIZER_PIN_4 = 14;  // Atomizer 4 PWM pin
    
    // Unit Configuration
    static constexpr const char* UNIT_NAME_PATH = "fields/unitName/stringValue";
    static constexpr const char* UNIT_ENABLED_PATH = "fields/unitState/booleanValue";
    static constexpr const char* UNIT_ON_INTERVAL_PATH = "fields/Interval_On/integerValue";
    static constexpr const char* UNIT_OFF_INTERVAL_PATH = "fields/Interval_Off/integerValue";
    
    // Network Configuration
    static constexpr unsigned long WIFI_RECONNECT_INTERVAL = 30000;  // 30 seconds
    static constexpr unsigned long HEARTBEAT_INTERVAL = 30000;  // 30 seconds
    static constexpr unsigned long CONNECTION_CHECK_INTERVAL = 60000;  // 1 minute
    
    // Buffer Sizes
    static constexpr size_t FIREBASE_PATH_BUFFER_SIZE = 512;  // Increased from 256 to handle deeply nested Firestore paths
    static constexpr size_t JSON_BUFFER_SIZE = 1024;  // Standard size for JSON buffers
    static constexpr size_t ERROR_MESSAGE_BUFFER_SIZE = 128;  // Standard size for error messages
    
    // Error Handling Configuration
    static constexpr uint8_t MAX_I2C_RETRIES = 3;  // Maximum number of I2C operation retries
    static constexpr uint8_t MAX_SENSOR_RETRIES = 3;  // Maximum number of sensor reading retries
    static constexpr uint8_t MAX_CONSECUTIVE_ERRORS = 5;  // Maximum number of consecutive errors before system reset
    static constexpr uint8_t MAX_PIN_ERRORS = 3;  // Maximum number of pin operation errors
    static constexpr uint8_t MAX_UPDATE_ERRORS = 3;  // Maximum number of update operation errors
    
    // Timing Configuration
    static constexpr uint32_t I2C_RETRY_DELAY_MS = 100;  // Delay between I2C retries
    static constexpr uint32_t SENSOR_RETRY_DELAY_MS = 100;  // Delay between sensor retries
    static constexpr uint32_t ERROR_RESET_DELAY_MS = 1000;  // Delay before system reset after max errors
    static constexpr uint32_t MUTEX_TIMEOUT_MS = 100;  // Standard mutex timeout
    static constexpr uint32_t SENSOR_READING_DELAY_MS = 100;  // Delay between sensor readings
    static constexpr uint8_t NUM_SENSOR_SAMPLES = 5;  // Number of samples to average for sensor readings
    
    // Hardware Configuration
    static constexpr uint8_t TCAADDR = 0x70;  // Main multiplexer address
    static constexpr uint8_t PCA_ADDRS[] = {0x40, 0x41, 0x42};  // Unit multiplexer addresses
    static constexpr uint8_t FDC1004_ADDR = 0x50;  // FDC1004 sensor address
    static constexpr uint8_t MCP3021_ADDR = 0x48;  // MCP3021 sensor address
    static constexpr uint8_t FDC1004_CHANNEL = 2;  // FDC1004 channel for water level
    static constexpr uint8_t MCP3021_CHANNEL = 3;  // MCP3021 channel for EC
    
    // PWM Configuration
    static constexpr uint32_t PWM_FREQUENCY_ATOMIZER = 25000;  // 25kHz for atomizer
    static constexpr uint8_t PWM_RESOLUTION_ATOMIZER = 8;  // 8-bit resolution
    static constexpr uint8_t PWM_ATOMIZER_OFF = 0;  // PWM value for atomizer off
    static constexpr uint8_t PWM_ATOMIZER_ON = 255;  // PWM value for atomizer on
    
    // GPIO Configuration
    static constexpr int WATER_LEVEL_PINS[] = {32, 33, 34};  // Water level input pins
    static constexpr int SYSTEM_12V_POWER_PIN = 12;  // System power control pin
    static constexpr int SYSTEM_LIGHTS_PIN = 13;  // System lights control pin
    
    // Error Message Formats
    static constexpr const char* ERROR_FORMAT_FIREBASE = "Firebase error in %s: %s\n";
    static constexpr const char* ERROR_FORMAT_I2C = "I2C error in %s for unit %d: %d\n";
    static constexpr const char* ERROR_FORMAT_SENSOR = "Sensor error in %s for unit %d: %s\n";
    static constexpr const char* ERROR_FORMAT_MUTEX = "Failed to take mutex in %s\n";
    static constexpr const char* ERROR_FORMAT_GENERIC = "Error in %s: %s\n";
    
    // Time Intervals
    static constexpr unsigned long INTERVAL_30_SECONDS = 30000;  // 30 seconds in milliseconds
    static constexpr unsigned long INTERVAL_1_MINUTE = 60000;  // 1 minute in milliseconds
    static constexpr unsigned long INTERVAL_5_MINUTES = 300000;  // 5 minutes in milliseconds
    
    // Heap Monitoring
    static constexpr uint32_t HEAP_WARNING_THRESHOLD = 15000;  // Warning threshold in bytes
    static constexpr uint32_t HEAP_MONITOR_INTERVAL = 3600000;  // 1 hour
    
    // WiFi Manager Configuration
    static constexpr uint32_t CONFIG_PORTAL_TIMEOUT = 180;  // 3 minutes timeout for config portal
    static constexpr int MIN_SIGNAL_QUALITY = 30;  // Minimum WiFi signal quality in dBm
}

namespace DefaultValues {
    // Light Control
    constexpr bool LIGHT_MASTER_SWITCH = false;
    constexpr unsigned long FALLBACK_LIGHT_ON_DURATION = 21600000;  // 6 hours in milliseconds
    constexpr unsigned long FALLBACK_LIGHT_OFF_DURATION = 64800000;  // 18 hours in milliseconds
    
    // Unit Settings
    constexpr bool UNITS_ENABLED[SystemConfig::NUMBER_OF_UNITS] = {true, true, true};
    constexpr unsigned long ATOMIZER_ON_INTERVAL = 300;  // 5 minutes
    constexpr unsigned long ATOMIZER_OFF_INTERVAL = 1800;  // 30 minutes
}

/**
 * System State Structure
 * Tracks the current state of all system components
 * 
 * Unit States:
 * - unitsEnabled: Array of boolean flags for each unit's irrigation state
 * - waterLevelStates: Array of boolean flags for each unit's water level sensor state
 * - previousWaterLevelStates: Array to track water level state changes
 * - atomizerOnIntervals: Array of on-time durations for each unit's irrigation
 * - atomizerOffIntervals: Array of off-time durations for each unit's irrigation
 * 
 * System Timing:
 * - previousHeartbeatMillis: Timestamp of last system heartbeat
 * - lastConnectionCheckMillis: Timestamp of last connection check
 * - lastFirmwareCheckMillis: Timestamp of last firmware update check
 */
struct SystemState {
    // Unit States
    bool unitsEnabled[SystemConfig::NUMBER_OF_UNITS] = {false};
    bool waterLevelStates[SystemConfig::NUMBER_OF_UNITS] = {false};
    bool previousWaterLevelStates[SystemConfig::NUMBER_OF_UNITS] = {false};
    unsigned long atomizerOnIntervals[SystemConfig::NUMBER_OF_UNITS] = {0};
    unsigned long atomizerOffIntervals[SystemConfig::NUMBER_OF_UNITS] = {0};
    
    // Light Control
    bool lightMasterSwitch = false;
    bool timeCycleEnabled = false;
    unsigned long lightOnTime = 0;
    unsigned long lightOffTime = 0;
    unsigned long lastLightStateChange = 0;  // Track when light state last changed
    bool isLightOn = false;  // Current light state
    
    // System Timing
    unsigned long previousHeartbeatMillis = 0;
    unsigned long lastConnectionCheckMillis = 0;
    unsigned long lastFirmwareCheckMillis = 0;
    unsigned long lastReconnectAttempt = 0;
    
    // Time Validation
    bool timeValid = false;  // Whether system time is valid
    unsigned long lastSyncTime = 0;  // Last successful NTP sync time
    
    // Heap Monitoring
    bool heapWarning = false;  // Whether heap is below warning threshold
    unsigned long minHeapSeen = ESP.getFreeHeap();  // Minimum heap seen during operation
};

// External declarations
extern SystemState systemState;

#endif // CONFIG_H