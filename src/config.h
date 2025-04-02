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
 * - systemName: stringValue - Name of the system
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
    static constexpr int NUMBER_OF_UNITS = 4;  // Number of vertical farming units
    static constexpr const char* systemPath = "systems";  // Base path for system data in Firebase
    static constexpr const char* FIRMWARE_VERSION = "1.5";  // Current firmware version
    static constexpr const char* SERIAL_NUMBER = "FB32";  // System serial number
    
    // Buffer Sizes
    static constexpr size_t FIREBASE_PATH_BUFFER_SIZE = 256;  // Standard size for Firebase paths
    static constexpr size_t JSON_BUFFER_SIZE = 1024;  // Standard size for JSON buffers
    static constexpr size_t ERROR_MESSAGE_BUFFER_SIZE = 128;  // Standard size for error messages
    
    // Error Handling Configuration
    static constexpr uint8_t MAX_I2C_RETRIES = 3;  // Maximum number of I2C operation retries
    static constexpr uint8_t MAX_SENSOR_RETRIES = 3;  // Maximum number of sensor reading retries
    static constexpr uint8_t MAX_FIREBASE_RETRIES = 3;  // Maximum number of Firebase operation retries
    static constexpr uint8_t MAX_CONSECUTIVE_ERRORS = 5;  // Maximum number of consecutive errors before system reset
    static constexpr uint32_t I2C_RETRY_DELAY_MS = 100;  // Delay between I2C retries
    static constexpr uint32_t SENSOR_RETRY_DELAY_MS = 100;  // Delay between sensor retries
    static constexpr uint32_t FIREBASE_RETRY_DELAY_MS = 5000;  // Delay between Firebase retries
    static constexpr uint32_t ERROR_RESET_DELAY_MS = 1000;  // Delay before system reset after max errors
    
    // Mutex Configuration
    static constexpr uint32_t MUTEX_TIMEOUT_MS = 100;  // Standard mutex timeout
    
    // Error Message Format
    static constexpr const char* ERROR_FORMAT_FIREBASE = "Firebase error in %s: %s\n";
    static constexpr const char* ERROR_FORMAT_I2C = "I2C error in %s for unit %d: %d\n";
    static constexpr const char* ERROR_FORMAT_SENSOR = "Sensor error in %s for unit %d: %s\n";
    static constexpr const char* ERROR_FORMAT_MUTEX = "Failed to take mutex in %s\n";
    static constexpr const char* ERROR_FORMAT_GENERIC = "%s: %s\n";
    
    // Sensor Types
    static constexpr const char* SENSOR_TYPE_WATER = "water";  // Water level sensor type
    static constexpr const char* SENSOR_TYPE_EC = "ec";  // EC sensor type
    
    // Pin Configuration
    static constexpr int I2C_SDA = 21;  // I2C data pin
    static constexpr int I2C_SCL = 22;  // I2C clock pin
    static constexpr int WATER_LEVEL_PINS[] = {32, 33, 34, 35};  // Water level sensor pins
    static constexpr int ATOMIZER_PINS[] = {25, 26, 27, 14};  // Atomizer control pins
    static constexpr int PWM_CHANNELS[] = {0, 1, 2, 3};  // PWM channels for atomizers
    static constexpr int PWM_FREQ = 25000;  // PWM frequency in Hz
    static constexpr int PWM_RESOLUTION = 8;  // PWM resolution in bits
    static constexpr int PWM_ATOMIZER_ON = 255;  // PWM value for atomizer on
    static constexpr int PWM_ATOMIZER_OFF = 0;  // PWM value for atomizer off
    static constexpr int SYSTEM_12V_POWER_PIN = 32;  // Required for atomizer power
    static constexpr int SYSTEM_LIGHTS_PIN = 33;  // Controls system lighting
    
    // I2C Configuration
    static constexpr uint8_t TCAADDR = 0x70;  // Default I2C multiplexer address
    static constexpr uint8_t PCA_ADDRS[] = {0x70, 0x71, 0x72, 0x73};  // I2C multiplexer addresses
    static constexpr uint8_t FDC1004_ADDR = 0x50;  // FDC1004 water level sensor address
    static constexpr uint8_t MCP3021_ADDR = 0x4D;  // MCP3021 EC sensor address
    static constexpr uint32_t I2C_FREQ = 100000;  // I2C frequency in Hz
    static constexpr uint8_t FDC1004_CHANNEL = 2;  // Third channel for capacitive sensor
    static constexpr uint8_t MCP3021_CHANNEL = 3;  // Fourth channel for EC sensor
    
    // PWM Configuration
    static constexpr int PWM_FREQUENCY_ATOMIZER = 25000;  // PWM frequency for atomizers
    static constexpr int PWM_RESOLUTION_ATOMIZER = 8;  // PWM resolution for atomizers
    
    // Timing Configuration
    static constexpr uint32_t SENSOR_READ_INTERVAL = 30000;  // Sensor read interval in ms
    static constexpr uint32_t FIREBASE_UPDATE_INTERVAL = 5000;  // Firebase update interval in ms
    static constexpr uint32_t INTERVAL_30_SECONDS = 30000;  // 30 seconds interval
    static constexpr uint32_t FIRMWARE_CHECK_INTERVAL = 3600000;  // 1 hour
    
    // Firebase Configuration
    static constexpr int FIREBASE_TIMEOUT = 10000;  // 10 seconds timeout for Firebase operations
    static constexpr uint32_t FIREBASE_BATCH_SIZE = 10;  // Number of operations to batch together
    static constexpr uint32_t FIREBASE_BATCH_DELAY_MS = 100;  // Delay between batched operations
    
    // Firebase Paths
    static constexpr const char* UNIT_PATH_FORMAT = "%s/units/%d";
    static constexpr const char* SYSTEM_PATH_FORMAT = "%s/system";
    static constexpr const char* UNIT_NAME_PATH = "fields/unitName/stringValue";
    static constexpr const char* UNIT_ENABLED_PATH = "fields/enabled/booleanValue";
    static constexpr const char* UNIT_ON_INTERVAL_PATH = "fields/onInterval/integerValue";
    static constexpr const char* UNIT_OFF_INTERVAL_PATH = "fields/offInterval/integerValue";
    static constexpr const char* UNIT_WATER_LEVEL_PATH = "fields/waterLevel/doubleValue";
    static constexpr const char* UNIT_WATER_LEVEL_STATE_PATH = "fields/waterLevelState/booleanValue";
    static constexpr const char* UNIT_EC_VALUE_PATH = "fields/ecValue/doubleValue";
    static constexpr const char* UNIT_EC_LAST_UPDATED_PATH = "fields/ecLastUpdated/timestampValue";
    static constexpr const char* SYSTEM_LAST_SEEN_PATH = "fields/lastSeen/timestampValue";
    
    // Sensor Configuration
    static constexpr float WATER_LEVEL_MIN = 0.0f;  // Minimum water level value
    static constexpr float WATER_LEVEL_MAX = 100.0f;  // Maximum water level value
    static constexpr float EC_MAX = 5.0f;  // Maximum EC value in mS/cm
    
    // EEPROM Configuration
    static constexpr int EEPROM_SIZE = 512;
    static constexpr int EEPROM_API_KEY_ADDR = 0;
    static constexpr int EEPROM_EMAIL_ADDR = 128;
    static constexpr int EEPROM_PASSWORD_ADDR = 256;
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
    bool unitsEnabled[SystemConfig::NUMBER_OF_UNITS] = {false};
    bool waterLevelStates[SystemConfig::NUMBER_OF_UNITS] = {false};
    bool previousWaterLevelStates[SystemConfig::NUMBER_OF_UNITS] = {false};
    unsigned long atomizerOnIntervals[SystemConfig::NUMBER_OF_UNITS] = {0};
    unsigned long atomizerOffIntervals[SystemConfig::NUMBER_OF_UNITS] = {0};
    unsigned long previousHeartbeatMillis = 0;
    unsigned long lastConnectionCheckMillis = 0;
    unsigned long lastFirmwareCheckMillis = 0;
};

// External declarations
extern SystemState systemState;

#endif // CONFIG_H