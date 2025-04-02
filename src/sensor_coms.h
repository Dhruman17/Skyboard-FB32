#ifndef SENSOR_COMS_H
#define SENSOR_COMS_H

#include "config.h"
#include "hardware_manager.h"
#include "Wire.h"
#include "MCP3X21.h"
#include <Protocentral_FDC1004.h>

/**
 * SensorManager Class
 * 
 * Manages sensor communication and data collection:
 * 1. Sensor Initialization:
 *    - FDC1004 capacitive sensors
 *    - MCP3021 EC sensors
 *    - Sensor calibration
 * 
 * 2. Data Collection:
 *    - Water level readings
 *    - EC measurements
 *    - Sensor state monitoring
 * 
 * 3. Hardware Integration:
 *    - Two-level I2C multiplexing
 *    - Sensor channel selection
 *    - Error handling and recovery
 * 
 * Sensor Configuration:
 * - FDC1004: Channel 2 for water level
 * - MCP3021: Channel 3 for EC
 * - Readings averaged over 5 samples
 * - 100ms delay between readings
 */
class SensorManager {
private:
    HardwareManager& hardwareManager;
    FDC1004* fdc1004[SystemConfig::NUMBER_OF_UNITS];
    MCP3021* mcp3021[SystemConfig::NUMBER_OF_UNITS];
    bool initialized;
    SemaphoreHandle_t mutex;  // Add mutex for thread safety
    
    // Constants for sensor reading
    static constexpr uint8_t NUM_SAMPLES = 5;  // Number of samples to average
    static constexpr uint8_t READING_DELAY = 100;  // Delay between readings in ms
    static constexpr uint8_t MAX_RETRIES = 3;  // Maximum number of retries for sensor operations
    
    /**
     * Safely deletes a sensor instance
     * @param sensor Pointer to sensor instance
     */
    template<typename T>
    void safeDelete(T*& sensor) {
        if (sensor != nullptr) {
            delete sensor;
            sensor = nullptr;
        }
    }
    
    /**
     * Initializes FDC1004 sensor for a unit with retry mechanism
     * @param unitIndex Index of the unit
     * @return true if initialization successful
     */
    bool initializeFDC1004(int unitIndex) {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            Serial.println("Failed to take mutex in initializeFDC1004");
            return false;
        }
        
        bool success = false;
        uint8_t retries = 0;
        
        while (!success && retries < MAX_RETRIES) {
            hardwareManager.selectUnitAndSensor(unitIndex, SystemConfig::FDC1004_CHANNEL);
            
            // Clean up existing instance
            safeDelete(fdc1004[unitIndex]);
            
            // Create new instance with memory check
            fdc1004[unitIndex] = new (std::nothrow) FDC1004(SystemConfig::FDC1004_ADDR);
            if (fdc1004[unitIndex] == nullptr) {
                Serial.println("Failed to allocate memory for FDC1004");
                retries++;
                delay(100);
                continue;
            }
            
            // Configure FDC1004 with error checking
            if (!fdc1004[unitIndex]->configureMeasurementSingle(1, FDC1004_100HZ, 0)) {
                Serial.println("Failed to configure FDC1004");
                safeDelete(fdc1004[unitIndex]);
                retries++;
                delay(100);
                continue;
            }
            
            if (!fdc1004[unitIndex]->triggerSingleMeasurement(1, 0)) {
                Serial.println("Failed to trigger FDC1004 measurement");
                safeDelete(fdc1004[unitIndex]);
                retries++;
                delay(100);
                continue;
            }
            
            success = true;
        }
        
        xSemaphoreGive(mutex);
        return success;
    }
    
    /**
     * Initializes MCP3021 sensor for a unit with retry mechanism
     * @param unitIndex Index of the unit
     * @return true if initialization successful
     */
    bool initializeMCP3021(int unitIndex) {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            Serial.println("Failed to take mutex in initializeMCP3021");
            return false;
        }
        
        bool success = false;
        uint8_t retries = 0;
        
        while (!success && retries < MAX_RETRIES) {
            hardwareManager.selectUnitAndSensor(unitIndex, SystemConfig::MCP3021_CHANNEL);
            
            // Clean up existing instance
            safeDelete(mcp3021[unitIndex]);
            
            // Create new instance with memory check
            mcp3021[unitIndex] = new (std::nothrow) MCP3021();
            if (mcp3021[unitIndex] == nullptr) {
                Serial.println("Failed to allocate memory for MCP3021");
                retries++;
                delay(100);
                continue;
            }
            
            // Verify sensor is responding
            if (mcp3021[unitIndex]->read() < 0) {
                Serial.println("Failed to read from MCP3021");
                safeDelete(mcp3021[unitIndex]);
                retries++;
                delay(100);
                continue;
            }
            
            success = true;
        }
        
        xSemaphoreGive(mutex);
        return success;
    }
    
    /**
     * Reads multiple samples from FDC1004 and averages them with outlier detection
     * @param unitIndex Index of the unit
     * @return Average reading or -1 if error
     */
    float readFDC1004Averaged(int unitIndex) {
        if (!initialized || fdc1004[unitIndex] == nullptr) {
            return -1.0f;
        }
        
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            Serial.println("Failed to take mutex in readFDC1004Averaged");
            return -1.0f;
        }
        
        float readings[NUM_SAMPLES];
        uint8_t validReadings = 0;
        
        // Collect readings
        for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
            float reading = fdc1004[unitIndex]->readMeasurement(1, 0);
            if (reading >= 0) {
                readings[validReadings++] = reading;
            }
            delay(READING_DELAY);
        }
        
        if (validReadings == 0) {
            xSemaphoreGive(mutex);
            return -1.0f;
        }
        
        // Calculate mean and standard deviation
        float sum = 0.0f;
        for (uint8_t i = 0; i < validReadings; i++) {
            sum += readings[i];
        }
        float mean = sum / validReadings;
        
        float sumSquaredDiff = 0.0f;
        for (uint8_t i = 0; i < validReadings; i++) {
            float diff = readings[i] - mean;
            sumSquaredDiff += diff * diff;
        }
        float stdDev = sqrt(sumSquaredDiff / validReadings);
        
        // Remove outliers (values more than 2 standard deviations from mean)
        float filteredSum = 0.0f;
        uint8_t filteredCount = 0;
        for (uint8_t i = 0; i < validReadings; i++) {
            if (abs(readings[i] - mean) <= 2 * stdDev) {
                filteredSum += readings[i];
                filteredCount++;
            }
        }
        
        xSemaphoreGive(mutex);
        return filteredCount > 0 ? filteredSum / filteredCount : -1.0f;
    }
    
    /**
     * Reads multiple samples from MCP3021 and averages them with outlier detection
     * @param unitIndex Index of the unit
     * @return Average reading or -1 if error
     */
    float readMCP3021Averaged(int unitIndex) {
        if (!initialized || mcp3021[unitIndex] == nullptr) {
            return -1.0f;
        }
        
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            Serial.println("Failed to take mutex in readMCP3021Averaged");
            return -1.0f;
        }
        
        float readings[NUM_SAMPLES];
        uint8_t validReadings = 0;
        
        // Collect readings
        for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
            float reading = mcp3021[unitIndex]->read();
            if (reading >= 0) {
                readings[validReadings++] = reading;
            }
            delay(READING_DELAY);
        }
        
        if (validReadings == 0) {
            xSemaphoreGive(mutex);
            return -1.0f;
        }
        
        // Calculate mean and standard deviation
        float sum = 0.0f;
        for (uint8_t i = 0; i < validReadings; i++) {
            sum += readings[i];
        }
        float mean = sum / validReadings;
        
        float sumSquaredDiff = 0.0f;
        for (uint8_t i = 0; i < validReadings; i++) {
            float diff = readings[i] - mean;
            sumSquaredDiff += diff * diff;
        }
        float stdDev = sqrt(sumSquaredDiff / validReadings);
        
        // Remove outliers (values more than 2 standard deviations from mean)
        float filteredSum = 0.0f;
        uint8_t filteredCount = 0;
        for (uint8_t i = 0; i < validReadings; i++) {
            if (abs(readings[i] - mean) <= 2 * stdDev) {
                filteredSum += readings[i];
                filteredCount++;
            }
        }
        
        xSemaphoreGive(mutex);
        return filteredCount > 0 ? filteredSum / filteredCount : -1.0f;
    }

public:
    /**
     * Constructor
     * @param hardware Reference to hardware manager
     */
    SensorManager(HardwareManager& hardware) : hardwareManager(hardware), initialized(false) {
        // Initialize arrays to nullptr
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            fdc1004[i] = nullptr;
            mcp3021[i] = nullptr;
        }
        
        // Create mutex for thread safety
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("Failed to create mutex in SensorManager");
        }
    }
    
    /**
     * Destructor
     * Cleans up sensor objects
     */
    ~SensorManager() {
        cleanup();
        if (mutex != NULL) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Cleans up all sensor objects
     */
    void cleanup() {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            Serial.println("Failed to take mutex in cleanup");
            return;
        }
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            safeDelete(fdc1004[i]);
            safeDelete(mcp3021[i]);
        }
        initialized = false;
        
        xSemaphoreGive(mutex);
    }
    
    /**
     * Initializes all sensors
     * @return true if initialization successful
     */
    bool begin() {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            Serial.println("Failed to take mutex in begin");
            return false;
        }
        
        // Clean up any existing instances
        cleanup();
        
        // Initialize sensors for each unit
        bool success = true;
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!initializeMCP3021(i)) {
                Serial.println("Failed to initialize MCP3021 for unit " + String(i));
                success = false;
                break;
            }
            
            if (!initializeFDC1004(i)) {
                Serial.println("Failed to initialize FDC1004 for unit " + String(i));
                success = false;
                break;
            }
        }
        
        if (success) {
            initialized = true;
        } else {
            cleanup();
        }
        
        xSemaphoreGive(mutex);
        return success;
    }
    
    /**
     * Reads water level from FDC1004 sensor for a specific unit
     * @param unitIndex Index of the unit to read from
     * @return Water level reading (0-1)
     */
    float readWaterLevel(int unitIndex) {
        float rawValue = readFDC1004Averaged(unitIndex);
        if (rawValue < 0) {
            return -1.0f;
        }
        
        // Convert to water level (0-1)
        float waterLevel = (rawValue - SystemConfig::WATER_LEVEL_MIN) / 
                          (SystemConfig::WATER_LEVEL_MAX - SystemConfig::WATER_LEVEL_MIN);
        return constrain(waterLevel, 0.0f, 1.0f);
    }
    
    /**
     * Reads EC value from MCP3021 sensor for a specific unit
     * @param unitIndex Index of the unit to read from
     * @return EC value (0-100)
     */
    float readECValue(int unitIndex) {
        float rawValue = readMCP3021Averaged(unitIndex);
        if (rawValue < 0) {
            return -1.0f;
        }
        
        // Convert to EC value (0-100)
        float ecValue = (rawValue / 1024.0f) * SystemConfig::EC_MAX;
        return constrain(ecValue, 0.0f, 100.0f);
    }
    
    /**
     * Reads water levels for all units
     * @param levels Array to store water level readings
     * @return true if readings successful
     */
    bool readAllWaterLevels(float levels[SystemConfig::NUMBER_OF_UNITS]) {
        bool success = true;
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            levels[i] = readWaterLevel(i);
            if (levels[i] < 0) {
                success = false;
            }
        }
        
        return success;
    }
};

#endif // SENSOR_COMS_H