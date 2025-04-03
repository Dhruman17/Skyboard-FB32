#ifndef UNIT_MANAGER_H
#define UNIT_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "firebase_manager.h"
#include "sensor_manager.h"
#include "atomizer_manager.h"
#include <Firebase_ESP_Client.h>
#include <time.h>
#include "mutex_manager.h"
#include <Arduino.h>
#include <string>

/**
 * UnitManager Class
 * 
 * Manages individual vertical farming units, handling:
 * 1. Irrigation Control:
 *    - Enable/disable each unit independently
 *    - Configure on/off intervals for atomizers
 *    - PWM control of atomizer operation
 * 
 * 2. Sensor Monitoring:
 *    - Water level readings from FDC1004 (only when unit's atomizer is off)
 *    - EC readings from MCP3021 (only when unit's atomizer is off)
 * 
 * 3. Firebase Integration:
 *    - Reads unit configuration (state, intervals)
 *    - Updates sensor readings
 * 
 * Unit Configuration:
 * - Each unit has a unique name in Firebase
 * - Irrigation intervals are in seconds
 * - Water level and EC readings are updated every 30 seconds
 * - Sensor readings are only taken when the specific unit's atomizer is off
 */
class UnitManager : public MutexManager {
private:
    SystemState& systemState;
    FirebaseManager& firebaseManager;
    SensorManager& sensorManager;
    AtomizerManager& atomizerManager;
    
    // Pre-allocate string space to prevent fragmentation
    static constexpr size_t UNIT_NAME_MAX_LENGTH = 50;
    String unitNames[SystemConfig::NUMBER_OF_UNITS];
    
    // Structure to hold atomizer timing information
    struct AtomizerTiming {
        time_t nextOnTime;
        time_t nextOffTime;
    };
    
    // Array to store timing information for each unit
    AtomizerTiming atomizerTimings[SystemConfig::NUMBER_OF_UNITS];
    
    unsigned long previousMillis[SystemConfig::NUMBER_OF_UNITS];
    mutable bool atomizerStates[SystemConfig::NUMBER_OF_UNITS];
    bool previousWaterLevelStates[SystemConfig::NUMBER_OF_UNITS];
    
    // Reusable buffer for water level readings
    static constexpr float INVALID_WATER_LEVEL = -1.0f;
    float waterLevels[SystemConfig::NUMBER_OF_UNITS];
    
    // I2C error tracking
    static constexpr uint8_t MAX_I2C_ERRORS = 3;
    mutable uint8_t i2cErrorCount;  // Made mutable to allow modification in const methods
    
    // Error tracking
    mutable uint8_t consecutiveErrors[SystemConfig::NUMBER_OF_UNITS] = {0};
    uint8_t firebaseErrorCount = 0;
    uint8_t sensorErrorCount = 0;
    
    bool initialized;
    unsigned long lastUpdate;
    unsigned long atomizerOnTimes[SystemConfig::NUMBER_OF_UNITS];
    unsigned long atomizerOffTimes[SystemConfig::NUMBER_OF_UNITS];
    
    bool unitStates[SystemConfig::NUMBER_OF_UNITS];
    bool waterLevelStates[SystemConfig::NUMBER_OF_UNITS];
    unsigned long atomizerOnIntervals[SystemConfig::NUMBER_OF_UNITS];
    unsigned long atomizerOffIntervals[SystemConfig::NUMBER_OF_UNITS];
    unsigned long lastAtomizerStateChange[SystemConfig::NUMBER_OF_UNITS];
    
    /**
     * Initializes a specific unit
     * @param unitIndex Index of the unit to initialize
     * @return true if initialization was successful
     */
    bool initializeUnit(int unitIndex) {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::initializeUnit"
            );
            return false;
        }
        
        // Initialize unit state
        unitStates[unitIndex] = false;
        waterLevelStates[unitIndex] = false;
        atomizerStates[unitIndex] = false;
        previousWaterLevelStates[unitIndex] = false;
        
        // Set default intervals
        atomizerOnIntervals[unitIndex] = DefaultValues::DEFAULT_ATOMIZER_ON_INTERVAL;
        atomizerOffIntervals[unitIndex] = DefaultValues::DEFAULT_ATOMIZER_OFF_INTERVAL;
        
        // Initialize timing
        atomizerOnTimes[unitIndex] = 0;
        atomizerOffTimes[unitIndex] = 0;
        lastAtomizerStateChange[unitIndex] = 0;
        
        return true;
    }
    
    /**
     * Cleans up resources for a specific unit
     * @param unitIndex Index of the unit to clean up
     */
    void cleanup(int unitIndex) {
        if (!isValidAndEnabled(unitIndex)) {
            return;
        }
        
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return;
        }
        
        // Reset unit state
        unitStates[unitIndex] = false;
        waterLevelStates[unitIndex] = false;
        atomizerStates[unitIndex] = false;
        previousWaterLevelStates[unitIndex] = false;
        
        // Reset timing
        atomizerOnTimes[unitIndex] = 0;
        atomizerOffTimes[unitIndex] = 0;
        lastAtomizerStateChange[unitIndex] = 0;
        
        // Reset water level
        waterLevels[unitIndex] = INVALID_WATER_LEVEL;
    }
    
    /**
     * Validates a unit index and checks if the unit is enabled
     * @param unitIndex Index of the unit to validate
     * @return true if unit is valid and enabled
     */
    bool isValidAndEnabled(int unitIndex) const {
        if (unitIndex < 0 || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return false;
        }
        return systemState.unitsEnabled[unitIndex];
    }
    
    /**
     * Safely reads the atomizer state with mutex protection
     * @param unitIndex Index of the unit
     * @param state Reference to store the state
     * @return true if state was read successfully
     */
    bool getAtomizerState(int unitIndex, bool& state) const {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::getAtomizerState"
            );
            return false;
        }
        state = atomizerStates[unitIndex];
        return true;
    }
    
    /**
     * Safely sets the atomizer state with mutex protection
     * @param unitIndex Index of the unit
     * @param newState New state to set
     * @return true if state was set successfully
     */
    bool setAtomizerState(int unitIndex, bool newState) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::setAtomizerState"
            );
            return false;
        }
        atomizerStates[unitIndex] = newState;
        return true;
    }
    
    /**
     * Helper method to safely create Firebase paths
     * @param buffer Buffer to store the path
     * @param format Format string for snprintf
     * @param ... Additional arguments for snprintf
     * @return true if path was created successfully
     */
    bool createFirebasePath(char* buffer, size_t bufferSize, const char* format, ...) {
        if (!buffer || bufferSize == 0) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_PATH_INVALID,
                "Invalid buffer",
                "UnitManager::createFirebasePath"
            );
            return false;
        }
        
        va_list args;
        va_start(args, format);
        int written = vsnprintf(buffer, bufferSize, format, args);
        va_end(args);
        
        if (written >= bufferSize) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_PATH_INVALID,
                "Buffer overflow",
                "UnitManager::createFirebasePath"
            );
            return false;
        }
        return true;
    }
    
    /**
     * Helper method to safely create Firebase paths
     * @param buffer Buffer to store the path
     * @param unitIndex Index of the unit
     * @return true if path was created successfully
     */
    bool createUnitPath(char* buffer, size_t bufferSize, int unitIndex) {
        if (!buffer || bufferSize == 0 || unitIndex < 0 || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return false;
        }
        
        return createFirebasePath(buffer, bufferSize, SystemConfig::UNIT_PATH_FORMAT, SystemConfig::SERIAL_NUMBER, unitIndex);
    }
    
    /**
     * Handles sensor errors with retry mechanism
     * @param unitIndex Index of the unit
     * @param errorType Type of error
     * @return true if error was handled successfully
     */
    bool handleSensorError(int unitIndex, const char* errorType) const {
        // Get current error count with minimal mutex time
        uint8_t currentErrors = 0;
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                ErrorManager::mutexError(
                    ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                    "Failed to take mutex",
                    "UnitManager::handleSensorError"
                );
                return false;
            }
            currentErrors = consecutiveErrors[unitIndex];
        }
        
        // Increment error count
        currentErrors++;
        
        // Update error count with minimal mutex time
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                return false;
            }
            consecutiveErrors[unitIndex] = currentErrors;
        }
        
        if (currentErrors >= SystemConfig::MAX_CONSECUTIVE_ERRORS) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_READ_FAILED,
                String("Too many consecutive ") + errorType + " sensor errors for unit " + String(unitIndex),
                "UnitManager::handleSensorError"
            );
            
            // Allow other tasks to run before recalibration
            yield();
            
            // Attempt sensor recalibration outside of main mutex
            bool recalibrated = recalibrateSensor(unitIndex, errorType);
            
            // Update error count based on recalibration result
            {
                ScopedLock lock(*this);
                if (!lock.isLocked()) {
                    return false;
                }
                
                if (recalibrated) {
                    consecutiveErrors[unitIndex] = 0;
                }
            }
            
            if (!recalibrated) {
                ErrorManager::sensorError(
                    ErrorManager::ErrorCode::SENSOR_READ_FAILED,
                    String("Failed to recover ") + errorType + " sensor for unit " + String(unitIndex),
                    "UnitManager::handleSensorError"
                );
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * Sets the multiplexer for a specific unit
     * @param unitIndex Index of the unit
     * @return true if multiplexer was set successfully
     */
    bool setMultiplexer(int unitIndex) const {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        // Get hardware manager reference with minimal mutex time
        HardwareManager* hwManager = nullptr;
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                ErrorManager::mutexError(
                    ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                    "Failed to take mutex",
                    "UnitManager::setMultiplexer"
                );
                return false;
            }
            hwManager = &sensorManager.getHardwareManager();
        }
        
        // Perform I2C operation outside of mutex
        bool success = hwManager->selectUnitAndSensor(unitIndex, SystemConfig::FDC1004_CHANNEL);
        if (!success) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_I2C_ERROR,
                String("Failed to set multiplexer for unit ") + String(unitIndex),
                "UnitManager::setMultiplexer"
            );
        }
        
        return success;
    }
    
    /**
     * Recalibrates a sensor for a specific unit
     * @param unitIndex Index of the unit
     * @param sensorType Type of sensor to recalibrate
     * @return true if recalibration was successful
     */
    bool recalibrateSensor(int unitIndex, const char* sensorType) const {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::recalibrateSensor"
            );
            return false;
        }
        
        bool success = true;
        
        if (strcmp(sensorType, SystemConfig::SENSOR_TYPE_WATER) == 0) {
            sensorManager.cleanup();
            success = sensorManager.begin();
        } else if (strcmp(sensorType, SystemConfig::SENSOR_TYPE_EC) == 0) {
            sensorManager.cleanup();
            success = sensorManager.begin();
        }
        
        return success;
    }
    
    /**
     * Handles Firebase operation errors with retry mechanism
     * @param operation Description of the failed operation
     * @return true if error was handled successfully
     */
    bool handleFirebaseError(const char* operation) {
        firebaseErrorCount++;
        
        if (firebaseErrorCount >= SystemConfig::MAX_FIREBASE_RETRIES) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                String("Too many errors in ") + operation + ", attempting recovery",
                "UnitManager::handleFirebaseError"
            );
            
            // Attempt to reconnect to Firebase
            if (!reconnectFirebase()) {
                ErrorManager::firebaseError(
                    ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                    String("Failed to recover connection in ") + operation,
                    "UnitManager::handleFirebaseError"
                );
                return false;
            }
            
            firebaseErrorCount = 0;
        }
        
        return true;
    }
    
    /**
     * Reconnect to Firebase
     * @return true if reconnection was successful
     */
    bool reconnectFirebase() {
        for (int retry = 0; retry < SystemConfig::MAX_FIREBASE_RETRIES; retry++) {
            Firebase.reconnectWiFi(true);  // Force reconnection
            if (Firebase.ready()) {
                return true;
            }
            delay(SystemConfig::FIREBASE_RETRY_DELAY_MS);
        }
        return false;
    }
    
    /**
     * Updates a field in Firebase for a specific unit with enhanced error recovery
     * Thread-safe: Yes (uses mutex)
     * Error Handling: Includes retry mechanism and connection recovery
     * @param unitIndex Index of the unit
     * @param fieldPath Path to the field in Firebase
     * @param value Value to update
     * @param valueType Type of value
     * @return true if update was successful
     */
    bool updateUnitField(int unitIndex, const char* fieldPath, const String& value, const char* valueType) {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::updateUnitField"
            );
            return false;
        }
        
        bool success = firebaseManager.addToBatch(unitIndex, fieldPath, value, valueType);
        
        if (!success) {
            handleFirebaseError("updateUnitField");
        }
        
        return success;
    }
    
    /**
     * Updates a field in Firebase for a specific unit with enhanced error recovery
     * Thread-safe: Yes (uses mutex)
     * Error Handling: Includes retry mechanism and connection recovery
     * @param unitIndex Index of the unit
     * @param fieldPath Path to the field in Firebase
     * @param value Value to update
     * @param valueType Type of value
     * @return true if update was successful
     */
    bool updateUnitField(int unitIndex, const char* fieldPath, bool value, const char* valueType) {
        return updateUnitField(unitIndex, fieldPath, value ? "true" : "false", valueType);
    }
    
    /**
     * Updates a field in Firebase for a specific unit with enhanced error recovery
     * Thread-safe: Yes (uses mutex)
     * Error Handling: Includes retry mechanism and connection recovery
     * @param unitIndex Index of the unit
     * @param fieldPath Path to the field in Firebase
     * @param value Value to update
     * @param valueType Type of value
     * @return true if update was successful
     */
    bool updateUnitField(int unitIndex, const char* fieldPath, float value, const char* valueType) {
        return updateUnitField(unitIndex, fieldPath, String(value, 3), valueType);
    }
    
    /**
     * Updates a field in Firebase for a specific unit with enhanced error recovery
     * Thread-safe: Yes (uses mutex)
     * Error Handling: Includes retry mechanism and connection recovery
     * @param unitIndex Index of the unit
     * @param fieldPath Path to the field in Firebase
     * @param value Value to update
     * @param valueType Type of value
     * @return true if update was successful
     */
    bool updateUnitField(int unitIndex, const char* fieldPath, int value, const char* valueType) {
        return updateUnitField(unitIndex, fieldPath, String(value), valueType);
    }
    
    /**
     * Reads a unit's document from Firebase
     * @param unitIndex Index of the unit
     * @return true if read was successful
     */
    bool readUnitDocument(int unitIndex) {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::readUnitDocument"
            );
            return false;
        }
        
        char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        if (!createUnitPath(pathBuffer, sizeof(pathBuffer), unitIndex)) {
            return false;
        }
        
        String unitData;
        bool success = firebaseManager.getDocument(pathBuffer, unitData);
        
        if (!success) {
            handleFirebaseError("readUnitDocument");
        }
        
        return success;
    }
    
    /**
     * Updates water level readings for all units
     */
    void updateWaterLevelReadings() {
        // Process one unit at a time to avoid long mutex holds
        static int currentUnit = 0;
        
        // Process the current unit
        if (currentUnit < SystemConfig::NUMBER_OF_UNITS) {
            if (systemState.unitsEnabled[currentUnit]) {
                updateWaterLevel(currentUnit);
            }
            
            // Move to next unit for next call
            currentUnit++;
        } else {
            // Reset to start for next cycle
            currentUnit = 0;
        }
    }
    
    /**
     * Updates water level for a specific unit
     * @param unitIndex Index of the unit
     */
    void updateWaterLevel(int unitIndex) {
        if (!isValidAndEnabled(unitIndex)) {
            return;
        }
        
        // Get atomizer state with minimal mutex time
        bool atomizerOn = false;
        {
            ScopedLock lock(*this);
            if (!lock.isLocked()) {
                ErrorManager::mutexError(
                    ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                    "Failed to take mutex",
                    "UnitManager::updateWaterLevel"
                );
                return;
            }
            atomizerOn = atomizerStates[unitIndex];
        }
        
        // Only read water level if atomizer is off
        if (!atomizerOn) {
            // Set multiplexer outside of main mutex
            if (!setMultiplexer(unitIndex)) {
                return;
            }
            
            // Allow other tasks to run
            yield();
            
            // Get water level reading
            float level = sensorManager.getWaterLevel(unitIndex);
            
            // Process the reading with minimal mutex time
            if (level >= 0) {
                bool newWaterLevelState = level > SystemConfig::WATER_LEVEL_MIN;
                bool stateChanged = false;
                
                {
                    ScopedLock lock(*this);
                    if (!lock.isLocked()) {
                        return;
                    }
                    
                    // Update water level state
                    waterLevels[unitIndex] = level;
                    waterLevelStates[unitIndex] = newWaterLevelState;
                    
                    // Check if state changed
                    stateChanged = (waterLevelStates[unitIndex] != previousWaterLevelStates[unitIndex]);
                    if (stateChanged) {
                        previousWaterLevelStates[unitIndex] = waterLevelStates[unitIndex];
                    }
                }
                
                // Update Firebase outside of mutex if state changed
                if (stateChanged) {
                    // Allow other tasks to run
                    yield();
                    
                    firebaseManager.updateDocument(
                        SystemConfig::UNIT_PATH_FORMAT,
                        newWaterLevelState ? "true" : "false",
                        "bool"
                    );
                }
            } else {
                // Handle sensor error outside of main mutex
                handleSensorError(unitIndex, SystemConfig::SENSOR_TYPE_WATER);
            }
        }
    }
    
    /**
     * Verifies and corrects hardware state for a unit
     * @param unitIndex Index of the unit
     * @return true if hardware state is valid
     */
    bool verifyAndCorrectHardwareState(int unitIndex) {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::verifyAndCorrectHardwareState"
            );
            return false;
        }
        
        bool success = true;
        
        // Check multiplexer state
        if (!setMultiplexer(unitIndex)) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_I2C_ERROR,
                String("Failed to verify multiplexer state for unit ") + String(unitIndex),
                "UnitManager::verifyAndCorrectHardwareState"
            );
            success = false;
        }
        
        // Check sensor states by attempting to read them
        if (sensorManager.getWaterLevel(unitIndex) < 0) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_I2C_ERROR,
                String("Failed to verify water level sensor for unit ") + String(unitIndex),
                "UnitManager::verifyAndCorrectHardwareState"
            );
            success = false;
        }
        
        if (sensorManager.getEC(unitIndex) < 0) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_I2C_ERROR,
                String("Failed to verify EC sensor for unit ") + String(unitIndex),
                "UnitManager::verifyAndCorrectHardwareState"
            );
            success = false;
        }
        
        return success;
    }
    
    /**
     * Updates atomizer timing for a specific unit
     * Thread-safe: Yes
     * @param unitIndex Index of the unit to update
     * @return true if update successful
     */
    bool updateAtomizerTiming(uint8_t unitIndex) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to update atomizer timing",
                "UnitManager::updateAtomizerTiming"
            );
            return false;
        }
        
        if (unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INVALID_STATE,
                "Invalid unit index",
                "UnitManager::updateAtomizerTiming"
            );
            return false;
        }
        
        // Use stored intervals from systemState
        unsigned long onInterval = systemState.atomizerOnIntervals[unitIndex];
        unsigned long offInterval = systemState.atomizerOffIntervals[unitIndex];
        
        // Validate intervals
        if (onInterval == 0 || offInterval == 0) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INVALID_STATE,
                "Invalid atomizer intervals",
                "UnitManager::updateAtomizerTiming"
            );
            return false;
        }
        
        // Update atomizer timing
        if (!atomizerManager.setTiming(unitIndex, onInterval, offInterval)) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_PWM_ERROR,
                "Failed to set atomizer timing",
                "UnitManager::updateAtomizerTiming"
            );
            return false;
        }
        
        return true;
    }
    
    /**
     * Controls atomizer state
     * Thread-safe: Yes
     * @param unit Unit index
     * @param state Whether to turn atomizer on
     * @return true if control successful
     */
    bool controlAtomizer(int unit, bool state) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::controlAtomizer"
            );
            return false;
        }
        
        if (unit < 0 || unit >= SystemConfig::NUMBER_OF_UNITS) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::INVALID_UNIT,
                "Invalid unit index",
                "UnitManager::controlAtomizer"
            );
            return false;
        }
        
        // Set atomizer state
        digitalWrite(SystemConfig::ATOMIZER_PINS[unit], state ? HIGH : LOW);
        atomizerStates[unit] = state;
        
        // Update timing
        if (state) {
            atomizerOnTimes[unit] = millis();
        } else {
            atomizerOffTimes[unit] = millis();
        }
        
        return true;
    }

    /**
     * Updates atomizer output for a unit
     * @param unitIndex Index of the unit
     * @return true if update was successful
     */
    bool updateAtomizerOutput(int unitIndex) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::updateAtomizerOutput"
            );
            return false;
        }
        
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        return controlAtomizer(unitIndex, atomizerStates[unitIndex]);
    }

public:
    /**
     * Constructor
     */
    UnitManager(SystemState& state, FirebaseManager& fb, SensorManager& sm, AtomizerManager& am)
        : MutexManager(), systemState(state), firebaseManager(fb), sensorManager(sm), atomizerManager(am),
          i2cErrorCount(0), initialized(false), lastUpdate(0) {
        // Initialize arrays
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            unitNames[i].reserve(UNIT_NAME_MAX_LENGTH);
            atomizerTimings[i] = {0, 0};
            previousMillis[i] = 0;
            atomizerStates[i] = false;
            previousWaterLevelStates[i] = false;
            waterLevels[i] = INVALID_WATER_LEVEL;
            consecutiveErrors[i] = 0;
            atomizerOnTimes[i] = 0;
            atomizerOffTimes[i] = 0;
            unitStates[i] = false;
            waterLevelStates[i] = false;
            atomizerOnIntervals[i] = 0;
            atomizerOffIntervals[i] = 0;
            lastAtomizerStateChange[i] = 0;
        }
    }
    
    /**
     * Destructor
     */
    ~UnitManager() {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            cleanup(i);
        }
    }
    
    /**
     * Checks if a unit's atomizer is on
     * @param unitIndex Index of the unit
     * @return true if atomizer is on
     */
    bool isAtomizerOn(int unitIndex) const {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        bool state;
        if (!getAtomizerState(unitIndex, state)) {
            return false;
        }
        return state;
    }
    
    /**
     * Reads EC sensor value for a unit
     * @param unitIndex Index of the unit
     * @return EC value or -1 if error
     */
    float readECSensorValue(const int& unitIndex) const {
        if (!isValidAndEnabled(unitIndex)) {
            return -1;
        }
        
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::readECSensorValue"
            );
            return -1;
        }
        
        float value = -1;
        
        // Only read EC if atomizer is off
        if (!atomizerStates[unitIndex]) {
            if (!setMultiplexer(unitIndex)) {
                return -1;
            }
            
            value = sensorManager.getEC(unitIndex);
            if (value < 0) {
                handleSensorError(unitIndex, SystemConfig::SENSOR_TYPE_EC);
            }
        }
        
        return value;
    }
    
    /**
     * Updates unit names from Firebase
     * @param names Array of unit names
     */
    void updateUnitNames(const String names[SystemConfig::NUMBER_OF_UNITS]) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::updateUnitNames"
            );
            return;
        }
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            unitNames[i] = names[i];
        }
    }
    
    /**
     * Updates unit data from Firebase
     */
    void updateUnitData() {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (systemState.unitsEnabled[i]) {
                readUnitDocument(i);
            }
        }
    }
    
    /**
     * Updates unit states and handles atomizer control
     * Thread-safe: Yes
     */
    void update() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::update"
            );
            return;
        }

        if (!initialized) {
            return;
        }

        unsigned long currentMillis = millis();
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!unitStates[i]) {
                continue;
            }

            // Check if we need to change atomizer state
            if (currentMillis - lastAtomizerStateChange[i] >= 
                (atomizerStates[i] ? atomizerOnIntervals[i] : atomizerOffIntervals[i])) {
                
                // Toggle atomizer state
                atomizerStates[i] = !atomizerStates[i];
                lastAtomizerStateChange[i] = currentMillis;
                
                // Update atomizer output
                updateAtomizerOutput(i);
            }
        }
    }
    
    /**
     * Updates atomizer state for a unit
     * @param unitIndex Index of the unit
     * @param isOn New state
     * @return true if update was successful
     */
    bool updateAtomizerState(int unitIndex, bool isOn) {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::updateAtomizerState"
            );
            return false;
        }
        
        bool success = setAtomizerState(unitIndex, isOn);
        
        if (success) {
            success = updateUnitField(unitIndex, "atomizerOn", isOn, "bool");
        }
        
        return success;
    }
    
    /**
     * Reads water level for all units
     */
    void readWaterLevel() {
        updateWaterLevelReadings();
    }
    
    /**
     * Initializes the unit manager
     * Thread-safe: Yes
     * @return true if initialization successful
     */
    bool begin() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }

        bool success = true;
        
        // Initialize atomizer pins
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!initializeUnit(i)) {
                success = false;
                break;
            }
        }
        
        if (success) {
            initialized = true;
        }
        
        return success;
    }

    /**
     * Gets the state of a unit
     * Thread-safe: Yes
     * @param unitIndex Index of the unit
     * @return true if unit is enabled
     */
    bool getUnitState(uint8_t unitIndex) {
        ScopedLock lock(*this);
        if (!lock.isLocked() || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return false;
        }
        return unitStates[unitIndex];
    }

    /**
     * Sets the state of a unit
     * Thread-safe: Yes
     * @param unitIndex Index of the unit
     * @param state New state
     */
    void setUnitState(uint8_t unitIndex, bool state) {
        ScopedLock lock(*this);
        if (!lock.isLocked() || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return;
        }
        
        if (unitStates[unitIndex] != state) {
            unitStates[unitIndex] = state;
            if (!state) {
                // Turn off atomizer when disabling unit
                atomizerStates[unitIndex] = false;
                updateAtomizerOutput(unitIndex);
            }
        }
    }

    /**
     * Gets the water level state of a unit
     * Thread-safe: Yes
     * @param unitIndex Index of the unit
     * @return true if water level is OK
     */
    bool getWaterLevelState(uint8_t unitIndex) {
        ScopedLock lock(*this);
        if (!lock.isLocked() || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return false;
        }
        return waterLevelStates[unitIndex];
    }

    /**
     * Sets the water level state of a unit
     * Thread-safe: Yes
     * @param unitIndex Index of the unit
     * @param state New state
     */
    void setWaterLevelState(uint8_t unitIndex, bool state) {
        ScopedLock lock(*this);
        if (!lock.isLocked() || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return;
        }
        
        previousWaterLevelStates[unitIndex] = waterLevelStates[unitIndex];
        waterLevelStates[unitIndex] = state;
    }

    /**
     * Gets the atomizer intervals for a unit
     * Thread-safe: Yes
     * @param unitIndex Index of the unit
     * @param onInterval Reference to store on interval
     * @param offInterval Reference to store off interval
     * @return true if successful
     */
    bool getAtomizerIntervals(uint8_t unitIndex, unsigned long& onInterval, unsigned long& offInterval) {
        ScopedLock lock(*this);
        if (!lock.isLocked() || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return false;
        }
        
        onInterval = atomizerOnIntervals[unitIndex];
        offInterval = atomizerOffIntervals[unitIndex];
        return true;
    }

    /**
     * Sets the atomizer intervals for a unit
     * Thread-safe: Yes
     * @param unitIndex Index of the unit
     * @param onInterval New on interval
     * @param offInterval New off interval
     */
    void setAtomizerIntervals(uint8_t unitIndex, unsigned long onInterval, unsigned long offInterval) {
        ScopedLock lock(*this);
        if (!lock.isLocked() || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return;
        }
        
        atomizerOnIntervals[unitIndex] = onInterval;
        atomizerOffIntervals[unitIndex] = offInterval;
    }
};

#endif // UNIT_MANAGER_H 