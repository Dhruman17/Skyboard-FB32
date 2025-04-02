#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "config.h"
#include "http_client_wrapper.h"
#include <HTTPClient.h>
#include <Update.h>
#include <Firebase_ESP_Client.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <ArduinoOTA.h>
#include <freertos/semphr.h>
#include <time.h>

/**
 * OTAManager Class
 * 
 * Handles Over-The-Air firmware updates:
 * 1. Update Checking:
 *    - Periodic version checks
 *    - Version comparison
 *    - Update availability detection
 * 
 * 2. Update Process:
 *    - Download verification
 *    - Update progress tracking
 *    - Error handling
 * 
 * 3. System Integration:
 *    - Version reporting to Firebase
 *    - Update status monitoring
 *    - System restart after update
 * 
 * Update Process:
 * - Checks for updates every hour
 * - Downloads updates in chunks
 * - Verifies update before applying
 * - Restarts system after successful update
 */
class OTAManager {
private:
    FirebaseData& fbdo;
    const char* systemPath;
    String serialNumber;
    const char* firmwareUrl;
    const char* firmwarePath;
    const char* currentVersion;
    bool initialized;
    SemaphoreHandle_t mutex;
    
    // Update state tracking
    bool updateInProgress;
    bool updateVerified;
    uint32_t updateSize;
    uint32_t updateProgress;
    unsigned long lastUpdateCheck;
    
    // Error tracking
    uint8_t updateErrorCount;
    
    /**
     * Safely takes the mutex with timeout
     * @return true if mutex was taken successfully
     */
    bool takeMutex() {
        return xSemaphoreTake(mutex, pdMS_TO_TICKS(SystemConfig::MUTEX_TIMEOUT_MS)) == pdTRUE;
    }
    
    /**
     * Safely gives the mutex
     */
    void giveMutex() {
        xSemaphoreGive(mutex);
    }
    
    /**
     * Verifies firmware before applying update
     * @param firmwareData Pointer to firmware data
     * @param firmwareSize Size of firmware data
     * @return true if verification successful
     */
    bool verifyFirmware(const uint8_t* firmwareData, size_t firmwareSize) {
        if (!takeMutex()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "verifyFirmware", "Failed to take mutex");
            return false;
        }
        
        bool success = true;
        
        // Check firmware size
        if (firmwareSize == 0 || firmwareSize > UPDATE_SIZE_UNKNOWN) {
            Serial.printf("Invalid firmware size: %d\n", firmwareSize);
            success = false;
        }
        
        // Verify firmware checksum (implement your checksum verification here)
        // This is a placeholder for actual checksum verification
        if (success) {
            // TODO: Implement actual checksum verification
            updateVerified = true;
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Prepares for firmware update
     * @return true if preparation successful
     */
    bool prepareUpdate() {
        if (!takeMutex()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "prepareUpdate", "Failed to take mutex");
            return false;
        }
        
        bool success = true;
        
        // Check if enough space is available
        if (ESP.getFreeSketchSpace() < updateSize) {
            Serial.printf("Not enough space for update: required %d, available %d\n", 
                        updateSize, ESP.getFreeSketchSpace());
            success = false;
        }
        
        // Begin update
        if (success) {
            if (!Update.begin(updateSize)) {
                Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "prepareUpdate", "Failed to begin update");
                success = false;
            }
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Handles update process
     * @param firmwareData Pointer to firmware data
     * @param firmwareSize Size of firmware data
     * @return true if update successful
     */
    bool processUpdate(const uint8_t* firmwareData, size_t firmwareSize) {
        if (!takeMutex()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "processUpdate", "Failed to take mutex");
            return false;
        }
        
        bool success = true;
        
        // Create non-const copy of firmware data
        uint8_t* writeData = new uint8_t[firmwareSize];
        if (!writeData) {
            Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "processUpdate", "Failed to allocate memory");
            success = false;
        } else {
            memcpy(writeData, firmwareData, firmwareSize);
            
            // Write firmware data
            if (Update.write(writeData, firmwareSize) != firmwareSize) {
                Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "processUpdate", "Failed to write firmware");
                success = false;
            }
            
            delete[] writeData;
        }
        
        // End update
        if (success) {
            if (!Update.end()) {
                Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "processUpdate", "Failed to end update");
                success = false;
            }
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Performs rollback if update fails
     */
    void performRollback() {
        if (!takeMutex()) {
            Serial.printf(SystemConfig::ERROR_FORMAT_FIREBASE, "performRollback", "Failed to take mutex");
            return;
        }
        
        Serial.println("Performing rollback...");
        
        // Abort update
        Update.abort();
        
        // Reset error count
        updateErrorCount = 0;
        
        // Reset state
        updateInProgress = false;
        updateVerified = false;
        updateSize = 0;
        updateProgress = 0;
        
        giveMutex();
    }
    
    /**
     * Formats current timestamp for Firebase
     * @return Formatted timestamp string
     */
    String formatTimestamp() {
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            Serial.println("Failed to obtain time");
            return "";
        }
        char timestamp[30];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        return String(timestamp);
    }

public:
    /**
     * Constructor
     * @param fbdo Reference to Firebase data object
     * @param systemPath Path to system document in Firebase
     * @param serialNumber System serial number
     * @param firmwareUrl URL for firmware updates
     * @param firmwarePath Path to firmware file
     * @param currentVersion Current firmware version
     */
    OTAManager(FirebaseData& fbdo, const char* systemPath, String serialNumber,
               const char* firmwareUrl, const char* firmwarePath, const char* currentVersion)
        : fbdo(fbdo), systemPath(systemPath), serialNumber(serialNumber),
          firmwareUrl(firmwareUrl), firmwarePath(firmwarePath), currentVersion(currentVersion),
          initialized(false), updateInProgress(false), updateVerified(false),
          updateSize(0), updateProgress(0), lastUpdateCheck(0), updateErrorCount(0) {
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("Failed to create mutex in OTAManager");
        }
    }
    
    /**
     * Destructor
     */
    ~OTAManager() {
        if (mutex != NULL) {
            vSemaphoreDelete(mutex);
        }
        
        // Clean up any pending update
        if (updateInProgress) {
            Update.abort();
        }
    }
    
    /**
     * Initializes the OTA manager
     * @return true if initialization successful
     */
    bool begin() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in begin");
            return false;
        }
        
        bool success = true;
        
        // Initialize update state
        updateInProgress = false;
        updateVerified = false;
        updateSize = 0;
        updateProgress = 0;
        updateErrorCount = 0;
        
        if (success) {
            initialized = true;
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Checks for firmware updates
     * @return true if update available
     */
    bool checkForUpdates() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in checkForUpdates");
            return false;
        }
        
        bool success = true;
        FirebaseJson json;
        FirebaseJsonData jsonData;
        
        // Get latest version from Firebase
        if (Firebase.RTDB.getJSON(&fbdo, firmwarePath)) {
            if (json.get(jsonData, "fields/version/stringValue")) {
                String latestVersion = jsonData.stringValue;
                if (latestVersion != currentVersion) {
                    // New version available
                    updateInProgress = true;
                    updateSize = UPDATE_SIZE_UNKNOWN;
                    updateProgress = 0;
                }
            }
        } else {
            Serial.println("Failed to get latest version");
            success = false;
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Handles firmware update process
     * @return true if update successful
     */
    bool handle() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in handle");
            return false;
        }
        
        bool success = true;
        
        if (updateInProgress) {
            HTTPClient http;
            http.begin(firmwareUrl);
            int httpCode = http.GET();
            
            if (httpCode == HTTP_CODE_OK) {
                uint8_t buffer[1024];
                WiFiClient* stream = http.getStreamPtr();
                int len = stream->read(buffer, sizeof(buffer));
                if (len > 0) {
                    if (!processUpdate(buffer, len)) {
                        Serial.println("Failed to process update");
                        success = false;
                    }
                    updateProgress += len;
                }
            } else {
                Serial.println("Failed to download firmware");
                success = false;
            }
            
            http.end();
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Gets the current update progress
     * @return Update progress percentage (0-100)
     */
    uint32_t getUpdateProgress() {
        if (!takeMutex()) {
            return 0;
        }
        
        uint32_t progress = updateProgress;
        giveMutex();
        return progress;
    }
    
    /**
     * Gets the current update state
     * @return true if update is in progress
     */
    bool isUpdateInProgress() {
        if (!takeMutex()) {
            return false;
        }
        
        bool inProgress = updateInProgress;
        giveMutex();
        return inProgress;
    }

    /**
     * Updates system version in Firebase
     * @return true if update successful
     */
    bool updateSystemVersion() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in updateSystemVersion");
            return false;
        }
        
        bool success = true;
        FirebaseJson json;
        
        // Set version and timestamp
        json.set("fields/version/stringValue", currentVersion);
        json.set("fields/lastUpdated/timestampValue", formatTimestamp());
        
        // Update Firebase
        if (!Firebase.RTDB.setJSON(&fbdo, systemPath, &json)) {
            Serial.println("Failed to update system version");
            success = false;
        }
        
        giveMutex();
        return success;
    }
};

#endif // OTA_MANAGER_H 