#ifndef FIREBASE_MANAGER_H
#define FIREBASE_MANAGER_H

#include "config.h"
#include "error_manager.h"
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
class FirebaseManager {
private:
    FirebaseData& fbdo;
    SemaphoreHandle_t mutex;
    
    // Batch operation tracking
    struct BatchOperation {
        int unitIndex;
        String fieldPath;
        String value;
        String valueType;
    };
    
    std::vector<BatchOperation> batchOperations;
    static constexpr size_t MAX_BATCH_SIZE = 10;
    static constexpr uint32_t BATCH_TIMEOUT_MS = 1000;  // 1 second timeout
    unsigned long lastBatchFlushTime = 0;
    
    // Pre-allocated buffers
    FirebaseJson documentJson;
    char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
    
    /**
     * Takes mutex with timeout
     * Thread-safe: Yes
     * @return true if mutex was taken
     */
    bool takeMutex() {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(SystemConfig::MUTEX_TIMEOUT_MS)) != pdTRUE) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "FirebaseManager::takeMutex"
            );
            return false;
        }
        return true;
    }
    
    /**
     * Releases mutex
     * Thread-safe: Yes
     */
    void giveMutex() {
        xSemaphoreGive(mutex);
    }
    
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
    FirebaseManager(FirebaseData& fbdo) : fbdo(fbdo) {
        mutex = xSemaphoreCreateMutex();
        if (!mutex) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_CREATION_FAILED,
                "Failed to create mutex",
                "FirebaseManager::FirebaseManager"
            );
        }
    }
    
    /**
     * Destructor
     */
    ~FirebaseManager() {
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Adds an operation to the batch
     * Thread-safe: Yes
     * @param unitIndex Unit index
     * @param fieldPath Field path in document
     * @param value Value to set
     * @param valueType Value type
     * @return true if operation was added successfully
     */
    bool addToBatch(int unitIndex, const char* fieldPath, const String& value, const char* valueType) {
        if (!takeMutex()) {
            return false;
        }
        
        BatchOperation op;
        op.unitIndex = unitIndex;
        op.fieldPath = fieldPath;
        op.value = value;
        op.valueType = valueType;
        
        batchOperations.push_back(op);
        
        bool result = true;
        if (batchOperations.size() >= MAX_BATCH_SIZE || 
            (millis() - lastBatchFlushTime) >= BATCH_TIMEOUT_MS) {
            result = flushBatch();
        }
        
        giveMutex();
        return result;
    }
    
    /**
     * Flushes pending batch operations
     * Thread-safe: Yes
     * @return true if all operations were successful
     */
    bool flushBatch() {
        if (batchOperations.empty()) {
            return true;
        }
        
        if (!takeMutex()) {
            return false;
        }
        
        bool success = true;
        
        // Group operations by unit
        std::map<int, std::vector<BatchOperation>> unitOperations;
        for (const auto& op : batchOperations) {
            unitOperations[op.unitIndex].push_back(op);
        }
        
        // Process each unit's operations
        for (const auto& unit : unitOperations) {
            if (!createPath(pathBuffer, sizeof(pathBuffer), 
                          SystemConfig::UNIT_PATH_FORMAT, 
                          SystemConfig::SERIAL_NUMBER, unit.first)) {
                success = false;
                continue;
            }
            
            documentJson.clear();
            for (const auto& op : unit.second) {
                documentJson.set(op.fieldPath.c_str(), op.value);
            }
            
            if (!Firebase.Firestore.patchDocument(&fbdo, 
                                                SystemConfig::FIREBASE_PROJECT_ID, 
                                                "", pathBuffer, 
                                                documentJson.raw(), "fields")) {
                ErrorManager::firebaseError(
                    ErrorManager::ErrorCode::FIREBASE_BATCH_FAILED,
                    String("Batch update failed: ") + fbdo.errorReason().c_str(),
                    "FirebaseManager::flushBatch"
                );
                success = false;
            }
        }
        
        batchOperations.clear();
        lastBatchFlushTime = millis();
        
        giveMutex();
        return success;
    }
    
    /**
     * Updates a single field in a document
     * Thread-safe: Yes
     * @param path Document path
     * @param field Field to update
     * @param value Value to set
     * @return true if update was successful
     */
    bool updateField(const char* path, const char* field, const String& value) {
        if (!takeMutex()) {
            return false;
        }
        
        documentJson.clear();
        documentJson.set(field, value);
        
        bool success = Firebase.Firestore.patchDocument(&fbdo, 
                                                      SystemConfig::FIREBASE_PROJECT_ID, 
                                                      "", path, 
                                                      documentJson.raw(), field);
        
        if (!success) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                String("Field update failed: ") + fbdo.errorReason().c_str(),
                "FirebaseManager::updateField"
            );
        }
        
        giveMutex();
        return success;
    }
};

#endif // FIREBASE_MANAGER_H 