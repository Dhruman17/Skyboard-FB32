#ifndef UNIT_MANAGER_H
#define UNIT_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "firebase_manager.h"
#include "sensor_coms.h"
#include <Firebase_ESP_Client.h>

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
class UnitManager {
private:
    SystemState& systemState;
    FirebaseManager& firebaseManager;
    SensorManager& sensorManager;
    
    // Pre-allocate string space to prevent fragmentation
    static constexpr size_t UNIT_NAME_MAX_LENGTH = 50;
    String unitNames[SystemConfig::NUMBER_OF_UNITS];
    
    unsigned long previousMillis[SystemConfig::NUMBER_OF_UNITS];
    mutable bool atomizerStates[SystemConfig::NUMBER_OF_UNITS];
    
    // Reusable buffer for water level readings
    static constexpr float INVALID_WATER_LEVEL = -1.0f;
    float waterLevels[SystemConfig::NUMBER_OF_UNITS];
    
    // Mutex configuration
    static constexpr uint32_t MUTEX_TIMEOUT_MS = 100;
    mutable SemaphoreHandle_t mutex;
    
    // I2C error tracking
    static constexpr uint8_t MAX_I2C_ERRORS = 3;
    mutable uint8_t i2cErrorCount;  // Made mutable to allow modification in const methods
    
    // Error tracking
    mutable uint8_t consecutiveErrors[SystemConfig::NUMBER_OF_UNITS] = {0};
    uint8_t firebaseErrorCount = 0;
    uint8_t sensorErrorCount = 0;
    
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
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            state = atomizerStates[unitIndex];
            xSemaphoreGive(mutex);
            return true;
        }
        return false;
    }
    
    /**
     * Safely sets the atomizer state with mutex protection
     * @param unitIndex Index of the unit
     * @param newState New state to set
     * @return true if state was set successfully
     */
    bool setAtomizerState(int unitIndex, bool newState) {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            atomizerStates[unitIndex] = newState;
            xSemaphoreGive(mutex);
            return true;
        }
        return false;
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
     * Handles sensor reading errors with retry mechanism
     * @param unitIndex Index of the unit
     * @param errorType Type of error ("water" or "ec")
     * @return true if error was handled successfully
     */
    bool handleSensorError(int unitIndex, const char* errorType) const {
        if (!takeMutex()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::handleSensorError"
            );
            return false;
        }
        
        bool success = true;
        consecutiveErrors[unitIndex]++;
        
        if (consecutiveErrors[unitIndex] >= SystemConfig::MAX_CONSECUTIVE_ERRORS) {
            ErrorManager::sensorError(
                ErrorManager::ErrorCode::SENSOR_READ_FAILED,
                String("Too many consecutive ") + errorType + " sensor errors for unit " + String(unitIndex),
                "UnitManager::handleSensorError"
            );
            
            // Attempt sensor recalibration
            if (!recalibrateSensor(unitIndex, errorType)) {
                ErrorManager::sensorError(
                    ErrorManager::ErrorCode::SENSOR_READ_FAILED,
                    String("Failed to recover ") + errorType + " sensor for unit " + String(unitIndex),
                    "UnitManager::handleSensorError"
                );
                success = false;
            } else {
                consecutiveErrors[unitIndex] = 0;
            }
        }
        
        giveMutex();
        return success;
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
        
        if (!takeMutex()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::setMultiplexer"
            );
            return false;
        }
        
        bool success = sensorManager.getHardwareManager().selectUnitAndSensor(unitIndex, SystemConfig::FDC1004_CHANNEL);
        
        if (!success) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_I2C_ERROR,
                String("Failed to set multiplexer for unit ") + String(unitIndex),
                "UnitManager::setMultiplexer"
            );
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Attempts to recalibrate a sensor
     * @param unitIndex Index of the unit
     * @param sensorType Type of sensor ("water" or "ec")
     * @return true if recalibration was successful
     */
    bool recalibrateSensor(int unitIndex, const char* sensorType) const {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        if (!takeMutex()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::recalibrateSensor"
            );
            return false;
        }
        
        bool success = false;
        if (strcmp(sensorType, SystemConfig::SENSOR_TYPE_WATER) == 0) {
            success = sensorManager.cleanupSensor(unitIndex, SystemConfig::SENSOR_TYPE_WATER);
        } else if (strcmp(sensorType, SystemConfig::SENSOR_TYPE_EC) == 0) {
            success = sensorManager.cleanupSensor(unitIndex, SystemConfig::SENSOR_TYPE_EC);
        }
        
        giveMutex();
        
        if (success) {
            handleSensorError(unitIndex, sensorType);
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
        
        if (!takeMutex()) {
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
        
        giveMutex();
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
        
        if (!takeMutex()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::readUnitDocument"
            );
            return false;
        }
        
        char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        if (!createUnitPath(pathBuffer, sizeof(pathBuffer), unitIndex)) {
            giveMutex();
            return false;
        }
        
        bool success = firebaseManager.updateField(pathBuffer, "lastRead", String(millis()));
        
        if (!success) {
            handleFirebaseError("readUnitDocument");
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Takes mutex with timeout
     * Thread-safe: Yes
     * @return true if mutex was taken
     */
    bool takeMutex() const {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::takeMutex"
            );
            return false;
        }
        return true;
    }
    
    /**
     * Releases mutex
     * Thread-safe: Yes
     */
    void giveMutex() const {
        xSemaphoreGive(mutex);
    }
    
    /**
     * Updates water level for a specific unit
     * @param unitIndex Index of the unit
     */
    void updateWaterLevel(int unitIndex) {
        if (!isValidAndEnabled(unitIndex)) {
            return;
        }
        
        if (!takeMutex()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::updateWaterLevel"
            );
            return;
        }
        
        // Only read water level if atomizer is off
        if (!atomizerStates[unitIndex]) {
            if (!setMultiplexer(unitIndex)) {
                giveMutex();
                return;
            }
            
            float waterLevel = sensorManager.readWaterLevel(unitIndex);
            if (waterLevel >= 0) {
                waterLevels[unitIndex] = waterLevel;
                updateUnitField(unitIndex, "waterLevel", waterLevel, "float");
            } else {
                handleSensorError(unitIndex, SystemConfig::SENSOR_TYPE_WATER);
            }
        }
        
        giveMutex();
    }
    
    /**
     * Updates water level readings for all units
     */
    void updateWaterLevelReadings() {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (systemState.unitsEnabled[i]) {
                updateWaterLevel(i);
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
        
        if (!takeMutex()) {
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
        if (sensorManager.readWaterLevel(unitIndex) < 0) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_I2C_ERROR,
                String("Failed to verify water level sensor for unit ") + String(unitIndex),
                "UnitManager::verifyAndCorrectHardwareState"
            );
            success = false;
        }
        
        if (sensorManager.readECValue(unitIndex) < 0) {
            ErrorManager::hardwareError(
                ErrorManager::ErrorCode::HARDWARE_I2C_ERROR,
                String("Failed to verify EC sensor for unit ") + String(unitIndex),
                "UnitManager::verifyAndCorrectHardwareState"
            );
            success = false;
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Updates atomizer timing for a unit
     * @param unitIndex Index of the unit
     * @param currentMillis Current time in milliseconds
     */
    void updateAtomizerTiming(int unitIndex, unsigned long currentMillis) {
        if (!isValidAndEnabled(unitIndex)) {
            return;
        }
        
        if (!takeMutex()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "UnitManager::updateAtomizerTiming"
            );
            return;
        }
        
        unsigned long elapsed = currentMillis - previousMillis[unitIndex];
        
        // Update atomizer state based on timing
        if (elapsed >= DefaultValues::ATOMIZER_ON_INTERVAL) {
            if (!atomizerStates[unitIndex]) {
                atomizerStates[unitIndex] = true;
                updateUnitField(unitIndex, "atomizerOn", true, "bool");
            }
        } else if (elapsed >= DefaultValues::ATOMIZER_OFF_INTERVAL) {
            if (atomizerStates[unitIndex]) {
                atomizerStates[unitIndex] = false;
                updateUnitField(unitIndex, "atomizerOn", false, "bool");
            }
        }
        
        giveMutex();
    }
    
    /**
     * Initializes the unit manager
     * @return true if initialization was successful
     */
    bool begin() {
        mutex = xSemaphoreCreateMutex();
        if (!mutex) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_CREATION_FAILED,
                "Failed to create mutex",
                "UnitManager::begin"
            );
            return false;
        }
        
        // Initialize arrays
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            previousMillis[i] = 0;
            atomizerStates[i] = false;
            waterLevels[i] = INVALID_WATER_LEVEL;
            consecutiveErrors[i] = 0;
        }
        
        i2cErrorCount = 0;
        firebaseErrorCount = 0;
        sensorErrorCount = 0;
        
        return true;
    }

public:
    /**
     * Constructor
     * @param state Reference to system state
     * @param firebase Reference to Firebase manager
     * @param sensor Reference to sensor manager
     */
    UnitManager(SystemState& state, FirebaseManager& firebase, SensorManager& sensor)
        : systemState(state), firebaseManager(firebase), sensorManager(sensor) {
        begin();
    }
    
    /**
     * Destructor
     */
    ~UnitManager() {
        if (mutex) {
            vSemaphoreDelete(mutex);
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
        
        if (!takeMutex()) {
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
                giveMutex();
                return -1;
            }
            
            value = sensorManager.readECValue(unitIndex);
            if (value < 0) {
                handleSensorError(unitIndex, SystemConfig::SENSOR_TYPE_EC);
            }
        }
        
        giveMutex();
        return value;
    }
    
    /**
     * Updates unit names from Firebase
     * @param names Array of unit names
     */
    void updateUnitNames(const String names[SystemConfig::NUMBER_OF_UNITS]) {
        if (!takeMutex()) {
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
        
        giveMutex();
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
     * Main update loop
     */
    void update() {
        unsigned long currentMillis = millis();
        
        // Update water levels
        updateWaterLevelReadings();
        
        // Update atomizer states
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (systemState.unitsEnabled[i]) {
                updateAtomizerTiming(i, currentMillis);
            }
        }
        
        // Update unit data in Firebase
        updateUnitData();
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
        
        if (!takeMutex()) {
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
        
        giveMutex();
        return success;
    }
    
    /**
     * Reads water level for all units
     */
    void readWaterLevel() {
        updateWaterLevelReadings();
    }
};

#endif // UNIT_MANAGER_H 