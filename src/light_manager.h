#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "mutex_manager.h"
#include <time.h>
#include <Arduino.h>

/**
 * LightManager Class
 * 
 * Manages the system's lighting control with two modes:
 * 1. Time-based cycle mode:
 *    - Lights turn on/off based on configured schedule
 *    - Schedule is set via lightOnTime and lightOffTime
 *    - Can handle overnight cycles (e.g., 5pm off, 2am on)
 *    - Requires valid time synchronization
 * 
 * 2. Manual control mode:
 *    - Lights controlled directly by masterSwitch
 *    - Overrides time-based schedule when timeCycleEnabled is false
 * 
 * 3. Fallback cycle mode:
 *    - Activated when time is invalid or not synchronized
 *    - Uses fixed intervals (6h ON, 18h OFF)
 *    - Prevents incorrect lighting cycles on first boot
 * 
 * Time Validation:
 * - Initializes with timeValid = false at boot
 * - Validates time every hour
 * - Maximum 3 consecutive time failures before alert
 * - Uses fallback cycles when time is invalid
 * 
 * Firebase Integration:
 * - Reads lightOnTime, lightOffTime, lightMasterSwitch, and timeCycleEnabled
 * - Updates are handled by SystemManager::updateSystemData()
 * 
 * Time Format:
 * - Times are stored in 24-hour format (HH:MM)
 * - Converted to seconds since midnight for comparison
 */
class LightManager : public MutexManager {
private:
    // Light state tracking
    bool lightState;
    bool masterSwitch;
    bool timeCycleEnabled;
    time_t onTime;
    time_t offTime;
    bool initialized;
    
    // Error tracking
    uint8_t pinErrorCount;
    static constexpr uint8_t MAX_PIN_ERRORS = 3;
    
    // Fallback cycle tracking
    unsigned long lastLightStateChange;
    bool fallbackLightState;
    
    // Time management
    static constexpr uint32_t TIME_VALIDATION_INTERVAL = 3600000;  // 1 hour
    static constexpr uint8_t MAX_TIME_FAILURES = 3;  // Maximum consecutive time failures before alert
    
    unsigned long lastTimeValidation;
    uint8_t consecutiveTimeFailures;
    bool timeValid;
    
    /**
     * Safely writes to the light pin with error handling
     * @param state Whether to turn the light on or off
     * @return true if write successful
     */
    bool writeLightPin(bool state) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            Serial.println("Failed to take mutex in writeLightPin");
            return false;
        }
        
        bool success = true;
        if (SystemConfig::SYSTEM_LIGHTS_PIN != -1) {
            digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, state ? HIGH : LOW);
            lightState = state;
        } else {
            Serial.println("Light pin not configured");
            pinErrorCount++;
            success = false;
        }
        
        return success;
    }
    
    /**
     * Resets pin if too many errors occur
     */
    void resetPinIfNeeded() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "LightManager::resetPinIfNeeded"
            );
            return;
        }
        
        if (pinErrorCount >= MAX_PIN_ERRORS) {
            Serial.println("Too many pin errors, resetting light pin...");
            if (SystemConfig::SYSTEM_LIGHTS_PIN != -1) {
                pinMode(SystemConfig::SYSTEM_LIGHTS_PIN, OUTPUT);
                digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
            }
            pinErrorCount = 0;
        }
    }
    
    /**
     * Handles the fallback light cycle when offline
     * @return true if light should be on
     */
    bool handleFallbackCycle() {
        unsigned long currentMillis = millis();
        unsigned long elapsedTime;
        
        // Handle millis() overflow
        if (currentMillis < lastLightStateChange) {
            elapsedTime = (0xFFFFFFFF - lastLightStateChange) + currentMillis + 1;
        } else {
            elapsedTime = currentMillis - lastLightStateChange;
        }
        
        if (fallbackLightState) {
            // Check if we need to turn off
            if (elapsedTime >= DefaultValues::FALLBACK_LIGHT_ON_DURATION) {
                fallbackLightState = false;
                lastLightStateChange = currentMillis;
                return false;
            }
        } else {
            // Check if we need to turn on
            if (elapsedTime >= DefaultValues::FALLBACK_LIGHT_OFF_DURATION) {
                fallbackLightState = true;
                lastLightStateChange = currentMillis;
                return true;
            }
        }
        
        return fallbackLightState;
    }

public:
    /**
     * Constructor
     */
    LightManager() : lightState(false),
                    masterSwitch(false),
                    timeCycleEnabled(false),
                    onTime(0),
                    offTime(0),
                    initialized(false),
                    pinErrorCount(0),
                    lastLightStateChange(0),
                    fallbackLightState(false),
                    lastTimeValidation(0),
                    consecutiveTimeFailures(0),
                    timeValid(false) {
        // Initialize light pin
        pinMode(SystemConfig::SYSTEM_LIGHTS_PIN, OUTPUT);
        digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
    }
    
    /**
     * Destructor
     * Ensures proper cleanup of resources
     */
    ~LightManager() {
        ScopedLock lock(*this);
        if (lock.isLocked()) {
            // Turn off light before cleanup
            if (SystemConfig::SYSTEM_LIGHTS_PIN != -1) {
                digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
            }
        }
    }
    
    /**
     * Initializes the light manager
     * @return true if initialization successful
     */
    bool begin() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            Serial.println("Failed to take mutex in begin");
            return false;
        }
        
        bool success = true;
        if (SystemConfig::SYSTEM_LIGHTS_PIN != -1) {
            pinMode(SystemConfig::SYSTEM_LIGHTS_PIN, OUTPUT);
            digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
            lightState = false;
            fallbackLightState = false;
            lastLightStateChange = millis();
        } else {
            Serial.println("Light pin not configured");
            success = false;
        }
        
        if (success) {
            initialized = true;
        }
        
        return success;
    }
    
    /**
     * Updates the lighting control settings
     * @param master Manual control state
     * @param cycle Whether time-based control is enabled
     * @param on Time to turn lights on (seconds since midnight)
     * @param off Time to turn lights off (seconds since midnight)
     */
    void updateSettings(bool master, bool cycle, time_t on, time_t off) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            Serial.println("Failed to take mutex in updateSettings");
            return;
        }
        
        masterSwitch = master;
        timeCycleEnabled = cycle;
        onTime = on;
        offTime = off;
        
        // If switching to time cycle mode, reset fallback timing
        if (cycle) {
            lastLightStateChange = millis();
            fallbackLightState = false;
        }
    }
    
    /**
     * Updates the light state based on current settings
     * Handles both time-based and manual control modes
     */
    void update() {
        if (!initialized) {
            return;
        }
        
        // Get current state and settings with minimal mutex time
        bool currentLightState;
        bool currentMasterSwitch;
        bool currentTimeCycleEnabled;
        time_t currentOnTime;
        time_t currentOffTime;
        
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                Serial.println("Failed to take mutex in update");
                return;
            }
            
            currentLightState = lightState;
            currentMasterSwitch = masterSwitch;
            currentTimeCycleEnabled = timeCycleEnabled;
            currentOnTime = onTime;
            currentOffTime = offTime;
        }
        
        // Calculate new state outside of mutex
        bool newState = false;
        
        if (currentMasterSwitch) {
            // Manual control mode
            newState = true;
        } else {
            // Get current time of day outside of mutex
            time_t currentTime = getCurrentTimeOfDay();
            
            if (currentTime == 0) {
                // Time not set or invalid, use fallback cycle
                newState = handleFallbackCycle();
            } else if (currentTimeCycleEnabled) {
                // Time-based control mode with valid time
                if (currentOffTime < currentOnTime) {
                    newState = currentTime >= currentOnTime || currentTime < currentOffTime;
                } else {
                    newState = currentTime >= currentOnTime && currentTime < currentOffTime;
                }
            }
        }
        
        // Only update if state changed
        if (newState != currentLightState) {
            // Reset pin if needed with minimal mutex time
            {
                ScopedLock lock(*this);
                if (lock.isLocked()) {
                    resetPinIfNeeded();
                }
            }
            
            // Allow other tasks to run
            yield();
            
            // Write to pin with minimal mutex time
            writeLightPin(newState);
        }
    }
    
    /**
     * Gets the current light state
     * @return true if light is on
     */
    bool isLightOn() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        return lightState;
    }
    
    /**
     * Gets the current control mode
     * @return true if in manual control mode
     */
    bool isManualControl() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        return masterSwitch;
    }
    
    /**
     * Gets the current time cycle settings
     * @param onTime Reference to store on time
     * @param offTime Reference to store off time
     * @return true if time cycle is enabled
     */
    bool getTimeCycleSettings(time_t& onTime, time_t& offTime) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        onTime = this->onTime;
        offTime = this->offTime;
        return timeCycleEnabled;
    }

    /**
     * Sets the time validity state
     * @param valid Whether time is valid
     */
    void setTimeValid(bool valid) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            Serial.println("Failed to take mutex in setTimeValid");
            return;
        }
        
        timeValid = valid;
        if (valid) {
            consecutiveTimeFailures = 0;
        }
    }

    /**
     * Gets current time of day in seconds since midnight
     * Thread-safe: Yes
     * @return Current time of day in seconds
     */
    time_t getCurrentTimeOfDay() {
        // Get current time outside of mutex
        time_t currentTime;
        time(&currentTime);
        
        // Check if we need to validate time
        bool needValidation = false;
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                ErrorManager::mutexError(
                    ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                    "Failed to take mutex",
                    "LightManager::getCurrentTimeOfDay"
                );
                return 0;
            }
            
            needValidation = (millis() - lastTimeValidation >= TIME_VALIDATION_INTERVAL);
        }
        
        // Validate time if needed, outside of mutex
        if (needValidation) {
            struct tm timeinfo;
            bool timeValid = getLocalTime(&timeinfo);
            
            // Update validation status with minimal mutex time
            {
                ScopedLock lock(*this);
                if (lock.isLocked()) {
                    if (!timeValid) {
                        consecutiveTimeFailures++;
                        if (consecutiveTimeFailures >= MAX_TIME_FAILURES) {
                            this->timeValid = false;
                            Serial.println("CRITICAL ERROR: Persistent time synchronization failure");
                        }
                    } else {
                        consecutiveTimeFailures = 0;
                        this->timeValid = true;
                    }
                    lastTimeValidation = millis();
                }
            }
            
            // Allow other tasks to run
            yield();
        }
        
        // Check if time is valid with minimal mutex time
        bool isTimeValid;
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return 0;
            }
            isTimeValid = timeValid;
        }
        
        // If time is invalid, return 0 to prevent incorrect lighting control
        if (!isTimeValid) {
            return 0;
        }
        
        // Convert to seconds since midnight outside of mutex
        struct tm timeinfo;
        localtime_r(&currentTime, &timeinfo);
        time_t timeOfDay = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60;
        
        return timeOfDay;
    }
};

#endif // LIGHT_MANAGER_H 