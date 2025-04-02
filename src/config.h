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

// Firebase path constant
const char* systemPath = "systems";

/**
 * System Configuration
 * Contains all hardware and software configuration parameters
 */
namespace SystemConfig {
    // Firmware version
    static constexpr const char* FIRMWARE_VERSION = "1.5";
    
    // Firebase API Key
    static constexpr const char* FIREBASE_API_KEY = "AIzaSyDxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";  // Replace with your actual API key
    
    // Number of units in the system
    static constexpr int NUMBER_OF_UNITS = 3;
    
    // Pin arrays
    static constexpr uint8_t WATER_LEVEL_PINS[NUMBER_OF_UNITS] = {14, 12, 13};
    static constexpr uint8_t ATOMIZER_PINS[NUMBER_OF_UNITS] = {25, 26, 27};
    static constexpr uint8_t PCA_ADDRS[NUMBER_OF_UNITS] = {0xE0, 0xE2, 0xE4};
    
    // System pins
    static constexpr uint8_t SYSTEM_12V_POWER_PIN = 32;  // Required for atomizer power
    static constexpr uint8_t SYSTEM_LIGHTS_PIN = 33;     // Controls system lighting
    
    // PWM configuration
    static constexpr int PWM_FREQUENCY_ATOMIZER = 108000;
    static constexpr int PWM_RESOLUTION_ATOMIZER = 8;
    static constexpr int PWM_ATOMIZER_ON = 9;
    static constexpr int PWM_ATOMIZER_OFF = 0;
    
    // I2C configuration
    static constexpr uint8_t I2C_SDA = 21;
    static constexpr uint8_t I2C_SCL = 22;
    static constexpr uint8_t TCAADDR = 0x70;
    
    // Sensor Channel Configuration
    static constexpr uint8_t FDC1004_CHANNEL = 2;  // Third channel for capacitive sensor
    static constexpr uint8_t MCP3021_CHANNEL = 3;  // Fourth channel for EC sensor
    static constexpr uint8_t FDC1004_ADDR = 0x50;  // I2C address for FDC1004 sensor
    
    // Timing Configuration (in milliseconds)
    static constexpr unsigned long INTERVAL_30_SECONDS = 30000;
    static constexpr unsigned long WIFI_RESET_INTERVAL = 300000;  // 5 minutes
    static constexpr unsigned long FIRMWARE_CHECK_INTERVAL = 3600000;  // 1 hour
    static constexpr unsigned long WATER_LEVEL_READ_INTERVAL = 100;  // 100ms
    
    // Default Intervals (in seconds)
    static constexpr int DEFAULT_ATOMIZER_ON_INTERVAL = 5;
    static constexpr int DEFAULT_ATOMIZER_OFF_INTERVAL = 10;
    
    // Sensor configuration
    static constexpr float EC_CALIBRATION_FACTOR = 0.727;
    static constexpr float EC_CALIBRATION_OFFSET = -0.365;
    static constexpr float EC_CALIBRATION_SQUARE = 0.416;
    static constexpr float EC_MAX = 5.0;  // Maximum EC value in mS/cm
    
    // Water level sensor configuration
    static constexpr uint8_t MEASURMENT = 1;
    static constexpr uint8_t CHANNEL = 1;
    static constexpr float WATER_LEVEL_CALIBRATION_OFFSET = 0.0;
    static constexpr float WATER_LEVEL_CALIBRATION_FACTOR = 1.0;
    static constexpr float WATER_LEVEL_MIN = 0.0;  // Minimum capacitance value
    static constexpr float WATER_LEVEL_MAX = 100.0;  // Maximum capacitance value
    static constexpr float UPPER_BOUND = 100.0;
    static constexpr float LOWER_BOUND = 0.0;

    // Firebase Configuration
    static constexpr int FIREBASE_TIMEOUT = 10000;  // 10 seconds timeout for Firebase operations
    static constexpr int FIREBASE_RETRY_DELAY = 5000;  // 5 seconds between retry attempts
    static constexpr int MAX_FIREBASE_RETRIES = 3;  // Maximum number of retry attempts
    
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
extern String serialNumber;
extern SystemState systemState;

#endif // CONFIG_H