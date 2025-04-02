#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include <time.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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
class LightManager {
private:
    bool lightState;
    bool masterSwitch;
    bool timeCycleEnabled;
    time_t onTime;
    time_t offTime;
    bool initialized;
    SemaphoreHandle_t mutex;
    
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
     * Gets current time of day in seconds since midnight
     * Thread-safe: Yes
     * @return Current time of day in seconds
     */
    time_t getCurrentTimeOfDay() {
        if (!takeMutex()) {
            return 0;
        }
        
        time_t currentTime;
        time(&currentTime);
        
        // Validate time every hour
        if (millis() - lastTimeValidation >= TIME_VALIDATION_INTERVAL) {
            struct tm timeinfo;
            if (!getLocalTime(&timeinfo)) {
                consecutiveTimeFailures++;
                if (consecutiveTimeFailures >= MAX_TIME_FAILURES) {
                    timeValid = false;
                    Serial.println("CRITICAL ERROR: Persistent time synchronization failure");
                }
            } else {
                consecutiveTimeFailures = 0;
                timeValid = true;
            }
            lastTimeValidation = millis();
        }
        
        // If time is invalid, return 0 to prevent incorrect lighting control
        if (!timeValid) {
            giveMutex();
            return 0;
        }
        
        struct tm timeinfo;
        localtime_r(&currentTime, &timeinfo);
        time_t timeOfDay = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60;
        
        giveMutex();
        return timeOfDay;
    }
    
    /**
     * Safely writes to the light pin with error handling
     * @param state Whether to turn the light on or off
     * @return true if write successful
     */
    bool writeLightPin(bool state) {
        if (!takeMutex()) {
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
        
        giveMutex();
        return success;
    }
    
    /**
     * Resets pin if too many errors occur
     */
    void resetPinIfNeeded() {
        if (!takeMutex()) {
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
        
        giveMutex();
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
    LightManager() : lightState(false), masterSwitch(false),
                    timeCycleEnabled(false), onTime(0), offTime(0),
                    initialized(false), pinErrorCount(0),
                    lastLightStateChange(0), fallbackLightState(false),
                    mutex(NULL), lastTimeValidation(0), consecutiveTimeFailures(0),
                    timeValid(false) {
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("CRITICAL ERROR: Failed to create mutex in LightManager");
            // Consider implementing a fallback mechanism or system reset here
        }
    }
    
    /**
     * Destructor
     * Ensures proper cleanup of resources
     */
    ~LightManager() {
        if (mutex != NULL) {
            // Ensure we're not holding the mutex before deleting
            if (xSemaphoreGetMutexHolder(mutex) == xTaskGetCurrentTaskHandle()) {
                xSemaphoreGive(mutex);
            }
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Initializes the light manager
     * @return true if initialization successful
     */
    bool begin() {
        if (!takeMutex()) {
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
        
        giveMutex();
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
        if (!takeMutex()) {
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
        
        giveMutex();
    }
    
    /**
     * Updates the light state based on current settings
     * Handles both time-based and manual control modes
     */
    void update() {
        if (!initialized) {
            return;
        }
        
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in update");
            return;
        }
        
        bool newState = false;
        
        if (masterSwitch) {
            // Manual control mode
            newState = true;
        } else {
            // Check if we have valid time
            time_t currentTime = getCurrentTimeOfDay();
            
            if (currentTime == 0) {
                // Time not set or invalid, use fallback cycle
                newState = handleFallbackCycle();
            } else if (timeCycleEnabled) {
                // Time-based control mode with valid time
                if (offTime < onTime) {
                    newState = currentTime >= onTime || currentTime < offTime;
                } else {
                    newState = currentTime >= onTime && currentTime < offTime;
                }
            }
        }
        
        // Only update if state changed
        if (newState != lightState) {
            resetPinIfNeeded();
            writeLightPin(newState);
        }
        
        giveMutex();
    }
    
    /**
     * Gets the current light state
     * @return true if light is on
     */
    bool isLightOn() {
        if (!takeMutex()) {
            return false;
        }
        
        bool state = lightState;
        giveMutex();
        return state;
    }
    
    /**
     * Gets the current control mode
     * @return true if in manual control mode
     */
    bool isManualControl() {
        if (!takeMutex()) {
            return false;
        }
        
        bool manual = masterSwitch;
        giveMutex();
        return manual;
    }
    
    /**
     * Gets the current time cycle settings
     * @param onTime Reference to store on time
     * @param offTime Reference to store off time
     * @return true if time cycle is enabled
     */
    bool getTimeCycleSettings(time_t& onTime, time_t& offTime) {
        if (!takeMutex()) {
            return false;
        }
        
        onTime = this->onTime;
        offTime = this->offTime;
        bool enabled = timeCycleEnabled;
        
        giveMutex();
        return enabled;
    }

    /**
     * Sets the time validity state
     * @param valid Whether time is valid
     */
    void setTimeValid(bool valid) {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in setTimeValid");
            return;
        }
        
        timeValid = valid;
        if (valid) {
            consecutiveTimeFailures = 0;
        }
        
        giveMutex();
    }
};

#endif // LIGHT_MANAGER_H 