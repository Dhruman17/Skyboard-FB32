#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include "config.h"
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
 * 
 * 2. Manual control mode:
 *    - Lights controlled directly by masterSwitch
 *    - Overrides time-based schedule when timeCycleEnabled is false
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
     * Gets the current time of day in seconds since midnight
     * Used for comparing against on/off times
     * @return Current time of day in seconds since midnight
     */
    time_t getCurrentTimeOfDay() {
        time_t now;
        time(&now);
        if (now < 1000000000) { // If time is not set (before year 2000)
            Serial.println("Time not set, using fallback cycle");
            return 0;
        }
        
        struct tm *currentTime = localtime(&now);
        if (!currentTime) {
            Serial.println("Failed to get local time, using fallback cycle");
            return 0;
        }
        
        struct tm currentTimeOfDay = *currentTime;
        currentTimeOfDay.tm_year = 0;
        currentTimeOfDay.tm_mon = 0;
        currentTimeOfDay.tm_mday = 0;
        currentTimeOfDay.tm_hour = currentTime->tm_hour;
        currentTimeOfDay.tm_min = currentTime->tm_min;
        currentTimeOfDay.tm_sec = currentTime->tm_sec;
        
        return mktime(&currentTimeOfDay);
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
                    lastLightStateChange(0), fallbackLightState(false) {
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("Failed to create mutex in LightManager");
        }
    }
    
    /**
     * Destructor
     */
    ~LightManager() {
        if (mutex != NULL) {
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
};

#endif // LIGHT_MANAGER_H 