#ifndef ATOMIZER_MANAGER_H
#define ATOMIZER_MANAGER_H

#include "config.h"
#include "error_manager.h"
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
class AtomizerManager {
private:
    // PWM configuration
    static constexpr uint8_t PWM_CHANNELS = SystemConfig::NUMBER_OF_UNITS;
    static constexpr uint32_t PWM_FREQUENCY = 25000;  // 25kHz
    static constexpr uint8_t PWM_RESOLUTION = 8;  // 8-bit resolution (0-255)
    static constexpr uint8_t PWM_DUTY_CYCLE = 128;  // 50% duty cycle
    
    // Pin configuration
    static constexpr int ATOMIZER_PINS[SystemConfig::NUMBER_OF_UNITS] = {
        SystemConfig::ATOMIZER_PIN_1,
        SystemConfig::ATOMIZER_PIN_2,
        SystemConfig::ATOMIZER_PIN_3
    };
    
    // Timing configuration
    struct AtomizerTiming {
        unsigned long onInterval;
        unsigned long offInterval;
        unsigned long lastStateChange;
        bool isOn;
    };
    
    AtomizerTiming timings[SystemConfig::NUMBER_OF_UNITS];
    mutable SemaphoreHandle_t mutex;
    
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
        ledcAttachPin(ATOMIZER_PINS[channel], channel);
        ledcWrite(channel, 0);  // Start with atomizer off
        
        return true;
    }
    
    /**
     * Takes mutex with timeout
     * @return true if mutex was taken
     */
    bool takeMutex() const {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(SystemConfig::MUTEX_TIMEOUT_MS)) != pdTRUE) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "AtomizerManager::takeMutex"
            );
            return false;
        }
        return true;
    }
    
    /**
     * Releases mutex
     */
    void giveMutex() const {
        xSemaphoreGive(mutex);
    }

public:
    /**
     * Constructor
     */
    AtomizerManager() {
        mutex = xSemaphoreCreateMutex();
        if (!mutex) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_CREATION_FAILED,
                "Failed to create mutex",
                "AtomizerManager::AtomizerManager"
            );
        }
        
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
    ~AtomizerManager() {
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Initializes the atomizer manager
     * @return true if initialization was successful
     */
    bool begin() {
        if (!takeMutex()) {
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
        
        giveMutex();
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
        if (!takeMutex()) {
            return false;
        }
        
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INVALID_STATE,
                "Invalid unit index",
                "AtomizerManager::setTiming"
            );
            giveMutex();
            return false;
        }
        
        if (onInterval == 0 || offInterval == 0) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INVALID_STATE,
                "Invalid timing intervals",
                "AtomizerManager::setTiming"
            );
            giveMutex();
            return false;
        }
        
        timings[unitIndex].onInterval = onInterval;
        timings[unitIndex].offInterval = offInterval;
        timings[unitIndex].lastStateChange = millis();
        timings[unitIndex].isOn = false;
        
        // Start with atomizer off
        ledcWrite(unitIndex, 0);
        
        giveMutex();
        return true;
    }
    
    /**
     * Updates atomizer states based on timing
     */
    void update() {
        if (!takeMutex()) {
            return;
        }
        
        unsigned long currentMillis = millis();
        
        for (uint8_t i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            AtomizerTiming& timing = timings[i];
            unsigned long elapsed = currentMillis - timing.lastStateChange;
            
            if (timing.isOn && elapsed >= timing.onInterval) {
                // Turn off atomizer
                ledcWrite(i, 0);
                timing.isOn = false;
                timing.lastStateChange = currentMillis;
            } else if (!timing.isOn && elapsed >= timing.offInterval) {
                // Turn on atomizer
                ledcWrite(i, PWM_DUTY_CYCLE);
                timing.isOn = true;
                timing.lastStateChange = currentMillis;
            }
        }
        
        giveMutex();
    }
    
    /**
     * Gets the current state of an atomizer
     * @param unitIndex Index of the unit
     * @return true if atomizer is on
     */
    bool isOn(uint8_t unitIndex) const {
        if (!takeMutex()) {
            return false;
        }
        
        bool state = false;
        if (unitIndex < SystemConfig::NUMBER_OF_UNITS) {
            state = timings[unitIndex].isOn;
        }
        
        giveMutex();
        return state;
    }
};

#endif // ATOMIZER_MANAGER_H 