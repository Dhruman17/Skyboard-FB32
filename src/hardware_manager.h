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
     * Cleans up hardware resources
     */
    void cleanup() {
        if (!takeMutex()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "cleanup");
            return;
        }
        
        // Reset multiplexer states
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            multiplexerStates[i] = false;
            pwmInitialized[i] = false;
        }
        
        // Reset I2C bus
        Wire.end();
        
        // Reset error count
        i2cErrorCount = 0;
        
        initialized = false;
        giveMutex();
    }
    
    /**
     * Initializes hardware resources
     * @return true if initialization successful
     */
    bool initialize() {
        if (!takeMutex()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "initialize");
            return false;
        }
        
        bool success = true;
        
        // Initialize I2C
        Wire.begin();
        Wire.setClock(100000);  // Set I2C clock to 100kHz
        
        // Initialize multiplexer states
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            multiplexerStates[i] = false;
            pwmInitialized[i] = false;
        }
        
        // Initialize PWM for atomizers
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!initializePWM(i)) {
                Serial.printf("Failed to initialize PWM for unit %d\n", i);
                success = false;
                break;
            }
        }
        
        if (success) {
            initialized = true;
        } else {
            cleanup();
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Initializes PWM for a specific unit
     * @param unitIndex Index of the unit
     * @return true if initialization successful
     */
    bool initializePWM(int unitIndex) {
        if (unitIndex < 0 || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return false;
        }
        
        // Initialize PWM pin
        pinMode(SystemConfig::ATOMIZER_PINS[unitIndex], OUTPUT);
        analogWrite(SystemConfig::ATOMIZER_PINS[unitIndex], 0);
        
        pwmInitialized[unitIndex] = true;
        return true;
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
    HardwareManager() : initialized(false), mutex(NULL), i2cErrorCount(0) {
        // Initialize arrays
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            multiplexerStates[i] = false;
            pwmInitialized[i] = false;
        }
        
        // Create mutex for thread safety
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("CRITICAL ERROR: Failed to create mutex in HardwareManager");
        }
    }
    
    /**
     * Destructor
     * Ensures proper cleanup of hardware resources
     */
    ~HardwareManager() {
        cleanup();
        if (mutex != NULL) {
            // Ensure we're not holding the mutex before deleting
            if (xSemaphoreGetMutexHolder(mutex) == xTaskGetCurrentTaskHandle()) {
                xSemaphoreGive(mutex);
            }
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Initializes the hardware manager
     * @return true if initialization successful
     */
    bool begin() {
        if (initialized) {
            return true;
        }
        return initialize();
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