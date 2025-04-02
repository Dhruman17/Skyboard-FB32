#ifndef SENSOR_COMS_H
#define SENSOR_COMS_H

#include "config.h"
#include "hardware_manager.h"
#include "water_level_sensor.h"
#include "Wire.h"
#include "MCP3X21.h"
#include "error_manager.h"

/**
 * SensorManager Class
 * 
 * Manages sensor communication and data collection:
 * 1. Sensor Initialization:
 *    - Water level sensors (capacitive or float switch)
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
    // Mutex for thread safety
    SemaphoreHandle_t mutex;
    
    // Hardware manager reference
    HardwareManager& hardwareManager;
    
    // Sensor instances
    WaterLevelSensor* waterSensors[SystemConfig::NUMBER_OF_UNITS];
    MCP3021* mcp3021[SystemConfig::NUMBER_OF_UNITS];
    
    // Error tracking
    uint8_t errorCount[SystemConfig::NUMBER_OF_UNITS];
    static constexpr uint8_t MAX_ERRORS = 3;
    
    // Pre-allocated error message strings to prevent fragmentation
    static constexpr size_t ERROR_MSG_MAX_LENGTH = 128;
    String errorMessages[SystemConfig::NUMBER_OF_UNITS];
    
    // Constants for sensor reading
    static constexpr uint8_t NUM_SAMPLES = 5;  // Number of samples to average
    static constexpr uint8_t READING_DELAY = 100;  // Delay between readings in ms
    static constexpr uint8_t MAX_RETRIES = 3;  // Maximum number of retries for sensor operations
    
    // Initialization state
    bool initialized;
    
    /**
     * Safely takes the mutex with timeout
     * @return true if mutex was taken successfully
     */
    bool takeMutex() {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "SensorManager::takeMutex"
            );
            return false;
        }
        return true;
    }
    
    /**
     * Safely gives the mutex
     */
    void giveMutex() {
        xSemaphoreGive(mutex);
    }
    
    /**
     * Initializes water level sensor for a unit
     * @param unitIndex Index of the unit
     * @return true if initialization successful
     */
    bool initializeWaterSensor(int unitIndex) {
        if (!takeMutex()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to initialize water sensor",
                "SensorManager::initializeWaterSensor"
            );
            return false;
        }
        
        // Create appropriate sensor type
        if (SystemConfig::WATER_LEVEL_SENSOR_TYPE == SystemConfig::WaterLevelSensorType::CAPACITIVE) {
            waterSensors[unitIndex] = new CapacitiveWaterSensor(hardwareManager);
        } else {
            waterSensors[unitIndex] = new FloatSwitchSensor(hardwareManager);
        }
        
        // Try to initialize with retries
        for (int i = 0; i < SystemConfig::MAX_SENSOR_RETRIES; i++) {
            if (waterSensors[unitIndex]->initialize()) {
                giveMutex();
                return true;
            }
            delay(SystemConfig::SENSOR_RETRY_DELAY_MS);
        }
        
        ErrorManager::sensorError(
            ErrorManager::ErrorCode::SENSOR_INIT_FAILED,
            "Failed to initialize water sensor after " + String(SystemConfig::MAX_SENSOR_RETRIES) + " attempts",
            "SensorManager::initializeWaterSensor"
        );
        
        giveMutex();
        return false;
    }
    
    /**
     * Initializes MCP3021 sensor for a unit
     * @param unitIndex Index of the unit
     * @return true if initialization successful
     */
    bool initializeMCP3021(int unitIndex) {
        if (!takeMutex()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to initialize MCP3021",
                "SensorManager::initializeMCP3021"
            );
            return false;
        }
        
        // Initialize MCP3021
        mcp3021[unitIndex] = new MCP3021(SystemConfig::MCP3021_ADDR);
        
        // Try to initialize with retries
        for (int i = 0; i < SystemConfig::MAX_SENSOR_RETRIES; i++) {
            int reading = mcp3021[unitIndex]->read();
            if (reading >= 0) {  // Valid reading
                giveMutex();
                return true;
            }
            delay(SystemConfig::SENSOR_RETRY_DELAY_MS);
        }
        
        ErrorManager::sensorError(
            ErrorManager::ErrorCode::SENSOR_INIT_FAILED,
            "Failed to initialize MCP3021 after " + String(SystemConfig::MAX_SENSOR_RETRIES) + " attempts",
            "SensorManager::initializeMCP3021"
        );
        
        giveMutex();
        return false;
    }
    
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
        if (waterSensors[unitIndex] == nullptr) {
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
            float reading = waterSensors[unitIndex]->readWaterLevel();
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
        if (mcp3021[unitIndex] == nullptr) {
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
            waterSensors[i] = nullptr;
            mcp3021[i] = nullptr;
            errorCount[i] = 0;
            errorMessages[i].reserve(ERROR_MSG_MAX_LENGTH);
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
            safeDelete(waterSensors[i]);
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
            // Reset water sensor
            success = initializeWaterSensor(unitIndex);
        } else if (strcmp(sensorType, SystemConfig::SENSOR_TYPE_EC) == 0) {
            success = initializeMCP3021(unitIndex);
        }
        
        return success;
    }
    
    /**
     * Initializes all sensors
     * @return true if initialization successful
     */
    bool initialize() {
        bool success = true;
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!initializeWaterSensor(i) || !initializeMCP3021(i)) {
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
            if (!initialize()) {
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
     * Reads water level from sensor for a specific unit
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