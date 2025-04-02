#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include "config.h"
#include "Wire.h"

/**
 * HardwareManager Class
 * 
 * Manages hardware initialization and control:
 * 1. I2C Multiplexing:
 *    - Two-level multiplexing system
 *    - TCA9548APWR (main multiplexer)
 *    - PCA9546A (unit multiplexers)
 * 
 * 2. Hardware Control:
 *    - PWM setup for atomizers
 *    - GPIO configuration
 *    - Power management
 * 
 * 3. Sensor Management:
 *    - Sensor channel selection
 *    - Unit selection
 *    - Error handling
 * 
 * Hardware Configuration:
 * - TCA9548APWR: Main multiplexer (0x70)
 * - PCA9546A: Unit multiplexers (0x40-0x42)
 * - FDC1004: Channel 2 for water level
 * - MCP3021: Channel 3 for EC
 */
class HardwareManager {
private:
    /**
     * Selects a unit using the main multiplexer
     * @param unitIndex Index of the unit to select
     * @return true if selection successful
     */
    bool selectUnit(int unitIndex) {
        if (unitIndex < 0 || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return false;
        }
        
        Wire.beginTransmission(SystemConfig::TCAADDR);
        Wire.write(1 << unitIndex);
        return Wire.endTransmission() == 0;
    }
    
    /**
     * Selects a sensor using the unit's multiplexer
     * @param unitIndex Index of the unit
     * @param sensorChannel Channel to select
     * @return true if selection successful
     */
    bool selectSensor(int unitIndex, int sensorChannel) {
        if (unitIndex < 0 || unitIndex >= SystemConfig::NUMBER_OF_UNITS ||
            sensorChannel < 0 || sensorChannel >= 4) {
            return false;
        }
        
        Wire.beginTransmission(SystemConfig::PCA_ADDRS[unitIndex]);
        Wire.write(1 << sensorChannel);
        return Wire.endTransmission() == 0;
    }
    
    bool initializeI2C() {
        Wire.begin();
        Wire.setClock(100000); // 100kHz I2C clock
        
        // Test main multiplexer
        Wire.beginTransmission(SystemConfig::TCAADDR);
        if (Wire.endTransmission() != 0) {
            Serial.println("Failed to communicate with main multiplexer");
            return false;
        }
        
        // Test unit multiplexers
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!selectUnit(i)) {
                Serial.println("Failed to select unit " + String(i));
                return false;
            }
            
            Wire.beginTransmission(SystemConfig::PCA_ADDRS[i]);
            if (Wire.endTransmission() != 0) {
                Serial.println("Failed to communicate with unit " + String(i) + " multiplexer");
                return false;
            }
        }
        
        return true;
    }
    
    void initializePins() {
        // Initialize water level pins
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            pinMode(SystemConfig::WATER_LEVEL_PINS[i], INPUT_PULLUP);
        }
        
        // Initialize PWM channels for atomizers
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            ledcSetup(i, SystemConfig::PWM_FREQUENCY_ATOMIZER, SystemConfig::PWM_RESOLUTION_ATOMIZER);
            ledcAttachPin(SystemConfig::ATOMIZER_PINS[i], i);
            ledcWrite(i, SystemConfig::PWM_ATOMIZER_OFF);
        }
        
        // Initialize system pins
        pinMode(SystemConfig::SYSTEM_12V_POWER_PIN, OUTPUT);
        digitalWrite(SystemConfig::SYSTEM_12V_POWER_PIN, HIGH);
        pinMode(SystemConfig::SYSTEM_LIGHTS_PIN, OUTPUT);
        digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
    }

public:
    /**
     * Constructor
     * Initializes I2C and hardware components
     */
    HardwareManager() {}
    
    /**
     * Initializes hardware components
     * Sets up I2C, PWM, and sensor communication
     */
    bool begin() {
        if (!initializeI2C()) {
            return false;
        }
        initializePins();
        return true;
    }
    
    /**
     * Selects both unit and sensor in one call
     * @param unitIndex Index of the unit
     * @param sensorChannel Channel to select
     * @return true if selection successful
     */
    bool selectUnitAndSensor(int unitIndex, int sensorChannel) {
        if (!selectUnit(unitIndex)) {
            return false;
        }
        return selectSensor(unitIndex, sensorChannel);
    }
};

#endif // HARDWARE_MANAGER_H 