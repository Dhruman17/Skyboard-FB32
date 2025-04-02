#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include "config.h"
#include "Wire.h"
#include "MCP3X21.h"
#include <Protocentral_FDC1004.h>

class HardwareManager {
private:
    MCP3021& mcp3021;
    FDC1004& fdc;
    
    void initializeI2C() {
        Wire.begin(SDA, SCL);
        mcp3021.init(&Wire);
    }
    
    void initializePins() {
        // Initialize water level pins
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            pinMode(SystemConfig::WATER_LEVEL_PINS[i], INPUT_PULLUP);
            ledcSetup(i, SystemConfig::PWM_FREQUENCY_ATOMIZER, SystemConfig::PWM_RESOLUTION_ATOMIZER);
            ledcAttachPin(SystemConfig::ATOMIZER_PINS[i], i);
        }
        
        // Initialize system pins
        pinMode(SystemConfig::SYSTEM_12V_POWER_PIN, OUTPUT);
        digitalWrite(SystemConfig::SYSTEM_12V_POWER_PIN, HIGH);
        pinMode(SystemConfig::SYSTEM_LIGHTS_PIN, OUTPUT);
        digitalWrite(SystemConfig::SYSTEM_LIGHTS_PIN, LOW);
    }

public:
    HardwareManager(MCP3021& mcp, FDC1004& fdc) 
        : mcp3021(mcp), fdc(fdc) {}
    
    void begin() {
        initializeI2C();
        initializePins();
    }
    
    // Getters for hardware components
    MCP3021& getMCP3021() { return mcp3021; }
    FDC1004& getFDC1004() { return fdc; }
};

#endif // HARDWARE_MANAGER_H 