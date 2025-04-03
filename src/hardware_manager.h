#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "mutex_manager.h"
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
class HardwareManager : public MutexManager {
private:
    bool initialized;
    
    // Hardware state tracking
    bool multiplexerStates[SystemConfig::NUMBER_OF_UNITS];
    bool pwmInitialized[SystemConfig::NUMBER_OF_UNITS];
    
    // Error tracking
    uint8_t i2cErrorCount;
    static constexpr uint8_t MAX_I2C_ERRORS = 3;
    
    /**
     * Cleans up hardware resources
     */
    void cleanup() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return;
        }
        
        // Reset all multiplexers
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            multiplexerStates[i] = false;
            pwmInitialized[i] = false;
        }
        
        // Reset I2C bus
        Wire.end();
        Wire.begin();
        Wire.setClock(100000);
        
        initialized = false;
    }
    
    /**
     * Initializes hardware resources
     * @return true if initialization successful
     */
    bool initialize() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
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
        
        return success;
    }
    
    /**
     * Initializes PWM for a specific unit
     * @param unitIndex Index of the unit
     * @return true if initialization was successful
     */
    bool initializePWM(int unitIndex) {
        if (unitIndex < 0 || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_INVALID_STATE,
                "Invalid unit index for PWM initialization",
                "HardwareManager::initializePWM"
            );
            return false;
        }
        
        // Get the correct pin based on unit index
        int pin;
        switch (unitIndex) {
            case 0:
                pin = SystemConfig::ATOMIZER_PIN_1;
                break;
            case 1:
                pin = SystemConfig::ATOMIZER_PIN_2;
                break;
            case 2:
                pin = SystemConfig::ATOMIZER_PIN_3;
                break;
            default:
                ErrorManager::hardwareError(
                    ErrorManager::ErrorCode::HARDWARE_INVALID_STATE,
                    "Invalid unit index for PWM initialization",
                    "HardwareManager::initializePWM"
                );
                return false;
        }
        
        // Initialize PWM
        pinMode(pin, OUTPUT);
        ledcSetup(unitIndex, SystemConfig::PWM_FREQUENCY_ATOMIZER, SystemConfig::PWM_RESOLUTION_ATOMIZER);
        ledcAttachPin(pin, unitIndex);
        ledcWrite(unitIndex, SystemConfig::PWM_ATOMIZER_OFF);  // Start with atomizer off
        pwmInitialized[unitIndex] = true;
        
        return true;
    }
    
    /**
     * Selects a unit using the main multiplexer with error handling
     * @param unitIndex Index of the unit to select
     * @return true if selection successful
     */
    bool selectUnit(int unitIndex) {
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_INVALID_STATE,
                "Invalid unit index",
                "HardwareManager::selectUnit"
            );
            return false;
        }
        
        bool success = false;
        Wire.beginTransmission(SystemConfig::TCAADDR);
        Wire.write(1 << unitIndex);
        success = Wire.endTransmission() == 0;
        
        if (success) {
            multiplexerStates[unitIndex] = true;
        } else {
            i2cErrorCount++;
            Serial.println("Failed to select unit " + String(unitIndex));
        }
        
        return success;
    }
    
    /**
     * Selects a sensor using the unit's multiplexer with error handling
     * @param unitIndex Index of the unit
     * @param sensorChannel Channel to select
     * @return true if selection successful
     */
    bool selectSensor(int unitIndex, int sensorChannel) {
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS || sensorChannel >= SystemConfig::MAX_SENSOR_CHANNELS) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_INVALID_STATE,
                "Invalid unit or sensor channel",
                "HardwareManager::selectSensor"
            );
            return false;
        }
        
        bool success = false;
        Wire.beginTransmission(SystemConfig::PCA_ADDRS[unitIndex]);
        Wire.write(1 << sensorChannel);
        success = Wire.endTransmission() == 0;
        
        if (!success) {
            i2cErrorCount++;
            Serial.println("Failed to select sensor channel " + String(sensorChannel) + 
                         " for unit " + String(unitIndex));
        }
        
        return success;
    }
    
    /**
     * Resets I2C bus if too many errors occur
     */
    void resetI2CIfNeeded() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
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
    }

public:
    /**
     * Constructor
     */
    HardwareManager() : MutexManager(), initialized(false), i2cErrorCount(0) {
        // Initialize arrays
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            multiplexerStates[i] = false;
            pwmInitialized[i] = false;
        }
    }
    
    /**
     * Destructor
     * Ensures proper cleanup of hardware resources
     */
    ~HardwareManager() {
        cleanup();
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
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_INVALID_STATE,
                "Invalid unit index",
                "HardwareManager::selectUnitAndSensor"
            );
            return false;
        }
        
        if (sensorChannel >= SystemConfig::MAX_SENSOR_CHANNELS) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_INVALID_STATE,
                "Invalid sensor channel",
                "HardwareManager::selectUnitAndSensor"
            );
            return false;
        }
        
        // Select unit first
        if (!selectUnit(unitIndex)) {
            return false;
        }
        
        // Then select sensor channel
        if (!selectSensor(unitIndex, sensorChannel)) {
            return false;
        }
        
        return true;
    }
    
    /**
     * Gets the current multiplexer state for a unit
     * @param unitIndex Index of the unit
     * @return true if unit is selected
     */
    bool isUnitSelected(int unitIndex) {
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_INVALID_STATE,
                "Invalid unit index",
                "HardwareManager::isUnitSelected"
            );
            return false;
        }
        
        return multiplexerStates[unitIndex];
    }
    
    /**
     * Gets the current PWM initialization state for a unit
     * @param unitIndex Index of the unit
     * @return true if PWM is initialized
     */
    bool isPWMInitialized(int unitIndex) {
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_INVALID_STATE,
                "Invalid unit index",
                "HardwareManager::isPWMInitialized"
            );
            return false;
        }
        
        return pwmInitialized[unitIndex];
    }
};

#endif // HARDWARE_MANAGER_H 