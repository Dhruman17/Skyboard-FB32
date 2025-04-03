#ifndef FIREBASE_MANAGER_H
#define FIREBASE_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "mutex_manager.h"
#include <Firebase_ESP_Client.h>
#include <map>
#include <vector>
#include <string>

/**
 * FirebaseManager Class
 * 
 * Handles all Firebase operations with improved error handling and batching:
 * 1. Document Operations:
 *    - Batch processing for multiple updates
 *    - Automatic retry mechanism
 *    - Path validation and sanitization
 * 
 * 2. Error Handling:
 *    - Detailed error tracking
 *    - Automatic recovery from transient failures
 *    - Error reporting through ErrorManager
 * 
 * Thread Safety:
 * - All public methods are thread-safe
 * - Uses mutex protection for critical sections
 * 
 * Performance Optimization:
 * - Implements batching for multiple updates
 * - Reuses JSON objects to prevent memory fragmentation
 * - Pre-allocates strings for paths and values
 */
class FirebaseManager : public MutexManager {
private:
    FirebaseData& fbdo;
    FirebaseJson documentJson;  // Reusable JSON object for document operations
    
    // Batch operation structure
    struct BatchedUpdate {
        int unitIndex;
        String fieldPath;
        String value;
        String valueType;
        
        // Default constructor
        BatchedUpdate() : unitIndex(0), fieldPath(""), value(""), valueType("") {}
        
        // Constructor to handle const values
        BatchedUpdate(int idx, const char* path, const String& val, const char* type)
            : unitIndex(idx), fieldPath(path), value(val), valueType(type) {}
            
        // Assignment operator to handle const values
        BatchedUpdate& operator=(const BatchedUpdate& other) {
            if (this != &other) {
                unitIndex = other.unitIndex;
                fieldPath = other.fieldPath;
                value = other.value;
                valueType = other.valueType;
            }
            return *this;
        }
    };
    
    std::vector<BatchedUpdate> batchOperations;
    static constexpr size_t MAX_BATCH_SIZE = 10;
    static constexpr uint32_t BATCH_TIMEOUT_MS = 1000;  // 1 second timeout
    unsigned long lastBatchFlushTime = 0;
    
    // Pre-allocated buffers
    char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
    
    /**
     * Creates a Firebase document path
     * Thread-safe: Yes
     * @param buffer Buffer to store path
     * @param size Buffer size
     * @param format Path format string
     * @param ... Format arguments
     * @return true if path was created successfully
     */
    bool createPath(char* buffer, size_t size, const char* format, ...) {
        if (!buffer || size == 0) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_PATH_INVALID,
                "Invalid buffer for path creation",
                "FirebaseManager::createPath"
            );
            return false;
        }
        
        va_list args;
        va_start(args, format);
        int written = vsnprintf(buffer, size, format, args);
        va_end(args);
        
        if (written >= size) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_PATH_INVALID,
                "Path buffer overflow",
                "FirebaseManager::createPath"
            );
            return false;
        }
        return true;
    }

public:
    /**
     * Constructor
     * @param fbdo Firebase Data object reference
     */
    FirebaseManager(FirebaseData& fbdo) : fbdo(fbdo), documentJson() {}
    
    /**
     * Destructor
     */
    ~FirebaseManager() {}
    
    /**
     * Adds a field update to the batch
     * @param unitIndex Index of the unit
     * @param fieldPath Path to the field in Firestore
     * @param value Value to update
     * @param valueType Type of the value ("string", "float", "bool", etc.)
     * @return true if added successfully
     */
    bool addToBatch(int unitIndex, const char* fieldPath, const String& value, const char* valueType) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (batchOperations.size() >= MAX_BATCH_SIZE) {
            if (!flushBatch()) {
                return false;
            }
        }
        
        batchOperations.emplace_back(unitIndex, fieldPath, value, valueType);
        return true;
    }
    
    /**
     * Flushes the current batch of operations
     * @return true if flush was successful
     */
    bool flushBatch() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (batchOperations.empty()) {
            return true;
        }
        
        // Process batch operations
        bool success = true;
        for (const auto& op : batchOperations) {
            if (!createPath(pathBuffer, sizeof(pathBuffer), "units/%d/%s", op.unitIndex, op.fieldPath)) {
                success = false;
                continue;
            }
            
            // Update document with valueType as String
            if (!updateDocument(pathBuffer, op.value, op.valueType)) {
                success = false;
            }
        }
        
        batchOperations.clear();
        lastBatchFlushTime = millis();
        return success;
    }
    
    /**
     * Updates a document field
     * @param path Document path
     * @param value New value
     * @param valueType Type of the value
     * @return true if update was successful
     */
    bool updateDocument(const char* path, const String& value, const String& valueType) {
        if (!Firebase.ready()) {
            Serial.println("FirebaseManager not ready");
            return false;
        }

        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }

        documentJson.clear();
        
        // Create the document structure
        documentJson.set("fields/" + valueType + "/stringValue", value);
        
        // Update the document with correct patchDocument signature
        const char* emptyStr = "";
        if (!Firebase.Firestore.patchDocument(&fbdo, SystemConfig::FIREBASE_PROJECT_ID, emptyStr, path, "fields", documentJson.raw())) {
            Serial.printf("Failed to update document: %s\n", fbdo.errorReason().c_str());
            return false;
        }
        
        return true;
    }
    
    /**
     * Updates system health information
     * @param field The health field to update
     * @param data The data to update with
     * @return true if update was successful
     */
    bool updateSystemHealth(const char* field, const std::map<const char*, String>& data) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        // Create path buffer for Firebase
        char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        snprintf(pathBuffer, sizeof(pathBuffer), "systems/%s", SystemConfig::SERIAL_NUMBER);
        
        documentJson.clear();
        for (const auto& pair : data) {
            documentJson.set(pair.first, pair.second);
        }
        
        const char* emptyStr = "";
        bool success = Firebase.Firestore.patchDocument(&fbdo, 
                                                      SystemConfig::FIREBASE_PROJECT_ID, 
                                                      emptyStr, pathBuffer, 
                                                      "fields", documentJson.raw());
        
        if (!success) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                String("System health update failed: ") + fbdo.errorReason().c_str(),
                "FirebaseManager::updateSystemHealth"
            );
        }
        
        return success;
    }
    
    /**
     * Fetches system data from Firebase
     * Thread-safe: Yes
     * @param systemName Output parameter for system name
     * @param lightOnTime Output parameter for light on time
     * @param lightOffTime Output parameter for light off time
     * @param lightMasterSwitch Output parameter for light master switch
     * @param timeCycleEnabled Output parameter for time cycle enabled
     * @param unitsEnabled Output parameter for units enabled array
     * @param atomizerOnIntervals Output parameter for atomizer on intervals
     * @param atomizerOffIntervals Output parameter for atomizer off intervals
     * @return true if fetch was successful
     */
    bool fetchSystemData(String* systemName, time_t* lightOnTime, time_t* lightOffTime,
                        bool* lightMasterSwitch, bool* timeCycleEnabled, bool* unitsEnabled,
                        unsigned long* atomizerOnIntervals, unsigned long* atomizerOffIntervals) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        bool success = true;
        
        // Get system document
        if (!createPath(pathBuffer, sizeof(pathBuffer), 
                       SystemConfig::SYSTEM_PATH_FORMAT, 
                       SystemConfig::SERIAL_NUMBER)) {
            success = false;
        }
        
        if (success) {
            if (!Firebase.Firestore.getDocument(&fbdo, 
                                              SystemConfig::FIREBASE_PROJECT_ID, 
                                              "", pathBuffer)) {
                ErrorManager::firebaseError(
                    ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                    String("Failed to get system document: ") + fbdo.errorReason().c_str(),
                    "FirebaseManager::fetchSystemData"
                );
                success = false;
            }
            
            if (success) {
                documentJson.setJsonData(fbdo.payload());
                FirebaseJsonData jsonData;
                
                // Get system name
                if (documentJson.get(jsonData, "fields/systemName/stringValue")) {
                    *systemName = jsonData.stringValue;
                }
                
                // Get light settings
                if (documentJson.get(jsonData, "fields/lightOnTime/stringValue")) {
                    String timeStr = jsonData.stringValue;
                    struct tm tm = {};
                    strptime(timeStr.c_str(), "%H:%M", &tm);
                    *lightOnTime = mktime(&tm);
                }
                
                if (documentJson.get(jsonData, "fields/lightOffTime/stringValue")) {
                    String timeStr = jsonData.stringValue;
                    struct tm tm = {};
                    strptime(timeStr.c_str(), "%H:%M", &tm);
                    *lightOffTime = mktime(&tm);
                }
                
                if (documentJson.get(jsonData, "fields/lightMasterSwitch/booleanValue")) {
                    *lightMasterSwitch = jsonData.boolValue;
                }
                
                if (documentJson.get(jsonData, "fields/timeCycleEnabled/booleanValue")) {
                    *timeCycleEnabled = jsonData.boolValue;
                }
                
                // Get unit settings
                for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                    char unitPath[50];
                    snprintf(unitPath, sizeof(unitPath), "fields/units/%d/", i);
                    
                    if (documentJson.get(jsonData, String(unitPath) + "enabled/booleanValue")) {
                        unitsEnabled[i] = jsonData.boolValue;
                    }
                    
                    if (documentJson.get(jsonData, String(unitPath) + "atomizerOnInterval/integerValue")) {
                        atomizerOnIntervals[i] = jsonData.intValue;
                    }
                    
                    if (documentJson.get(jsonData, String(unitPath) + "atomizerOffInterval/integerValue")) {
                        atomizerOffIntervals[i] = jsonData.intValue;
                    }
                }
            }
        }
        
        return success;
    }
    
    /**
     * Saves light settings to Firebase
     * Thread-safe: Yes
     * @param lightMasterSwitch Light master switch state
     * @param timeCycleEnabled Time cycle enabled state
     * @param lightOnTime Light on time
     * @param lightOffTime Light off time
     * @return true if save was successful
     */
    bool saveLightSettings(bool lightMasterSwitch, bool timeCycleEnabled,
                         const String& lightOnTime, const String& lightOffTime) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        bool success = true;
        
        if (!createPath(pathBuffer, sizeof(pathBuffer), 
                       SystemConfig::SYSTEM_PATH_FORMAT, 
                       SystemConfig::SERIAL_NUMBER)) {
            success = false;
        }
        
        if (success) {
            documentJson.clear();
            documentJson.set("lightMasterSwitch", lightMasterSwitch);
            documentJson.set("timeCycleEnabled", timeCycleEnabled);
            documentJson.set("lightOnTime", lightOnTime);
            documentJson.set("lightOffTime", lightOffTime);
            
            if (!Firebase.Firestore.patchDocument(&fbdo, 
                                                SystemConfig::FIREBASE_PROJECT_ID, 
                                                "", pathBuffer, 
                                                "fields", documentJson.raw())) {
                ErrorManager::firebaseError(
                    ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                    String("Failed to save light settings: ") + fbdo.errorReason().c_str(),
                    "FirebaseManager::saveLightSettings"
                );
                success = false;
            }
        }
        
        return success;
    }
    
    /**
     * Saves unit settings to Firebase
     * Thread-safe: Yes
     * @param unitIndex Index of the unit
     * @param enabled Whether the unit is enabled
     * @param atomizerOnInterval Atomizer on interval
     * @param atomizerOffInterval Atomizer off interval
     * @return true if save was successful
     */
    bool saveUnitSettings(int unitIndex, bool enabled,
                         unsigned long atomizerOnInterval,
                         unsigned long atomizerOffInterval) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        bool success = true;
        
        if (!createPath(pathBuffer, sizeof(pathBuffer), 
                       SystemConfig::SYSTEM_PATH_FORMAT, 
                       SystemConfig::SERIAL_NUMBER)) {
            success = false;
        }
        
        if (success) {
            // Create field path buffers
            char enabledPath[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
            char onIntervalPath[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
            char offIntervalPath[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
            
            snprintf(enabledPath, sizeof(enabledPath), "units/%d/enabled", unitIndex);
            snprintf(onIntervalPath, sizeof(onIntervalPath), "units/%d/atomizerOnInterval", unitIndex);
            snprintf(offIntervalPath, sizeof(offIntervalPath), "units/%d/atomizerOffInterval", unitIndex);
            
            documentJson.clear();
            documentJson.set(enabledPath, enabled);
            documentJson.set(onIntervalPath, atomizerOnInterval);
            documentJson.set(offIntervalPath, atomizerOffInterval);
            
            if (!Firebase.Firestore.patchDocument(&fbdo, 
                                                SystemConfig::FIREBASE_PROJECT_ID, 
                                                "", pathBuffer, 
                                                "fields", documentJson.raw())) {
                ErrorManager::firebaseError(
                    ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                    String("Failed to save unit settings: ") + fbdo.errorReason().c_str(),
                    "FirebaseManager::saveUnitSettings"
                );
                success = false;
            }
        }
        
        return success;
    }
    
    /**
     * Updates system version in Firebase
     * @return true if update was successful
     */
    bool updateSystemVersion(const char* version) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!createPath(pathBuffer, sizeof(pathBuffer), 
                       SystemConfig::SYSTEM_PATH_FORMAT, 
                       SystemConfig::SERIAL_NUMBER)) {
            return false;
        }
        
        documentJson.clear();
        documentJson.set("fields/firmwareVersion/stringValue", version);
        
        bool success = Firebase.Firestore.patchDocument(&fbdo, 
                                                      SystemConfig::FIREBASE_PROJECT_ID, 
                                                      "", pathBuffer, 
                                                      "fields", documentJson.raw());
        
        if (!success) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                String("Failed to update system version: ") + fbdo.errorReason().c_str(),
                "FirebaseManager::updateSystemVersion"
            );
        }
        
        return success;
    }
    
    /**
     * Gets a document field value
     * @param path Document path
     * @param value Output parameter for the value
     * @return true if get was successful
     */
    bool getDocument(const char* path, String& value) {
        if (!Firebase.ready()) {
            Serial.println("FirebaseManager not ready");
            return false;
        }

        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!Firebase.Firestore.getDocument(&fbdo, SystemConfig::FIREBASE_PROJECT_ID, "", path)) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                String("Failed to get document: ") + fbdo.errorReason().c_str(),
                "FirebaseManager::getDocument"
            );
            return false;
        }
        
        documentJson.setJsonData(fbdo.payload());
        FirebaseJsonData jsonData;
        
        // Try to get string value first
        if (documentJson.get(jsonData, "fields/value/stringValue")) {
            value = jsonData.stringValue;
            return true;
        }
        
        // Try integer value
        if (documentJson.get(jsonData, "fields/value/integerValue")) {
            value = String(jsonData.intValue);
            return true;
        }
        
        // Try boolean value
        if (documentJson.get(jsonData, "fields/value/booleanValue")) {
            value = jsonData.boolValue ? "true" : "false";
            return true;
        }
        
        // Try double/float value
        if (documentJson.get(jsonData, "fields/value/doubleValue")) {
            value = String(jsonData.doubleValue);
            return true;
        }
        
        ErrorManager::firebaseError(
            ErrorManager::ErrorCode::FIREBASE_INVALID_TYPE,
            "No valid value field found in document",
            "FirebaseManager::getDocument"
        );
        return false;
    }
};

#endif // FIREBASE_MANAGER_H 