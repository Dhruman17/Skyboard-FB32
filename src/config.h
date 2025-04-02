#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <MCP3X21.h>
#include <Wire.h>
#include <time.h>

struct SystemConfig {
    // Firmware version
    static constexpr const char* FIRMWARE_VERSION = "1.1.0";
    
    // Hardware configuration
    static constexpr int NUMBER_OF_UNITS = 3;
    
    // Pin arrays
    static const uint8_t WATER_LEVEL_PINS[NUMBER_OF_UNITS];
    static const uint8_t ATOMIZER_PINS[NUMBER_OF_UNITS];
    
    // System pins
    static constexpr uint8_t SYSTEM_12V_POWER_PIN = 32;
    static constexpr uint8_t SYSTEM_LIGHTS_PIN = 33;
    
    // PWM configuration
    static constexpr int PWM_FREQUENCY_ATOMIZER = 100000;
    static constexpr int PWM_RESOLUTION_ATOMIZER = 8;
    static constexpr int PWM_ATOMIZER_ON = 9;
    static constexpr int PWM_ATOMIZER_OFF = 0;
    
    // I2C configuration
    static constexpr uint8_t I2C_SDA = 21;
    static constexpr uint8_t I2C_SCL = 22;
    
    // Timing configuration
    static constexpr unsigned long INTERVAL_30_SECONDS = 30000;
    static constexpr unsigned long WIFI_RESET_INTERVAL = 300000;  // 5 minutes
    static constexpr unsigned long FIRMWARE_CHECK_INTERVAL = 3600000;  // 1 hour
    
    // Sensor configuration
    static constexpr float EC_CALIBRATION_FACTOR = 0.727;
    static constexpr float EC_CALIBRATION_OFFSET = -0.365;
    static constexpr float EC_CALIBRATION_SQUARE = 0.416;
};

// Initialize static members
const uint8_t SystemConfig::ATOMIZER_PINS[SystemConfig::NUMBER_OF_UNITS] = {25, 26, 27};
const uint8_t SystemConfig::WATER_LEVEL_PINS[SystemConfig::NUMBER_OF_UNITS] = {14, 12, 13};

// System state structure
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