#ifndef CAPACITIVE_WATER_LEVEL_SENSOR_H
#define CAPACITIVE_WATER_LEVEL_SENSOR_H

#include "water_level_sensor.h"
#include "config.h"
#include "error_manager.h"

/**
 * CapacitiveWaterLevelSensor Class
 * 
 * Concrete implementation of WaterLevelSensor using capacitive sensing
 */
class CapacitiveWaterLevelSensor : public WaterLevelSensor {
private:
    uint8_t unitIndex;
    bool initialized;
    
public:
    explicit CapacitiveWaterLevelSensor(uint8_t index) : unitIndex(index), initialized(false) {}
    
    bool initialize() override {
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_INIT_FAILED,
                "Invalid unit index",
                "CapacitiveWaterLevelSensor::initialize"
            );
            return false;
        }
        
        // Initialize the sensor hardware
        pinMode(SystemConfig::WATER_LEVEL_PINS[unitIndex], INPUT);
        initialized = true;
        return true;
    }
    
    float readWaterLevel() override {
        if (!initialized) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_READ_FAILED,
                "Sensor not initialized",
                "CapacitiveWaterLevelSensor::readWaterLevel"
            );
            return -1.0f;
        }
        
        // Read the sensor value
        int rawValue = analogRead(SystemConfig::WATER_LEVEL_PINS[unitIndex]);
        
        // Convert to percentage (0-100)
        float percentage = (rawValue / 4095.0f) * 100.0f;
        
        return percentage;
    }
    
    bool isInitialized() const override {
        return initialized;
    }
};

#endif // CAPACITIVE_WATER_LEVEL_SENSOR_H 