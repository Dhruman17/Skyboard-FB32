#ifndef ATOMIZER_MANAGER_H
#define ATOMIZER_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "mutex_manager.h"
#include <Arduino.h>

/**
 * AtomizerManager Class
 * 
 * Manages PWM control of atomizers for each unit:
 * 1. PWM Control:
 *    - Configurable PWM frequency and resolution
 *    - Individual control for each unit
 *    - Duty cycle control for fine-grained atomizer operation
 * 
 * 2. Timing Control:
 *    - On/off intervals for each unit
 *    - Automatic state transitions based on timing
 *    - Thread-safe timing updates
 * 
 * 3. Error Handling:
 *    - PWM initialization validation
 *    - Hardware error detection
 *    - Error recovery mechanisms
 */
class AtomizerManager : public MutexManager {
private:
    // PWM configuration
    static constexpr uint8_t PWM_CHANNELS = SystemConfig::NUMBER_OF_UNITS;
    static constexpr uint32_t PWM_FREQUENCY = 25000;  // 25kHz
    static constexpr uint8_t PWM_RESOLUTION = 8;  // 8-bit resolution
    static constexpr uint8_t PWM_DUTY_CYCLE = 128;  // 50% duty cycle
    
    // Timing configuration
    struct AtomizerTiming {
        unsigned long onInterval;
        unsigned long offInterval;
        unsigned long lastStateChange;
        bool isOn;
    };
    
    AtomizerTiming timings[SystemConfig::NUMBER_OF_UNITS];
    
    /**
     * Initializes PWM for a specific channel
     * @param channel PWM channel to initialize
     * @return true if initialization was successful
     */
    bool initializePWMChannel(uint8_t channel) {
        if (channel >= PWM_CHANNELS) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_INVALID_STATE,
                "Invalid PWM channel",
                "AtomizerManager::initializePWMChannel"
            );
            return false;
        }
        
        ledcSetup(channel, PWM_FREQUENCY, PWM_RESOLUTION);
        ledcAttachPin(SystemConfig::ATOMIZER_PINS[channel], channel);
        ledcWrite(channel, 0);  // Start with atomizer off
        
        return true;
    }

public:
    /**
     * Constructor
     */
    AtomizerManager() : MutexManager() {
        // Initialize timings
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            timings[i].onInterval = 0;
            timings[i].offInterval = 0;
            timings[i].lastStateChange = 0;
            timings[i].isOn = false;
        }
    }
    
    /**
     * Destructor
     */
    ~AtomizerManager() {}
    
    /**
     * Initializes the atomizer manager
     * @return true if initialization was successful
     */
    bool begin() {
        // Check if already initialized with minimal mutex time
        bool alreadyInitialized = false;
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return false;
            }
            // We could add an initialized flag here if needed
        }
        
        bool success = true;
        
        // Initialize PWM channels one at a time
        for (uint8_t i = 0; i < PWM_CHANNELS; i++) {
            // Initialize PWM channel with minimal mutex time
            if (!initializePWMChannel(i)) {
                success = false;
                break;
            }
            
            // Allow other tasks to run
            yield();
        }
        
        return success;
    }
    
    /**
     * Sets timing for a specific unit
     * @param unitIndex Index of the unit
     * @param onInterval On interval in milliseconds
     * @param offInterval Off interval in milliseconds
     * @return true if timing was set successfully
     */
    bool setTiming(uint8_t unitIndex, unsigned long onInterval, unsigned long offInterval) {
        // Validate parameters with minimal mutex time
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INVALID_STATE,
                "Invalid unit index",
                "AtomizerManager::setTiming"
            );
            return false;
        }
        
        if (onInterval == 0 || offInterval == 0) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INVALID_STATE,
                "Invalid timing intervals",
                "AtomizerManager::setTiming"
            );
            return false;
        }
        
        // Update timing data with minimal mutex time
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return false;
            }
            
            timings[unitIndex].onInterval = onInterval;
            timings[unitIndex].offInterval = offInterval;
            timings[unitIndex].lastStateChange = millis();
            timings[unitIndex].isOn = false;
        }
        
        // Perform hardware operation outside of mutex
        ledcWrite(unitIndex, 0);
        
        return true;
    }
    
    /**
     * Updates atomizer states based on timing
     * Should be called in the main loop
     */
    void update() {
        // Process one unit at a time to avoid long mutex holds
        static uint8_t currentUnit = 0;
        
        // Get current time outside of mutex
        unsigned long currentMillis = millis();
        
        // Get timing data for current unit with minimal mutex time
        bool isOn = false;
        unsigned long lastStateChange = 0;
        unsigned long onInterval = 0;
        unsigned long offInterval = 0;
        
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return;
            }
            
            if (currentUnit >= SystemConfig::NUMBER_OF_UNITS) {
                currentUnit = 0;
                return;
            }
            
            if (timings[currentUnit].onInterval == 0 || timings[currentUnit].offInterval == 0) {
                // Skip unconfigured units
                currentUnit = (currentUnit + 1) % SystemConfig::NUMBER_OF_UNITS;
                return;
            }
            
            isOn = timings[currentUnit].isOn;
            lastStateChange = timings[currentUnit].lastStateChange;
            onInterval = timings[currentUnit].onInterval;
            offInterval = timings[currentUnit].offInterval;
        }
        
        // Calculate elapsed time outside of mutex
        unsigned long elapsed = currentMillis - lastStateChange;
        
        // Determine if state change is needed
        bool needStateChange = false;
        bool newState = isOn;
        
        if (isOn && elapsed >= onInterval) {
            // Turn off atomizer
            needStateChange = true;
            newState = false;
        } else if (!isOn && elapsed >= offInterval) {
            // Turn on atomizer
            needStateChange = true;
            newState = true;
        }
        
        // Update state if needed
        if (needStateChange) {
            // Update hardware outside of mutex
            ledcWrite(currentUnit, newState ? PWM_DUTY_CYCLE : 0);
            
            // Update timing data with minimal mutex time
            {
                ScopedLock lock(*this);
                if (!lock.isLocked()) {
                    return;
                }
                
                timings[currentUnit].isOn = newState;
                timings[currentUnit].lastStateChange = currentMillis;
            }
        }
        
        // Move to next unit for next call
        currentUnit = (currentUnit + 1) % SystemConfig::NUMBER_OF_UNITS;
    }
    
    /**
     * Gets the current state of an atomizer
     * @param unitIndex Index of the unit
     * @return true if atomizer is on
     */
    bool isAtomizerOn(uint8_t unitIndex) const {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return false;
        }
        
        return timings[unitIndex].isOn;
    }
    
    /**
     * Gets the remaining time in the current state
     * @param unitIndex Index of the unit
     * @return Remaining time in milliseconds
     */
    unsigned long getRemainingTime(uint8_t unitIndex) const {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return 0;
        }
        
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return 0;
        }
        
        unsigned long currentMillis = millis();
        unsigned long elapsed = currentMillis - timings[unitIndex].lastStateChange;
        
        if (timings[unitIndex].isOn) {
            return timings[unitIndex].onInterval - elapsed;
        } else {
            return timings[unitIndex].offInterval - elapsed;
        }
    }
};

#endif // ATOMIZER_MANAGER_H 