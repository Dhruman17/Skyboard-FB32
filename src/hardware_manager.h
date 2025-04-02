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
    bool initialized;
    SemaphoreHandle_t mutex;
    
    // Hardware state tracking
    bool multiplexerStates[SystemConfig::NUMBER_OF_UNITS];
    bool pwmInitialized[SystemConfig::NUMBER_OF_UNITS];
    
    // Error tracking
    uint8_t i2cErrorCount;
    static constexpr uint8_t MAX_I2C_ERRORS = 3;
    
    /**
     * Safely takes the mutex with timeout
     * @return true if mutex was taken successfully
     */
    bool takeMutex() {
        return xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE;
    }
    
    /**
     * Safely gives the mutex
     */
    void giveMutex() {
        xSemaphoreGive(mutex);
    }
    
    /**
     * Selects a unit using the main multiplexer with error handling
     * @param unitIndex Index of the unit to select
     * @return true if selection successful
     */
    bool selectUnit(int unitIndex) {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in selectUnit");
            return false;
        }
        
        bool success = false;
        if (unitIndex >= 0 && unitIndex < SystemConfig::NUMBER_OF_UNITS) {
            Wire.beginTransmission(SystemConfig::TCAADDR);
            Wire.write(1 << unitIndex);
            success = Wire.endTransmission() == 0;
            
            if (success) {
                multiplexerStates[unitIndex] = true;
            } else {
                i2cErrorCount++;
                Serial.println("Failed to select unit " + String(unitIndex));
            }
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Selects a sensor using the unit's multiplexer with error handling
     * @param unitIndex Index of the unit
     * @param sensorChannel Channel to select
     * @return true if selection successful
     */
    bool selectSensor(int unitIndex, int sensorChannel) {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in selectSensor");
            return false;
        }
        
        bool success = false;
        if (unitIndex >= 0 && unitIndex < SystemConfig::NUMBER_OF_UNITS &&
            sensorChannel >= 0 && sensorChannel < 4) {
            Wire.beginTransmission(SystemConfig::PCA_ADDRS[unitIndex]);
            Wire.write(1 << sensorChannel);
            success = Wire.endTransmission() == 0;
            
            if (!success) {
                i2cErrorCount++;
                Serial.println("Failed to select sensor channel " + String(sensorChannel) + 
                             " for unit " + String(unitIndex));
            }
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Initializes I2C with error handling and recovery
     * @return true if initialization successful
     */
    bool initializeI2C() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in initializeI2C");
            return false;
        }
        
        bool success = true;
        i2cErrorCount = 0;
        
        // Initialize I2C
        Wire.begin();
        Wire.setClock(100000); // 100kHz I2C clock
        
        // Test main multiplexer
        Wire.beginTransmission(SystemConfig::TCAADDR);
        if (Wire.endTransmission() != 0) {
            Serial.println("Failed to communicate with main multiplexer");
            success = false;
        }
        
        // Test unit multiplexers
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!selectUnit(i)) {
                Serial.println("Failed to select unit " + String(i));
                success = false;
            }
            
            Wire.beginTransmission(SystemConfig::PCA_ADDRS[i]);
            if (Wire.endTransmission() != 0) {
                Serial.println("Failed to communicate with unit " + String(i) + " multiplexer");
                success = false;
            }
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Initializes GPIO pins with error checking
     * @return true if initialization successful
     */
    bool initializePins() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in initializePins");
            return false;
        }
        
        bool success = true;
        
        // Initialize water level pins
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (SystemConfig::WATER_LEVEL_PINS[i] != -1) {
                pinMode(SystemConfig::WATER_LEVEL_PINS[i], INPUT_PULLUP);
            }
        }
        
        // Initialize PWM channels for atomizers
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (SystemConfig::ATOMIZER_PINS[i] != -1) {
                ledcSetup(i, SystemConfig::PWM_FREQUENCY_ATOMIZER, SystemConfig::PWM_RESOLUTION_ATOMIZER);
                ledcAttachPin(SystemConfig::ATOMIZER_PINS[i], i);
                ledcWrite(i, SystemConfig::PWM_ATOMIZER_OFF);
                pwmInitialized[i] = true;
            } else {
                pwmInitialized[i] = false;
            }
        }
        
        // Initialize system pins
        if (SystemConfig::SYSTEM_12V_POWER_PIN != -1) {
            pinMode(SystemConfig::SYSTEM_12V_POWER_PIN, OUTPUT);
            digitalWrite(SystemConfig::SYSTEM_12V_POWER_PIN, HIGH);
        }
        
        if (SystemConfig::SYSTEM_LIGHTS_PIN != -1) {
            pinMode(SystemConfig::SYSTEM_LIGHTS_PIN, OUTPUT);
            digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Resets I2C bus if too many errors occur
     */
    void resetI2CIfNeeded() {
        if (!takeMutex()) {
            return;
        }
        
        if (i2cErrorCount >= MAX_I2C_ERRORS) {
            Serial.println("Too many I2C errors, resetting I2C bus...");
            Wire.end();
            delay(100);
            Wire.begin();
            Wire.setClock(100000);
            i2cErrorCount = 0;
            
            // Reinitialize multiplexers
            for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                selectUnit(i);
            }
        }
        
        giveMutex();
    }

public:
    /**
     * Constructor
     */
    HardwareManager() : initialized(false), i2cErrorCount(0) {
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("Failed to create mutex in HardwareManager");
        }
        
        // Initialize state arrays
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            multiplexerStates[i] = false;
            pwmInitialized[i] = false;
        }
    }
    
    /**
     * Destructor
     */
    ~HardwareManager() {
        if (mutex != NULL) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Initializes hardware components
     * Sets up I2C, PWM, and sensor communication
     * @return true if initialization successful
     */
    bool begin() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in begin");
            return false;
        }
        
        bool success = true;
        
        if (!initializeI2C()) {
            Serial.println("I2C initialization failed");
            success = false;
        }
        
        if (success && !initializePins()) {
            Serial.println("Pin initialization failed");
            success = false;
        }
        
        if (success) {
            initialized = true;
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Selects both unit and sensor in one call with error handling
     * @param unitIndex Index of the unit
     * @param sensorChannel Channel to select
     * @return true if selection successful
     */
    bool selectUnitAndSensor(int unitIndex, int sensorChannel) {
        if (!initialized) {
            Serial.println("HardwareManager not initialized");
            return false;
        }
        
        resetI2CIfNeeded();
        
        if (!selectUnit(unitIndex)) {
            return false;
        }
        return selectSensor(unitIndex, sensorChannel);
    }
    
    /**
     * Gets the current multiplexer state for a unit
     * @param unitIndex Index of the unit
     * @return true if unit is selected
     */
    bool isUnitSelected(int unitIndex) {
        if (!takeMutex()) {
            return false;
        }
        
        bool selected = unitIndex >= 0 && unitIndex < SystemConfig::NUMBER_OF_UNITS ? 
                       multiplexerStates[unitIndex] : false;
        
        giveMutex();
        return selected;
    }
    
    /**
     * Gets the current PWM initialization state for a unit
     * @param unitIndex Index of the unit
     * @return true if PWM is initialized
     */
    bool isPWMInitialized(int unitIndex) {
        if (!takeMutex()) {
            return false;
        }
        
        bool initialized = unitIndex >= 0 && unitIndex < SystemConfig::NUMBER_OF_UNITS ? 
                          pwmInitialized[unitIndex] : false;
        
        giveMutex();
        return initialized;
    }
};

#endif // HARDWARE_MANAGER_H 