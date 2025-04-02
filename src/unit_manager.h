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
    String unitNames[SystemConfig::NUMBER_OF_UNITS];
    unsigned long previousMillis[SystemConfig::NUMBER_OF_UNITS];
    bool atomizerStates[SystemConfig::NUMBER_OF_UNITS];
    
    // Firebase JSON objects (reused to prevent memory fragmentation)
    FirebaseJson unitJson;
    FirebaseJsonData jsonData;
    
    // Reusable buffer for water level readings
    float waterLevels[SystemConfig::NUMBER_OF_UNITS];
    
    // Firebase path format
    static constexpr const char* UNIT_PATH_FORMAT = "%s/units/%d";
    
    // Mutex for thread safety
    SemaphoreHandle_t mutex;
    
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
     * Updates atomizer PWM state for a unit with thread safety
     * @param unitIndex Index of the unit
     * @param isOn Whether the atomizer should be on
     */
    void updateAtomizerState(int unitIndex, bool isOn) {
        if (!isValidAndEnabled(unitIndex)) {
            return;
        }
        
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ledcWrite(unitIndex, isOn ? SystemConfig::PWM_ATOMIZER_ON : SystemConfig::PWM_ATOMIZER_OFF);
            atomizerStates[unitIndex] = isOn;
            xSemaphoreGive(mutex);
            Serial.println(String(isOn ? "Turning on" : "Turning off") + " atomizer for " + String(unitNames[unitIndex]));
        }
    }
    
    /**
     * Helper method to safely create Firebase paths
     * @param buffer Buffer to store the path
     * @param format Format string for snprintf
     * @param ... Additional arguments for snprintf
     * @return true if path was created successfully
     */
    bool createFirebasePath(char* buffer, size_t bufferSize, const char* format, ...) {
        va_list args;
        va_start(args, format);
        int written = vsnprintf(buffer, bufferSize, format, args);
        va_end(args);
        
        if (written >= bufferSize) {
            Serial.println("Error: Path buffer overflow");
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
        return createFirebasePath(buffer, bufferSize, UNIT_PATH_FORMAT, systemPath, unitIndex);
    }
    
    /**
     * Updates a unit's field in Firebase
     * @param unitIndex Index of the unit
     * @param fieldName Name of the field to update
     * @param value Value to set
     * @param type Type of the value (e.g., "doubleValue", "booleanValue")
     */
    template<typename T>
    void updateUnitField(int unitIndex, const char* fieldName, T value, const char* type) {
        if (!isValidAndEnabled(unitIndex)) {
            return;
        }
        
        char pathBuffer[50];
        if (!createUnitPath(pathBuffer, sizeof(pathBuffer), unitIndex)) {
            return;
        }
        
        // Clear previous data
        unitJson.clear();
        unitJson.set(fieldName, value);
        
        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", pathBuffer, unitJson.raw(), fieldName)) {
            Serial.println("Updated " + String(fieldName) + " for unit " + String(unitIndex));
        } else {
            Serial.println("Failed to update " + String(fieldName) + " for unit " + String(unitIndex));
        }
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
        
        char pathBuffer[50];
        if (!createUnitPath(pathBuffer, sizeof(pathBuffer), unitIndex)) {
            return false;
        }
        
        if (!Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", pathBuffer)) {
            return false;
        }
        
        unitJson.clear();
        unitJson.setJsonData(fbdo.payload());
        return true;
    }
    
    /**
     * Sets the multiplexer to the specified unit
     * @param unitIndex Index of the unit to select
     * @return true if successful
     */
    bool setMultiplexer(int unitIndex) {
        if (!isValidAndEnabled(unitIndex)) {
            return false;
        }
        // Set multiplexer to select the unit
        Wire.beginTransmission(SystemConfig::PCA_ADDRS[unitIndex]);
        Wire.write(0x00);  // Select all channels
        return Wire.endTransmission() == 0;
    }
    
    /**
     * Updates water level state and readings for a unit
     * @param unitIndex Index of the unit
     */
    void updateWaterLevel(int unitIndex) {
        if (!isValidAndEnabled(unitIndex)) {
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
        if (!atomizerStates[unitIndex]) {
            float newWaterLevel = sensorManager.readWaterLevel(unitIndex);
            if (newWaterLevel >= 0) {  // Valid reading
                waterLevels[unitIndex] = newWaterLevel;
                updateUnitField(unitIndex, "fields/waterLevel/doubleValue", newWaterLevel, "doubleValue");
            }
        }
    }
    
    /**
     * Updates water level readings for all units
     * Only reads for units where the atomizer is off
     */
    void updateWaterLevelReadings() {
        // Read water levels for all units at once if possible
        if (sensorManager.readAllWaterLevels(waterLevels)) {
            for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                if (isValidAndEnabled(i) && !atomizerStates[i]) {
                    updateUnitField(i, "fields/waterLevel/doubleValue", waterLevels[i], "doubleValue");
                }
            }
        } else {
            // Fallback to reading units individually
            for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                updateWaterLevel(i);
            }
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
            waterLevels[i] = 0.0f;
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
        
        bool state;
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            state = atomizerStates[unitIndex];
            xSemaphoreGive(mutex);
        } else {
            state = false;
        }
        return state;
    }
    
    /**
     * Reads EC sensor value from a specific unit
     * @param unitIndex Index of the unit to read from
     * @return EC value (0-1)
     */
    float readECSensorValue(const int& unitIndex) {
        if (!isValidAndEnabled(unitIndex)) {
            return 0.0f;
        }
        
        // Set the multiplexer to the correct unit
        if (!setMultiplexer(unitIndex)) {
            return 0.0f;
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
                unitNames[i] = names[i];
                // Update Firebase with new unit name
                updateUnitField(i, "fields/unitName/stringValue", names[i], "stringValue");
            }
        }
    }
    
    /**
     * Updates unit configuration from Firebase
     * Reads unit state and irrigation intervals
     */
    void updateUnitData() {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (!readUnitDocument(i)) {
                continue;
            }
            
            bool stateChanged = false;
            if (unitJson.get(jsonData, "fields/unitState/booleanValue")) {
                bool newState = jsonData.boolValue;
                if (systemState.unitsEnabled[i] != newState) {
                    systemState.unitsEnabled[i] = newState;
                    stateChanged = true;
                }
            }
            
            if (unitJson.get(jsonData, "fields/Interval_On/integerValue")) {
                systemState.atomizerOnIntervals[i] = jsonData.intValue * 1000;
            }
            
            if (unitJson.get(jsonData, "fields/Interval_Off/integerValue")) {
                systemState.atomizerOffIntervals[i] = jsonData.intValue * 1000;
            }
            
            // Update atomizer state if unit state changed
            if (stateChanged) {
                updateAtomizerState(i, systemState.unitsEnabled[i]);
            }
        }
    }
    
    /**
     * Updates unit states and irrigation timing
     * Called in the main loop to handle unit operations
     */
    void update() {
        unsigned long currentMillis = millis();
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (isValidAndEnabled(i)) {
                if (currentMillis - previousMillis[i] >= (atomizerStates[i] ? systemState.atomizerOnIntervals[i] : systemState.atomizerOffIntervals[i])) {
                    bool newState = !atomizerStates[i];
                    updateAtomizerState(i, newState);
                    
                    if (!newState) {
                        updateWaterLevel(i);
                    }
                    
                    previousMillis[i] = currentMillis;
                }
            }
        }
    }
    
    /**
     * Reads water levels for all units and updates Firebase
     * Only reads for units where the atomizer is off
     */
    void readWaterLevel() {
        updateWaterLevelReadings();
    }
};

#endif // UNIT_MANAGER_H 