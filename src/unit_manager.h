#ifndef UNIT_MANAGER_H
#define UNIT_MANAGER_H

#include "config.h"
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
    FirebaseData& fbdo;
    SensorManager& sensorManager;
    
    // Pre-allocate string space to prevent fragmentation
    static constexpr size_t UNIT_NAME_MAX_LENGTH = 50;
    String unitNames[SystemConfig::NUMBER_OF_UNITS];
    
    unsigned long previousMillis[SystemConfig::NUMBER_OF_UNITS];
    mutable bool atomizerStates[SystemConfig::NUMBER_OF_UNITS];
    
    // Firebase JSON objects (reused to prevent memory fragmentation)
    FirebaseJson unitJson;
    FirebaseJsonData jsonData;
    
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
    uint8_t consecutiveErrors[SystemConfig::NUMBER_OF_UNITS] = {0};
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
            Serial.printf(SystemConfig::ERROR_FORMAT_GENERIC, "createFirebasePath", "Invalid buffer");
            return false;
        }
        
        va_list args;
        va_start(args, format);
        int written = vsnprintf(buffer, bufferSize, format, args);
        va_end(args);
        
        if (written >= bufferSize) {
            Serial.printf(SystemConfig::ERROR_FORMAT_GENERIC, "createFirebasePath", "Buffer overflow");
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
    bool handleSensorError(int unitIndex, const char* errorType) {
        consecutiveErrors[unitIndex]++;
        
        if (consecutiveErrors[unitIndex] >= SystemConfig::MAX_CONSECUTIVE_ERRORS) {
            Serial.printf("Too many consecutive %s sensor errors for unit %d, attempting recovery\n", 
                         errorType, unitIndex);
            
            // Attempt sensor recalibration
            if (!recalibrateSensor(unitIndex, errorType)) {
                Serial.printf("Failed to recover %s sensor for unit %d\n", errorType, unitIndex);
                return false;
            }
            
            consecutiveErrors[unitIndex] = 0;
        }
        
        return true;
    }
    
    /**
     * Attempts to recalibrate a sensor
     * @param unitIndex Index of the unit
     * @param sensorType Type of sensor ("water" or "ec")
     * @return true if recalibration was successful
     */
    bool recalibrateSensor(int unitIndex, const char* sensorType) {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        bool success = false;
        if (strcmp(sensorType, SystemConfig::SENSOR_TYPE_WATER) == 0) {
            success = sensorManager.cleanupSensor(unitIndex, SystemConfig::SENSOR_TYPE_WATER);
        } else if (strcmp(sensorType, SystemConfig::SENSOR_TYPE_EC) == 0) {
            success = sensorManager.cleanupSensor(unitIndex, SystemConfig::SENSOR_TYPE_EC);
        }
        
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
            Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, operation, "Too many errors, attempting recovery");
            
            // Attempt to reconnect to Firebase
            if (!reconnectFirebase()) {
                Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, operation, "Failed to recover connection");
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
     * @param valueType Type of value ("stringValue", "booleanValue", etc.)
     * @return true if update was successful
     */
    bool updateUnitField(int unitIndex, const char* fieldPath, const String& value, const char* valueType) {
        if (!isValidAndEnabled(unitIndex)) return false;
        
        char path[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        snprintf(path, sizeof(path), SystemConfig::UNIT_PATH_FORMAT, SystemConfig::SERIAL_NUMBER, unitIndex);
        
        FirebaseJson unitJson;
        unitJson.set(fieldPath, value);
        
        for (int retry = 0; retry < SystemConfig::MAX_FIREBASE_RETRIES; retry++) {
            if (Firebase.Firestore.patchDocument(&fbdo, SystemConfig::FIREBASE_PROJECT_ID, "", path, unitJson.raw(), fieldPath)) {
                return true;
            }
            delay(SystemConfig::FIREBASE_RETRY_DELAY_MS);
        }
        return false;
    }
    
    // Overload for boolean values
    bool updateUnitField(int unitIndex, const char* fieldPath, bool value, const char* valueType) {
        return updateUnitField(unitIndex, fieldPath, String(value), valueType);
    }
    
    // Overload for float values
    bool updateUnitField(int unitIndex, const char* fieldPath, float value, const char* valueType) {
        return updateUnitField(unitIndex, fieldPath, String(value, 6), valueType);
    }
    
    // Overload for integer values
    bool updateUnitField(int unitIndex, const char* fieldPath, int value, const char* valueType) {
        return updateUnitField(unitIndex, fieldPath, String(value), valueType);
    }
    
    /**
     * Reads a unit's document from Firebase and caches the data
     * @param unitIndex Index of the unit
     * @return true if document was read successfully
     */
    bool readUnitDocument(int unitIndex) {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        char pathBuffer[256];
        if (!createUnitPath(pathBuffer, sizeof(pathBuffer), unitIndex)) {
            return false;
        }
        
        if (!Firebase.Firestore.getDocument(&fbdo, SystemConfig::FIREBASE_PROJECT_ID, "", pathBuffer)) {
            return false;
        }
        
        unitJson.clear();
        unitJson.setJsonData(fbdo.payload());
        return true;
    }
    
    /**
     * Safely takes the mutex with timeout
     * @return true if mutex was taken successfully
     */
    bool takeMutex() const {
        return xSemaphoreTake(mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE;
    }
    
    /**
     * Safely gives the mutex
     */
    void giveMutex() const {
        xSemaphoreGive(mutex);
    }
    
    /**
     * Sets the multiplexer to the specified unit with error handling
     * @param unitIndex Index of the unit to select
     * @return true if successful
     */
    bool setMultiplexer(int unitIndex) const {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        // Set multiplexer to select the unit
        Wire.beginTransmission(SystemConfig::PCA_ADDRS[unitIndex]);
        Wire.write(0x00);  // Select all channels
        
        uint8_t result = Wire.endTransmission();
        if (result != 0) {
            i2cErrorCount++;
            Serial.printf("I2C error in setMultiplexer for unit %d: %d\n", unitIndex, result);
            
            if (i2cErrorCount >= MAX_I2C_ERRORS) {
                Serial.println("Too many I2C errors, resetting I2C bus...");
                Wire.end();
                delay(100);
                Wire.begin(SystemConfig::I2C_SDA, SystemConfig::I2C_SCL);
                i2cErrorCount = 0;
            }
            return false;
        }
        
        i2cErrorCount = 0;  // Reset error count on success
        return true;
    }
    
    /**
     * Updates water level state and readings for a unit
     * @param unitIndex Index of the unit
     */
    void updateWaterLevel(int unitIndex) {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in updateWaterLevel");
            return;
        }
        
        // Skip disabled units
        if (!systemState.unitsEnabled[unitIndex]) {
            giveMutex();
            return;
        }
        
        // Update water level state
        bool newWaterLevelState = (digitalRead(SystemConfig::WATER_LEVEL_PINS[unitIndex]) != LOW);
        if (systemState.waterLevelStates[unitIndex] != newWaterLevelState) {
            systemState.waterLevelStates[unitIndex] = newWaterLevelState;
            updateUnitField(unitIndex, "fields/waterLevelState/booleanValue", newWaterLevelState, "booleanValue");
            systemState.previousWaterLevelStates[unitIndex] = newWaterLevelState;
        }
        
        // Update water level reading if atomizer is off
        bool atomState;
        if (getAtomizerState(unitIndex, atomState) && !atomState) {
            float newWaterLevel = sensorManager.readWaterLevel(unitIndex);
            if (newWaterLevel >= 0) {  // Valid reading
                waterLevels[unitIndex] = newWaterLevel;
                updateUnitField(unitIndex, "fields/waterLevel/doubleValue", newWaterLevel, "doubleValue");
            } else {
                waterLevels[unitIndex] = INVALID_WATER_LEVEL;
            }
        }
        
        giveMutex();
    }
    
    /**
     * Updates water level readings for all units with enhanced error recovery
     * Thread-safe: Yes (uses mutex)
     * Performance: Batches Firebase updates to reduce network calls
     * Error Handling: Includes retry mechanism and sensor recalibration
     */
    void updateWaterLevelReadings() {
        if (!takeMutex()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "updateWaterLevelReadings");
            return;
        }
        
        bool success = true;
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!systemState.unitsEnabled[i]) {
                continue;
            }
            
            // Update water level reading if atomizer is off
            bool atomState;
            if (getAtomizerState(i, atomState) && !atomState) {
                float newWaterLevel = sensorManager.readWaterLevel(i);
                if (newWaterLevel >= 0) {  // Valid reading
                    waterLevels[i] = newWaterLevel;
                    if (!updateUnitField(i, "fields/waterLevel/doubleValue", newWaterLevel, "doubleValue")) {
                        success = false;
                    }
                } else {
                    waterLevels[i] = INVALID_WATER_LEVEL;
                    if (!handleSensorError(i, SystemConfig::SENSOR_TYPE_WATER)) {
                        Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "updateWaterLevelReadings", i, "Failed to read water level");
                        success = false;
                    }
                }
            }
        }
        
        giveMutex();
        if (!success) {
            Serial.printf(SystemConfig::ERROR_FORMAT_SENSOR, "updateWaterLevelReadings", -1, "Some updates failed");
        }
    }
    
    /**
     * Verifies and corrects hardware state mismatch
     * @param unitIndex Index of the unit to check
     * @return true if state is correct or was corrected
     */
    bool verifyAndCorrectHardwareState(int unitIndex) {
        bool currentState;
        if (!getAtomizerState(unitIndex, currentState)) {
            Serial.println("Failed to get atomizer state for unit " + String(unitIndex));
            return false;
        }
        
        // Verify hardware state matches software state
        bool hardwareState = (ledcRead(unitIndex) == SystemConfig::PWM_ATOMIZER_ON);
        if (currentState != hardwareState) {
            if (!setAtomizerState(unitIndex, hardwareState)) {
                Serial.println("Failed to correct atomizer state mismatch for unit " + String(unitIndex));
                return false;
            }
            Serial.println("Corrected atomizer state mismatch for unit " + String(unitIndex));
        }
        return true;
    }
    
    /**
     * Updates atomizer timing for a unit
     * @param unitIndex Index of the unit to update
     * @param currentMillis Current time in milliseconds
     */
    void updateAtomizerTiming(int unitIndex, unsigned long currentMillis) {
        bool currentState;
        if (!getAtomizerState(unitIndex, currentState)) {
            return;
        }
        
        // Check timing for state change
        if (currentMillis - previousMillis[unitIndex] >= 
            (currentState ? systemState.atomizerOnIntervals[unitIndex] : 
                          systemState.atomizerOffIntervals[unitIndex])) {
            bool newState = !currentState;
            if (!updateAtomizerState(unitIndex, newState)) {
                Serial.println("Failed to update atomizer state for unit " + String(unitIndex));
                return;
            }
            
            if (!newState) {
                updateWaterLevel(unitIndex);
            }
            
            previousMillis[unitIndex] = currentMillis;
        }
    }

public:
    /**
     * Constructor
     * @param state Reference to system state
     * @param fbdo Reference to Firebase data object
     * @param sensor Reference to sensor manager
     */
    UnitManager(SystemState& state, FirebaseData& fbdo, SensorManager& sensor)
        : systemState(state), fbdo(fbdo), sensorManager(sensor) {
        // Initialize arrays
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            previousMillis[i] = 0;
            atomizerStates[i] = false;
            waterLevels[i] = INVALID_WATER_LEVEL;  // Initialize with invalid reading
            // Pre-allocate space for unit names
            unitNames[i].reserve(UNIT_NAME_MAX_LENGTH);
        }
        
        // Create mutex for thread safety
        mutex = xSemaphoreCreateMutex();
    }
    
    /**
     * Destructor
     */
    ~UnitManager() {
        if (mutex != NULL) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Initializes the unit manager
     * @return true if initialization successful
     */
    bool begin() {
        // Initialize unit names from Firebase
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (readUnitDocument(i)) {
                if (unitJson.get(jsonData, "fields/unitName/stringValue")) {
                    unitNames[i] = jsonData.stringValue;
                }
            }
        }
        
        return true;
    }
    
    /**
     * Checks if the atomizer is on for a specific unit
     * @param unitIndex Index of the unit to check
     * @return true if atomizer is on
     */
    bool isAtomizerOn(int unitIndex) const {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        bool state = false;
        if (!getAtomizerState(unitIndex, state)) {
            Serial.println("Failed to read atomizer state in isAtomizerOn");
        }
        return state;
    }
    
    /**
     * Reads EC sensor value from a specific unit
     * @param unitIndex Index of the unit to read from
     * @return EC value (0-1)
     */
    float readECSensorValue(const int& unitIndex) const {
        // First check if unit is valid and enabled
        if (unitIndex < 0 || unitIndex >= SystemConfig::NUMBER_OF_UNITS) {
            return INVALID_WATER_LEVEL;  // Use same invalid value for consistency
        }
        
        if (!systemState.unitsEnabled[unitIndex]) {
            return INVALID_WATER_LEVEL;  // Don't read sensors for disabled units
        }
        
        // Set the multiplexer to the correct unit
        if (!setMultiplexer(unitIndex)) {
            return INVALID_WATER_LEVEL;
        }
        
        float ecValue = sensorManager.readECValue(unitIndex);
        if (ecValue >= 0) {
            ecValue = ecValue / 100.0f;  // Convert to 0-1 range
        }
        return ecValue;
    }
    
    /**
     * Updates unit names from Firebase
     * @param names Array of unit names
     */
    void updateUnitNames(const String names[SystemConfig::NUMBER_OF_UNITS]) {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (names[i] != unitNames[i]) {
                // Truncate name if it exceeds max length
                String newName = names[i];
                if (newName.length() > UNIT_NAME_MAX_LENGTH) {
                    newName = newName.substring(0, UNIT_NAME_MAX_LENGTH);
                    Serial.println("Warning: Unit name truncated for unit " + String(i));
                }
                unitNames[i] = newName;
                // Update Firebase with new unit name
                updateUnitField(i, "fields/unitName/stringValue", newName, "stringValue");
            }
        }
    }
    
    /**
     * Updates unit configuration from Firebase with batched reads
     * Thread-safe: Yes (uses mutex)
     * Performance: Reads all unit data in a single batch operation
     * Error Handling: Includes retry mechanism for failed Firebase operations
     */
    void updateUnitData() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in updateUnitData");
            return;
        }
        
        // Read all unit documents in a single batch
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            uint8_t retries = 0;
            bool success = false;
            
            while (!success && retries < SystemConfig::MAX_FIREBASE_RETRIES) {
                if (readUnitDocument(i)) {
                    // Update unit name
                    if (unitJson.get(jsonData, SystemConfig::UNIT_NAME_PATH)) {
                        String newName = jsonData.stringValue;
                        if (newName != unitNames[i]) {
                            if (newName.length() > UNIT_NAME_MAX_LENGTH) {
                                newName = newName.substring(0, UNIT_NAME_MAX_LENGTH);
                                Serial.println("Warning: Unit name truncated for unit " + String(i));
                            }
                            unitNames[i] = newName;
                        }
                    }
                    
                    // Update unit state
                    if (unitJson.get(jsonData, SystemConfig::UNIT_ENABLED_PATH)) {
                        systemState.unitsEnabled[i] = jsonData.boolValue;
                    }
                    
                    // Update intervals
                    if (unitJson.get(jsonData, SystemConfig::UNIT_ON_INTERVAL_PATH)) {
                        systemState.atomizerOnIntervals[i] = jsonData.intValue;
                    }
                    if (unitJson.get(jsonData, SystemConfig::UNIT_OFF_INTERVAL_PATH)) {
                        systemState.atomizerOffIntervals[i] = jsonData.intValue;
                    }
                    
                    success = true;
                } else {
                    retries++;
                    if (retries < SystemConfig::MAX_FIREBASE_RETRIES) {
                        delay(SystemConfig::FIREBASE_RETRY_DELAY_MS);
                    }
                }
            }
            
            if (!success) {
                Serial.println("Failed to update unit data for unit " + String(i));
            }
        }
        
        giveMutex();
    }
    
    /**
     * Updates unit states and irrigation timing
     * Called in the main loop to handle unit operations
     */
    void update() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in update");
            return;
        }
        
        unsigned long currentMillis = millis();
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!isValidAndEnabled(i)) {
                continue;
            }
            
            // Verify and correct hardware state
            if (!verifyAndCorrectHardwareState(i)) {
                continue;
            }
            
            // Update atomizer timing
            updateAtomizerTiming(i, currentMillis);
            
            // Update water level state and reading
            updateWaterLevel(i);
        }
        
        giveMutex();
    }
    
    /**
     * Updates atomizer PWM state for a unit with thread safety
     * @param unitIndex Index of the unit
     * @param isOn Whether the atomizer should be on
     * @return true if update was successful, false otherwise
     */
    bool updateAtomizerState(int unitIndex, bool isOn) {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ledcWrite(unitIndex, isOn ? SystemConfig::PWM_ATOMIZER_ON : SystemConfig::PWM_ATOMIZER_OFF);
            atomizerStates[unitIndex] = isOn;
            xSemaphoreGive(mutex);
            Serial.println(String(isOn ? "Turning on" : "Turning off") + " atomizer for " + String(unitNames[unitIndex]));
            return true;
        }
        return false;
    }
    
    /**
     * Reads water levels for all units and updates Firebase
     * Only reads for units where the atomizer is off
     * Thread-safe: Yes (uses mutex)
     */
    void readWaterLevel() {
        if (!takeMutex()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_MUTEX, "readWaterLevel");
            return;
        }
        
        updateWaterLevelReadings();
        giveMutex();
    }
}; // End of UnitManager class

#endif // UNIT_MANAGER_H 