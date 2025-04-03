#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "config.h"
#include "hardware_manager.h"
#include "water_level_sensor.h"
#include "capacitive_water_level_sensor.h"
#include "mutex_manager.h"
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
class SensorManager : public MutexManager {
private:
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
     * Initializes water level sensor for a unit
     * @param unitIndex Index of the unit
     * @return true if initialization successful
     */
    bool initializeWaterLevelSensor(uint8_t unitIndex) {
        // Validate unit index with minimal mutex time
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_INIT_FAILED,
                "Invalid unit index",
                "SensorManager::initializeWaterLevelSensor"
            );
            return false;
        }
        
        // Clean up existing sensor with minimal mutex time
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return false;
            }
            
            if (waterSensors[unitIndex] != nullptr) {
                delete waterSensors[unitIndex];
                waterSensors[unitIndex] = nullptr;
            }
        }
        
        // Create new sensor instance outside of mutex
        WaterLevelSensor* newSensor = new CapacitiveWaterLevelSensor(unitIndex);
        if (newSensor == nullptr) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_INIT_FAILED,
                "Failed to allocate water level sensor",
                "SensorManager::initializeWaterLevelSensor"
            );
            return false;
        }
        
        // Initialize sensor outside of mutex
        bool initSuccess = newSensor->initialize();
        
        // Store sensor with minimal mutex time
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                delete newSensor;
                return false;
            }
            
            if (initSuccess) {
                waterSensors[unitIndex] = newSensor;
            } else {
                delete newSensor;
            }
        }
        
        return initSuccess;
    }
    
    /**
     * Initializes EC sensor for a unit
     * @param unitIndex Index of the unit
     * @return true if initialization successful
     */
    bool initializeECSensor(uint8_t unitIndex) {
        // Validate unit index with minimal mutex time
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_INIT_FAILED,
                "Invalid unit index",
                "SensorManager::initializeECSensor"
            );
            return false;
        }
        
        // Clean up existing sensor with minimal mutex time
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return false;
            }
            
            if (mcp3021[unitIndex] != nullptr) {
                delete mcp3021[unitIndex];
                mcp3021[unitIndex] = nullptr;
            }
        }
        
        // Create new sensor instance outside of mutex
        MCP3021* newSensor = new MCP3021(SystemConfig::MCP3021_ADDR);
        if (newSensor == nullptr) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_INIT_FAILED,
                "Failed to allocate EC sensor",
                "SensorManager::initializeECSensor"
            );
            return false;
        }
        
        // Initialize sensor outside of mutex
        int testReading = newSensor->read();
        bool initSuccess = (testReading >= 0);
        
        // Store sensor with minimal mutex time
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                delete newSensor;
                return false;
            }
            
            if (initSuccess) {
                mcp3021[unitIndex] = newSensor;
            } else {
                delete newSensor;
            }
        }
        
        return initSuccess;
    }
    
    /**
     * Safely deletes a pointer and sets it to nullptr
     * @param ptr Pointer to delete
     */
    template<typename T>
    void safeDelete(T*& ptr) {
        if (ptr != nullptr) {
            delete ptr;
            ptr = nullptr;
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
    }
    
    /**
     * Destructor
     * Cleans up sensor objects and error messages
     */
    ~SensorManager() {
        cleanup();
    }
    
    /**
     * Cleans up all sensor objects
     * Thread-safe: Yes (uses mutex)
     * Memory Management: Properly deletes all sensor instances
     */
    void cleanup() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
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
     * Initializes the sensor manager
     * @return true if initialization successful
     */
    bool begin() {
        // Check if already initialized with minimal mutex time
        bool alreadyInitialized = false;
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return false;
            }
            alreadyInitialized = initialized;
        }
        
        if (alreadyInitialized) {
            return true;
        }
        
        bool success = true;
        
        // Initialize sensors for each unit one at a time
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            // Initialize water level sensor with minimal mutex time
            if (!initializeWaterLevelSensor(i)) {
                success = false;
                break;
            }
            
            // Allow other tasks to run
            yield();
            
            // Initialize EC sensor with minimal mutex time
            if (!initializeECSensor(i)) {
                success = false;
                break;
            }
            
            // Allow other tasks to run
            yield();
        }
        
        // Update initialization state with minimal mutex time
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return false;
            }
            initialized = success;
        }
        
        return success;
    }
    
    /**
     * Gets water level reading for a unit
     * @param unitIndex Index of the unit
     * @return Water level in cm, -1.0f if error
     */
    float getWaterLevel(uint8_t unitIndex) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return -1.0f;
        }
        
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS || waterSensors[unitIndex] == nullptr) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_READ_FAILED,
                "Invalid unit index or sensor not initialized",
                "SensorManager::getWaterLevel"
            );
            return -1.0f;
        }
        
        float reading = waterSensors[unitIndex]->readWaterLevel();
        if (reading < 0) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_READING_FAILED,
                "Failed to read water level",
                "SensorManager::getWaterLevel"
            );
        }
        
        return reading;
    }
    
    /**
     * Gets EC reading for a unit
     * @param unitIndex Index of the unit
     * @return EC value in mS/cm, -1.0f if error
     */
    float getEC(uint8_t unitIndex) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return -1.0f;
        }
        
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS || mcp3021[unitIndex] == nullptr) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_READ_FAILED,
                "Invalid unit index or sensor not initialized",
                "SensorManager::getEC"
            );
            return -1.0f;
        }
        
        float reading = mcp3021[unitIndex]->read();
        if (reading < 0) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_READING_FAILED,
                "Failed to read EC value",
                "SensorManager::getEC"
            );
        }
        
        return reading;
    }
    
    /**
     * Checks if a unit's sensors are initialized
     * @param unitIndex Index of the unit
     * @return true if sensors are initialized
     */
    bool isUnitInitialized(uint8_t unitIndex) const {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return false;
        }
        
        return waterSensors[unitIndex] != nullptr && mcp3021[unitIndex] != nullptr;
    }
    
    /**
     * Gets error count for a unit
     * @param unitIndex Index of the unit
     * @return Error count
     */
    uint8_t getErrorCount(uint8_t unitIndex) const {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return 0;
        }
        
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return 0;
        }
        
        return errorCount[unitIndex];
    }
    
    /**
     * Gets the hardware manager reference
     * @return Reference to the hardware manager
     */
    HardwareManager& getHardwareManager() const {
        return hardwareManager;
    }
};

#endif // SENSOR_MANAGER_H