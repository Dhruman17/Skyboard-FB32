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
    // RAII-style mutex lock helper
    class ScopedLock {
    private:
        SemaphoreHandle_t& mutex;
        bool locked;

    public:
        ScopedLock(SemaphoreHandle_t& m) : mutex(m), locked(false) {
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(SystemConfig::MUTEX_TIMEOUT_MS)) == pdTRUE) {
                locked = true;
            }
        }

        ~ScopedLock() {
            if (locked) {
                xSemaphoreGive(mutex);
            }
        }

        bool isLocked() const { return locked; }
    };

    HardwareManager& hardwareManager;
    FDC1004* fdc1004[SystemConfig::NUMBER_OF_UNITS];
    MCP3021* mcp3021[SystemConfig::NUMBER_OF_UNITS];
    bool initialized;
    SemaphoreHandle_t mutex;  // Add mutex for thread safety
    
    // Pre-allocated error message strings to prevent fragmentation
    static constexpr size_t ERROR_MSG_MAX_LENGTH = 128;
    String errorMessages[SystemConfig::NUMBER_OF_UNITS];
    
    // Constants for sensor reading
    static constexpr uint8_t NUM_SAMPLES = 5;  // Number of samples to average
    static constexpr uint8_t READING_DELAY = 100;  // Delay between readings in ms
    static constexpr uint8_t MAX_RETRIES = 3;  // Maximum number of retries for sensor operations

private:
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
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "initializeFDC1004");
            return false;
        }
        
        bool success = false;
        uint8_t retries = 0;
        
        // Clean up existing instance first
        safeDelete(fdc1004[unitIndex]);
        
        while (!success && retries < SystemConfig::MAX_SENSOR_RETRIES) {
            hardwareManager.selectUnitAndSensor(unitIndex, SystemConfig::FDC1004_CHANNEL);
            
            // Create new instance with memory check
            fdc1004[unitIndex] = new (std::nothrow) FDC1004(SystemConfig::FDC1004_ADDR);
            if (fdc1004[unitIndex] == nullptr) {
                Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "initializeFDC1004", unitIndex, "Failed to allocate memory");
                retries++;
                if (retries < SystemConfig::MAX_SENSOR_RETRIES) {
                    delay(SystemConfig::SENSOR_RETRY_DELAY_MS);
                }
                continue;
            }
            
            // Configure FDC1004 with error checking - fixed parameter types
            uint8_t measurement = 0;  // Use measurement 0
            uint8_t channel = static_cast<uint8_t>(SystemConfig::FDC1004_CHANNEL);
            uint8_t capdac = 0;  // Start with 0 capacitance offset
            if (!fdc1004[unitIndex]->configureMeasurementSingle(measurement, channel, capdac)) {
                Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "initializeFDC1004", unitIndex, "Failed to configure");
                safeDelete(fdc1004[unitIndex]);
                retries++;
                if (retries < SystemConfig::MAX_SENSOR_RETRIES) {
                    delay(SystemConfig::SENSOR_RETRY_DELAY_MS);
                }
                continue;
            }
            
            if (!fdc1004[unitIndex]->triggerSingleMeasurement(SystemConfig::FDC1004_CHANNEL, 0x00)) {
                Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "initializeFDC1004", unitIndex, "Failed to trigger measurement");
                safeDelete(fdc1004[unitIndex]);
                retries++;
                if (retries < SystemConfig::MAX_SENSOR_RETRIES) {
                    delay(SystemConfig::SENSOR_RETRY_DELAY_MS);
                }
                continue;
            }
            
            success = true;
        }
        
        if (!success) {
            Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "initializeFDC1004", unitIndex, 
                         "Failed to initialize after " + String(SystemConfig::MAX_SENSOR_RETRIES) + " attempts");
            safeDelete(fdc1004[unitIndex]);
        }
        
        return success;
    }
    
    /**
     * Initializes MCP3021 sensor for a unit with retry mechanism
     * @param unitIndex Index of the unit
     * @return true if initialization successful
     */
    bool initializeMCP3021(int unitIndex) {
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "initializeMCP3021");
            return false;
        }
        
        bool success = false;
        uint8_t retries = 0;
        
        // Clean up existing instance first
        safeDelete(mcp3021[unitIndex]);
        
        while (!success && retries < SystemConfig::MAX_SENSOR_RETRIES) {
            hardwareManager.selectUnitAndSensor(unitIndex, SystemConfig::MCP3021_CHANNEL);
            
            // Create new instance with memory check
            mcp3021[unitIndex] = new (std::nothrow) MCP3021();
            if (mcp3021[unitIndex] == nullptr) {
                Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "initializeMCP3021", unitIndex, "Failed to allocate memory");
                retries++;
                if (retries < SystemConfig::MAX_SENSOR_RETRIES) {
                    delay(SystemConfig::SENSOR_RETRY_DELAY_MS);
                }
                continue;
            }
            
            // Verify sensor is responding
            if (mcp3021[unitIndex]->read() < 0) {
                Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "initializeMCP3021", unitIndex, "Failed to read from sensor");
                safeDelete(mcp3021[unitIndex]);
                retries++;
                if (retries < SystemConfig::MAX_SENSOR_RETRIES) {
                    delay(SystemConfig::SENSOR_RETRY_DELAY_MS);
                }
                continue;
            }
            
            success = true;
        }
        
        if (!success) {
            Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "initializeMCP3021", unitIndex, 
                         "Failed to initialize after " + String(SystemConfig::MAX_SENSOR_RETRIES) + " attempts");
            safeDelete(mcp3021[unitIndex]);
        }
        
        return success;
    }
    
    /**
     * Helper function to detect and remove outliers from sensor readings
     * @param readings Array of readings
     * @param count Number of valid readings
     * @param mean Reference to store the mean
     * @param stdDev Reference to store the standard deviation
     * @param filteredSum Reference to store the sum of filtered readings
     * @param filteredCount Reference to store the count of filtered readings
     */
    void detectOutliers(const float* readings, uint8_t count, float& mean, float& stdDev, 
                       float& filteredSum, uint8_t& filteredCount) {
        if (!readings || count == 0) {
            mean = 0.0f;
            stdDev = 0.0f;
            filteredSum = 0.0f;
            filteredCount = 0;
            return;
        }
        
        // Calculate mean
        float sum = 0.0f;
        for (uint8_t i = 0; i < count; i++) {
            sum += readings[i];
        }
        mean = sum / count;
        
        // Calculate standard deviation
        float sumSquaredDiff = 0.0f;
        for (uint8_t i = 0; i < count; i++) {
            float diff = readings[i] - mean;
            sumSquaredDiff += diff * diff;
        }
        stdDev = sqrt(sumSquaredDiff / count);
        
        // Remove outliers (values more than 2 standard deviations from mean)
        filteredSum = 0.0f;
        filteredCount = 0;
        for (uint8_t i = 0; i < count; i++) {
            if (abs(readings[i] - mean) <= 2 * stdDev) {
                filteredSum += readings[i];
                filteredCount++;
            }
        }
    }
    
    /**
     * Validates sensor parameters
     * @param unitIndex Index of the unit
     * @param sensorType Type of sensor
     * @return true if parameters are valid
     */
    bool validateSensorParams(int unitIndex, const char* sensorType) {
        if (unitIndex < 0 || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "validateSensorParams", unitIndex, "Invalid unit index");
            return false;
        }
        
        if (!sensorType) {
            Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "validateSensorParams", unitIndex, "Invalid sensor type");
            return false;
        }
        
        if (strcmp(sensorType, SystemConfig::SENSOR_TYPE_WATER) != 0 && 
            strcmp(sensorType, SystemConfig::SENSOR_TYPE_EC) != 0) {
            Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "validateSensorParams", unitIndex, "Unknown sensor type");
            return false;
        }
        
        return true;
    }
    
    /**
     * Reads multiple samples from FDC1004 and averages them with outlier detection
     * Thread-safe: Yes (uses mutex)
     * Performance: Minimizes mutex locking time
     * @param unitIndex Index of the unit
     * @return Average reading or -1 if error
     */
    float readFDC1004Averaged(int unitIndex) {
        if (!initialized || fdc1004[unitIndex] == nullptr) {
            return -1.0f;
        }
        
        // Use unique_ptr for automatic cleanup
        std::unique_ptr<float[]> readings(new (std::nothrow) float[NUM_SAMPLES]);
        if (!readings) {
            Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "readFDC1004Averaged", unitIndex, "Failed to allocate memory for readings");
            return -1.0f;
        }
        
        uint8_t validReadings = 0;
        
        // Use ScopedLock for mutex management
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "readFDC1004Averaged");
            return -1.0f;
        }
        
        // Collect readings
        for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
            float reading = fdc1004[unitIndex]->readMeasurement(SystemConfig::FDC1004_CHANNEL, 0);
            if (reading >= 0) {
                readings[validReadings++] = reading;
            }
            delay(READING_DELAY);
        }
        
        float result = -1.0f;
        if (validReadings > 0) {
            float mean, stdDev, filteredSum;
            uint8_t filteredCount;
            detectOutliers(readings.get(), validReadings, mean, stdDev, filteredSum, filteredCount);
            result = filteredCount > 0 ? filteredSum / filteredCount : -1.0f;
        }
        
        return result;
    }
    
    /**
     * Reads multiple samples from MCP3021 and averages them with outlier detection
     * Thread-safe: Yes (uses mutex)
     * Performance: Minimizes mutex locking time
     * @param unitIndex Index of the unit
     * @return Average reading or -1 if error
     */
    float readMCP3021Averaged(int unitIndex) {
        if (!initialized || mcp3021[unitIndex] == nullptr) {
            return -1.0f;
        }
        
        // Use unique_ptr for automatic cleanup
        std::unique_ptr<float[]> readings(new (std::nothrow) float[NUM_SAMPLES]);
        if (!readings) {
            Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "readMCP3021Averaged", unitIndex, "Failed to allocate memory for readings");
            return -1.0f;
        }
        
        uint8_t validReadings = 0;
        
        // Use ScopedLock for mutex management
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "readMCP3021Averaged");
            return -1.0f;
        }
        
        // Collect readings
        for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
            float reading = mcp3021[unitIndex]->read();
            if (reading >= 0) {
                readings[validReadings++] = reading;
            }
            delay(READING_DELAY);
        }
        
        float result = -1.0f;
        if (validReadings > 0) {
            float mean, stdDev, filteredSum;
            uint8_t filteredCount;
            detectOutliers(readings.get(), validReadings, mean, stdDev, filteredSum, filteredCount);
            result = filteredCount > 0 ? filteredSum / filteredCount : -1.0f;
        }
        
        return result;
    }

    /**
     * Initializes error message strings with pre-allocated space
     */
    void initializeErrorMessages() {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            errorMessages[i].reserve(ERROR_MSG_MAX_LENGTH);
        }
    }
    
    /**
     * Cleans up error message strings
     */
    void cleanupErrorMessages() {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            errorMessages[i] = "";
        }
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
        
        // Initialize error message strings
        initializeErrorMessages();
    }
    
    /**
     * Destructor
     * Cleans up sensor objects and error messages
     */
    ~SensorManager() {
        cleanup();
        cleanupErrorMessages();
        if (mutex != NULL) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Cleans up all sensor objects
     * Thread-safe: Yes (uses mutex)
     * Memory Management: Properly deletes all sensor instances
     */
    void cleanup() {
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "cleanup");
            return;
        }
        
        // Clean up all sensor instances
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            safeDelete(fdc1004[i]);
            safeDelete(mcp3021[i]);
        }
        
        initialized = false;
    }
    
    /**
     * Cleans up a specific sensor for a unit
     * @param unitIndex Index of the unit
     * @param sensorType Type of sensor to clean up
     * @return true if cleanup was successful
     */
    bool cleanupSensor(int unitIndex, const char* sensorType) {
        if (!validateSensorParams(unitIndex, sensorType)) {
            return false;
        }
        
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "cleanupSensor");
            return false;
        }
        
        bool success = false;
        if (strcmp(sensorType, SystemConfig::SENSOR_TYPE_WATER) == 0) {
            // Reset FDC1004
            success = initializeFDC1004(unitIndex);
        } else if (strcmp(sensorType, SystemConfig::SENSOR_TYPE_EC) == 0) {
            success = initializeMCP3021(unitIndex);
        }
        
        return success;
    }
    
    /**
     * Initializes all sensors
     * @return true if initialization successful
     */
    bool begin() {
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "begin");
            return false;
        }
        
        // Clean up any existing instances
        cleanup();
        
        // Initialize sensors for each unit
        bool success = true;
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!initializeMCP3021(i)) {
                Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "begin", i, "Failed to initialize MCP3021");
                success = false;
                break;
            }
            
            if (!initializeFDC1004(i)) {
                Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "begin", i, "Failed to initialize FDC1004");
                success = false;
                break;
            }
        }
        
        if (success) {
            initialized = true;
        } else {
            cleanup();
        }
        
        return success;
    }
    
    /**
     * Sets an error message with length protection
     * @param unitIndex Index of the unit
     * @param message Error message to set
     */
    void setErrorMessage(int unitIndex, const char* message) {
        if (unitIndex < 0 || unitIndex >= SystemConfig::NUMBER_OF_UNITS || !message || strlen(message) == 0) {
            return;
        }
        
        // Truncate message if it exceeds max length
        String truncatedMessage = String(message);
        if (truncatedMessage.length() > ERROR_MSG_MAX_LENGTH) {
            truncatedMessage = truncatedMessage.substring(0, ERROR_MSG_MAX_LENGTH - 3) + "...";
        }
        errorMessages[unitIndex] = truncatedMessage;
    }
    
    /**
     * Handles critical system failures by attempting recovery
     * @param errorType Type of error that occurred
     * @return true if recovery was successful
     */
    bool handleCriticalFailure(const char* errorType) {
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "handleCriticalFailure");
            return false;
        }
        
        bool success = false;
        uint8_t retries = 0;
        
        while (!success && retries < SystemConfig::MAX_SENSOR_RETRIES) {
            Serial.printf("Attempting system recovery from %s failure (attempt %d/%d)\n", 
                         errorType, retries + 1, SystemConfig::MAX_SENSOR_RETRIES);
            
            // Clean up all resources
            cleanup();
            cleanupErrorMessages();
            
            // Reinitialize hardware
            if (!hardwareManager.begin()) {
                Serial.println("Failed to reinitialize hardware");
                retries++;
                delay(SystemConfig::SENSOR_RETRY_DELAY_MS);
                continue;
            }
            
            // Reinitialize sensors
            if (!begin()) {
                Serial.println("Failed to reinitialize sensors");
                retries++;
                delay(SystemConfig::SENSOR_RETRY_DELAY_MS);
                continue;
            }
            
            success = true;
        }
        
        if (!success) {
            Serial.printf("System recovery failed after %d attempts\n", SystemConfig::MAX_SENSOR_RETRIES);
            // At this point, we might want to trigger a system restart
            // Release mutex before restart to prevent deadlock
            ESP.restart();
        }
        
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
            setErrorMessage(unitIndex, "Failed to read water level");
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
            setErrorMessage(unitIndex, "Failed to read EC value");
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
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "readAllWaterLevels");
            return false;
        }
        
        bool success = true;
        uint8_t failureCount = 0;
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            levels[i] = readWaterLevel(i);
            if (levels[i] < 0) {
                success = false;
                failureCount++;
            }
        }
        
        // If too many failures, attempt system recovery
        if (failureCount >= SystemConfig::NUMBER_OF_UNITS / 2) {
            // Create a new ScopedLock for recovery
            ScopedLock recoveryLock(mutex);
            if (recoveryLock.isLocked()) {
                handleCriticalFailure("sensor reading");
            }
        }
        
        return success;
    }

    /**
     * Gets the hardware manager reference
     * @return Reference to hardware manager
     */
    HardwareManager& getHardwareManager() {
        return hardwareManager;
    }
};

#endif // SENSOR_COMS_H