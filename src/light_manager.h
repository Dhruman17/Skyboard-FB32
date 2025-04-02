#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include <time.h>
#include "config.h"

class LightManager {
private:
    bool lightState;
    bool lightMasterSwitch;
    bool timeCycleEnabled;
    time_t lightOnTime;
    time_t lightOffTime;
    
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
    LightManager() : lightState(false), lightMasterSwitch(false), timeCycleEnabled(false) {
        pinMode(SystemConfig::SYSTEM_LIGHTS_PIN, OUTPUT);
        digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
    }
    
    void updateSettings(bool masterSwitch, bool cycleEnabled, time_t onTime, time_t offTime) {
        lightMasterSwitch = masterSwitch;
        timeCycleEnabled = cycleEnabled;
        lightOnTime = onTime;
        lightOffTime = offTime;
    }
    
    void update() {
        if (timeCycleEnabled) {
            time_t currentTime = getCurrentTimeOfDay();
            
            if (lightOffTime < lightOnTime) {
                // Handle overnight cycle (e.g., 5pm off, 2am on)
                if (currentTime >= lightOnTime || currentTime <= lightOffTime) {
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
            } else {
                // Handle same-day cycle (e.g., 9am to 5pm)
                if (currentTime >= lightOnTime && currentTime <= lightOffTime) {
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
            if (lightState != lightMasterSwitch) {
                if (lightMasterSwitch) {
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