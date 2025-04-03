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
    AtomizerManager() {
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
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        bool success = true;
        
        // Initialize PWM channels
        for (uint8_t i = 0; i < PWM_CHANNELS; i++) {
            if (!initializePWMChannel(i)) {
                success = false;
                break;
            }
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
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
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
        
        timings[unitIndex].onInterval = onInterval;
        timings[unitIndex].offInterval = offInterval;
        timings[unitIndex].lastStateChange = millis();
        timings[unitIndex].isOn = false;
        
        // Start with atomizer off
        ledcWrite(unitIndex, 0);
        
        return true;
    }
    
    /**
     * Updates atomizer states based on timing
     * Should be called in the main loop
     */
    void update() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return;
        }
        
        unsigned long currentMillis = millis();
        
        for (uint8_t i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (timings[i].onInterval == 0 || timings[i].offInterval == 0) {
                continue;  // Skip unconfigured units
            }
            
            unsigned long elapsed = currentMillis - timings[i].lastStateChange;
            
            if (timings[i].isOn) {
                if (elapsed >= timings[i].onInterval) {
                    // Turn off atomizer
                    ledcWrite(i, 0);
                    timings[i].isOn = false;
                    timings[i].lastStateChange = currentMillis;
                }
            } else {
                if (elapsed >= timings[i].offInterval) {
                    // Turn on atomizer
                    ledcWrite(i, PWM_DUTY_CYCLE);
                    timings[i].isOn = true;
                    timings[i].lastStateChange = currentMillis;
                }
            }
        }
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