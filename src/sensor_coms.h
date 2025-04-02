#ifndef SENSOR_COMS
#define SENSOR_COMS
#include "Wire.h"
#include "config.h"
#include "Protocentral_FDC1004.h"

struct WaterLevelReading {
    bool success;
    float capacitance;
    float waterLevel;
    String error;
};

class SensorManager {
private:
    FDC1004& fdc;
    int capdac;
    
    WaterLevelReading readWaterLevelForUnit(int unitIndex) {
        WaterLevelReading result = {false, 0.0f, 0.0f, ""};
        
        // Select the appropriate TCA channel
        tcaselect(unitIndex * 2);
        
        // Configure and trigger measurement
        if (!fdc.configureMeasurementSingle(SystemConfig::MEASURMENT, SystemConfig::CHANNEL, capdac)) {
            result.error = "Failed to configure measurement";
            return result;
        }
        
        if (!fdc.triggerSingleMeasurement(SystemConfig::MEASURMENT, FDC1004_100HZ)) {
            result.error = "Failed to trigger measurement";
            return result;
        }
        
        // Wait for completion
        delay(SystemConfig::WATER_LEVEL_READ_INTERVAL);
        
        // Read measurement
        uint16_t value[2];
        if (!fdc.readMeasurement(SystemConfig::MEASURMENT, value)) {
            int16_t msb = (int16_t)value[0];
            
            // Calculate capacitance
            int32_t capacitance = ((int32_t)457) * ((int32_t)msb); // in attofarads
            capacitance /= 1000;                                   // in femtofarads
            capacitance += ((int32_t)3028) * ((int32_t)capdac);
            result.capacitance = (float)capacitance / 1000; // in pF
            
            // Calculate water level
            result.waterLevel = (result.capacitance - SystemConfig::WATER_LEVEL_CALIBRATION_OFFSET) / 
                               SystemConfig::WATER_LEVEL_CALIBRATION_FACTOR;
            
            // Adjust capdac if needed
            if (msb > SystemConfig::UPPER_BOUND) {
                if (capdac < FDC1004_CAPDAC_MAX) capdac++;
            } else if (msb < SystemConfig::LOWER_BOUND) {
                if (capdac > 0) capdac--;
            }
            
            result.success = true;
        } else {
            result.error = "Failed to read measurement";
        }
        
        return result;
    }

public:
    SensorManager(FDC1004& fdcSensor) : fdc(fdcSensor), capdac(0) {}
    
    bool readAllWaterLevels(float waterLevels[SystemConfig::NUMBER_OF_UNITS]) {
        bool allSuccess = true;
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            WaterLevelReading reading = readWaterLevelForUnit(i);
            
            if (reading.success) {
                waterLevels[i] = reading.waterLevel;
            } else {
                Serial.printf("Failed to read water level for unit %d: %s\n", i, reading.error.c_str());
                allSuccess = false;
            }
        }
        
        return allSuccess;
    }
};

// Legacy function for backward compatibility
void tcaselect(uint8_t i) {
    if (i > 7) return;
    Wire.beginTransmission(SystemConfig::TCAADDR);
    Wire.write(1 << i);
    Wire.endTransmission();
}

namespace device {
    float aref = 3.3; // Vref, this is for 3.3v compatible controller boards
}

namespace sensor {
    float ec = 0;
    unsigned int tds = 0;
    float ecCalibration = 1;
}

#endif // SENSOR_COMS