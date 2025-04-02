#ifndef WATER_LEVEL_SENSOR_H
#define WATER_LEVEL_SENSOR_H

#include "config.h"
#include "hardware_manager.h"
#include <Protocentral_FDC1004.h>
#include "MCP3X21.h"

/**
 * Abstract Water Level Sensor Interface
 * Provides a common interface for different water level sensor implementations.
 * All implementations must provide:
 * - Initialization
 * - Water level reading (0-1 range)
 * - Initialization status check
 */
class WaterLevelSensor {
public:
    virtual ~WaterLevelSensor() = default;
    virtual bool initialize() = 0;
    virtual float readWaterLevel() = 0;
    virtual bool isInitialized() const = 0;
};

/**
 * FDC1004 Capacitive Sensor Implementation
 * Provides continuous water level measurements using the FDC1004 capacitive sensor.
 * Features:
 * - Multiple sample averaging for stability
 * - Linear scaling to 0-1 range
 * - Automatic error detection
 */
class CapacitiveWaterSensor : public WaterLevelSensor {
private:
    FDC1004* sensor;
    HardwareManager& hardwareManager;
    bool initialized;
    static constexpr uint8_t NUM_SAMPLES = 5;    // Number of samples to average
    static constexpr uint8_t READING_DELAY = 100; // Delay between readings in ms

public:
    CapacitiveWaterSensor(HardwareManager& hw) : hardwareManager(hw), initialized(false), sensor(nullptr) {}
    
    bool initialize() override {
        if (initialized) return true;
        
        sensor = new FDC1004(SystemConfig::FDC1004_ADDR);
        
        for (int i = 0; i < SystemConfig::MAX_SENSOR_RETRIES; i++) {
            if (sensor->configureMeasurementSingle(0, SystemConfig::FDC1004_CHANNEL, 0)) {
                initialized = true;
                return true;
            }
            delay(SystemConfig::SENSOR_RETRY_DELAY_MS);
        }
        
        return false;
    }
    
    float readWaterLevel() override {
        if (!initialized || !sensor) return -1.0f;
        
        float readings[NUM_SAMPLES];
        uint8_t validReadings = 0;
        
        for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
            float reading = sensor->readMeasurement(SystemConfig::FDC1004_CHANNEL, 0);
            if (reading >= 0) {
                readings[validReadings++] = reading;
            }
            delay(READING_DELAY);
        }
        
        if (validReadings == 0) return -1.0f;
        
        // Calculate average
        float sum = 0.0f;
        for (uint8_t i = 0; i < validReadings; i++) {
            sum += readings[i];
        }
        float average = sum / validReadings;
        
        // Convert to water level (0-1)
        float waterLevel = (average - SystemConfig::WATER_LEVEL_MIN) / 
                          (SystemConfig::WATER_LEVEL_MAX - SystemConfig::WATER_LEVEL_MIN);
        return constrain(waterLevel, 0.0f, 1.0f);
    }
    
    bool isInitialized() const override { return initialized; }
    
    ~CapacitiveWaterSensor() {
        if (sensor) {
            delete sensor;
            sensor = nullptr;
        }
    }
};

/**
 * Float Switch Implementation using MCP3021 ADC
 * Provides binary water level detection using a float switch connected to an ADC.
 * 
 * Operation:
 * 1. Takes multiple ADC readings to confirm float switch state
 * 2. Counts readings above HIGH_THRESHOLD and below LOW_THRESHOLD
 * 3. Requires MIN_CONSISTENT_READINGS to confirm state
 * 4. Returns:
 *    - 1.0f when water level is confirmed full
 *    - 0.0f when water level is confirmed empty
 *    - -1.0f when state is uncertain
 * 
 * Configuration:
 * - FLOAT_SWITCH_HIGH_THRESHOLD: ADC value indicating full water level
 * - FLOAT_SWITCH_LOW_THRESHOLD: ADC value indicating empty water level
 * - NUM_SAMPLES: Total number of readings to take
 * - MIN_CONSISTENT_READINGS: Minimum consistent readings to confirm state
 */
class FloatSwitchSensor : public WaterLevelSensor {
private:
    MCP3021* adc;
    HardwareManager& hardwareManager;
    bool initialized;
    static constexpr uint8_t NUM_SAMPLES = 5;    // Number of samples to confirm state
    static constexpr uint8_t READING_DELAY = 100; // Delay between readings in ms
    static constexpr uint8_t MIN_CONSISTENT_READINGS = 3;  // Minimum consistent readings to confirm state

public:
    FloatSwitchSensor(HardwareManager& hw) : hardwareManager(hw), initialized(false), adc(nullptr) {}
    
    bool initialize() override {
        if (initialized) return true;
        
        adc = new MCP3021(SystemConfig::MCP3021_ADDR);
        
        // Test reading to verify initialization
        int testReading = adc->read();
        if (testReading >= 0) {
            initialized = true;
            return true;
        }
        
        return false;
    }
    
    float readWaterLevel() override {
        if (!initialized || !adc) return -1.0f;
        
        // Take multiple readings to confirm state
        uint8_t highReadings = 0;
        uint8_t lowReadings = 0;
        
        for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
            float reading = adc->read();
            if (reading >= 0) {
                if (reading >= SystemConfig::FLOAT_SWITCH_HIGH_THRESHOLD) {
                    highReadings++;
                } else if (reading <= SystemConfig::FLOAT_SWITCH_LOW_THRESHOLD) {
                    lowReadings++;
                }
                // Readings between thresholds are ignored as they indicate an invalid state
            }
            delay(READING_DELAY);
        }
        
        // Check if we have enough consistent readings
        if (highReadings >= MIN_CONSISTENT_READINGS) {
            return 1.0f;  // Water level is full
        } else if (lowReadings >= MIN_CONSISTENT_READINGS) {
            return 0.0f;  // Water level is empty
        }
        
        // If we don't have enough consistent readings, return error
        return -1.0f;
    }
    
    bool isInitialized() const override { return initialized; }
    
    ~FloatSwitchSensor() {
        if (adc) {
            delete adc;
            adc = nullptr;
        }
    }
};

#endif // WATER_LEVEL_SENSOR_H 