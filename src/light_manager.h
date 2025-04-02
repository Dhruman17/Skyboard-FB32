#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include "config.h"
#include <time.h>

class LightManager {
private:
    bool lightState = false;
    bool masterSwitch = false;
    bool timeCycleEnabled = false;
    time_t onTime = 0;
    time_t offTime = 0;
    
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
    void updateSettings(bool master, bool cycle, time_t on, time_t off) {
        masterSwitch = master;
        timeCycleEnabled = cycle;
        onTime = on;
        offTime = off;
    }
    
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