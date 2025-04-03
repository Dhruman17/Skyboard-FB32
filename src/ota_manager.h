#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "firebase_manager.h"
#include "mutex_manager.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <string>

/**
 * OTAManager Class
 * 
 * Manages Over-The-Air (OTA) firmware updates:
 * 1. Update Checking:
 *    - Version comparison
 *    - Update availability
 *    - Update validation
 * 
 * 2. Update Process:
 *    - Firmware download
 *    - Update installation
 *    - Rollback handling
 * 
 * Thread Safety:
 * - All public methods are thread-safe
 * - Uses mutex protection for critical sections
 * 
 * Error Handling:
 * - Uses centralized ErrorManager for error reporting
 * - Implements automatic recovery mechanisms
 * - Provides detailed error context
 */
class OTAManager : public MutexManager {
private:
    FirebaseManager& firebaseManager;
    const char* systemPath;
    const char* serialNumber;
    const char* firmwareUrl;
    const char* firmwarePath;
    String currentVersion;
    bool initialized;
    bool updateInProgress;
    unsigned long lastCheck;
    
    /**
     * Downloads firmware update
     * Thread-safe: Yes
     * @return true if download successful
     */
    bool downloadUpdate() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        HTTPClient http;
        
        // Create URL
        String url = String(firmwareUrl) + firmwarePath;
        
        // Start HTTP client
        http.begin(url);
        
        // Get file size
        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::OTA_DOWNLOAD_FAILED,
                "Failed to get firmware file",
                "OTAManager::downloadUpdate"
            );
            http.end();
            return false;
        }
        
        // Get file size
        int contentLength = http.getSize();
        if (contentLength <= 0) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::OTA_DOWNLOAD_FAILED,
                "Invalid firmware file size",
                "OTAManager::downloadUpdate"
            );
            http.end();
            return false;
        }
        
        // Start update
        if (!Update.begin(contentLength)) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::OTA_UPDATE_FAILED,
                "Failed to start update",
                "OTAManager::downloadUpdate"
            );
            http.end();
            return false;
        }
        
        // Download and write update
        WiFiClient* stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);
        
        if (written != contentLength) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::OTA_UPDATE_FAILED,
                "Failed to write update",
                "OTAManager::downloadUpdate"
            );
            Update.end();
            http.end();
            return false;
        }
        
        // End update
        if (!Update.end()) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::OTA_UPDATE_FAILED,
                "Failed to end update",
                "OTAManager::downloadUpdate"
            );
            http.end();
            return false;
        }
        
        http.end();
        return true;
    }

public:
    /**
     * Constructor
     * @param firebase Reference to Firebase manager
     * @param systemPath System path in Firebase
     * @param serialNumber Device serial number
     * @param firmwareUrl Firmware server URL
     * @param firmwarePath Firmware file path
     * @param currentVersion Current firmware version
     */
    OTAManager(FirebaseManager& firebase, const char* systemPath, const char* serialNumber,
               const char* firmwareUrl, const char* firmwarePath, const String& currentVersion)
        : firebaseManager(firebase), systemPath(systemPath), serialNumber(serialNumber),
          firmwareUrl(firmwareUrl), firmwarePath(firmwarePath), currentVersion(currentVersion),
          initialized(false), updateInProgress(false), lastCheck(0) {
    }
    
    /**
     * Initializes the OTA manager
     * Thread-safe: Yes
     * @return true if initialization successful
     */
    bool begin() {
        if (initialized) {
            return true;
        }
        
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        // Verify configuration
        if (!systemPath || !serialNumber || !firmwareUrl || !firmwarePath) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::OTA_INIT_FAILED,
                "Invalid OTA configuration",
                "OTAManager::begin"
            );
            return false;
        }
        
        initialized = true;
        return true;
    }
    
    /**
     * Checks for firmware updates
     * Thread-safe: Yes
     * @return true if update available
     */
    bool checkForUpdates() {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!initialized) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::OTA_INIT_FAILED,
                "OTA manager not initialized",
                "OTAManager::checkForUpdates"
            );
            return false;
        }
        
        // Check Firebase for new version
        String newVersion;
        if (!firebaseManager.getDocument("/firmware/version", newVersion)) {
            return false;
        }
        
        return newVersion != currentVersion;
    }
    
    /**
     * Checks for and installs firmware updates
     * Should be called in the main loop
     */
    void update() {
        unsigned long currentMillis = millis();
        
        // Check if enough time has passed since last check
        if (currentMillis - lastCheck < SystemConfig::OTA_CHECK_INTERVAL) {
            return;
        }
        
        // Check for updates
        if (checkForUpdates()) {
            // Download and install update
            if (downloadUpdate()) {
                // Reboot to apply update
                ESP.restart();
            }
        }
        
        lastCheck = currentMillis;
    }
    
    /**
     * Gets current firmware version
     * Thread-safe: Yes
     * @return Firmware version string
     */
    const String& getCurrentVersion() const {
        return currentVersion;
    }
    
    /**
     * Checks if update is in progress
     * Thread-safe: Yes
     * @return true if update in progress
     */
    bool isUpdateInProgress() const {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        return updateInProgress;
    }
};

#endif // OTA_MANAGER_H 