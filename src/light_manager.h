#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include "config.h"
#include <time.h>

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
    bool lightState = false;      // Current state of the light
    bool masterSwitch = false;    // Manual control state
    bool timeCycleEnabled = false; // Whether time-based control is active
    time_t onTime = 0;           // Time to turn lights on (seconds since midnight)
    time_t offTime = 0;          // Time to turn lights off (seconds since midnight)
    
    /**
     * Gets the current time of day in seconds since midnight
     * Used for comparing against on/off times
     */
    time_t getCurrentTimeOfDay() {
        time_t now;
        struct tm *currentTime;
        time(&now);
        currentTime = localtime(&now);
        
        struct tm currentTimeOfDay = *currentTime;
        currentTimeOfDay.tm_year = 70; // Epoch year
        currentTimeOfDay.tm_mon = 0;   // January
        currentTimeOfDay.tm_mday = 1;  // 1st of the month
        return mktime(&currentTimeOfDay);
    }

public:
    /**
     * Constructor
     * Initializes the light pin and sets it to OFF state
     */
    LightManager() {
        pinMode(SystemConfig::SYSTEM_LIGHTS_PIN, OUTPUT);
        digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
    }
    
    /**
     * Updates the lighting control settings
     * @param master Manual control state
     * @param cycle Whether time-based control is enabled
     * @param on Time to turn lights on (seconds since midnight)
     * @param off Time to turn lights off (seconds since midnight)
     */
    void updateSettings(bool master, bool cycle, time_t on, time_t off) {
        masterSwitch = master;
        timeCycleEnabled = cycle;
        onTime = on;
        offTime = off;
    }
    
    /**
     * Updates the light state based on current settings
     * Handles both time-based and manual control modes
     */
    void update() {
        if (timeCycleEnabled) {
            time_t currentTime = getCurrentTimeOfDay();
            
            if (offTime < onTime) { // Overnight cycle (e.g., 5pm off, 2am on)
                if (currentTime >= onTime || currentTime <= offTime) {
                    if (!lightState) {
                        digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, HIGH);
                        lightState = true;
                    }
                } else {
                    if (lightState) {
                        digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
                        lightState = false;
                    }
                }
            } else { // Same-day cycle (e.g., 9am to 5pm)
                if (currentTime >= onTime && currentTime <= offTime) {
                    if (!lightState) {
                        digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, HIGH);
                        lightState = true;
                    }
                } else {
                    if (lightState) {
                        digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
                        lightState = false;
                    }
                }
            }
        } else {
            if (lightState != masterSwitch) {
                if (masterSwitch) {
                    digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, HIGH);
                    lightState = true;
                } else {
                    digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
                    lightState = false;
                }
            }
        }
    }
};

#endif // LIGHT_MANAGER_H 