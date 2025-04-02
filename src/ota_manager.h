#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "scoped_lock.h"
#include <HTTPClient.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Firebase_ESP_Client.h>

class OTAManager {
private:
    FirebaseData& fbdo;
    bool initialized;
    bool updateInProgress;
    size_t updateSize;
    String firmwareUrl;
    SemaphoreHandle_t mutex;
    bool shouldUpdate;
    size_t firmwareSize;
    
    /**
     * Downloads firmware from the specified URL
     * @return true if download was successful
     */
    bool downloadFirmware() {
        if (!initialized) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                "OTA not initialized",
                "OTAManager::downloadFirmware"
            );
            return false;
        }

        HTTPClient http;
        http.begin(firmwareUrl);
        int httpCode = http.GET();
        
        if (httpCode != HTTP_CODE_OK) {
            ErrorManager::networkError(
                ErrorManager::ErrorCode::NETWORK_CONNECTION_FAILED,
                "Failed to connect to firmware server",
                "OTAManager::downloadFirmware"
            );
            http.end();
            return false;
        }

        int contentLength = http.getSize();
        if (contentLength <= 0) {
            ErrorManager::networkError(
                ErrorManager::ErrorCode::NETWORK_CONNECTION_FAILED,
                "Invalid content length",
                "OTAManager::downloadFirmware"
            );
            http.end();
            return false;
        }

        if (!Update.begin(contentLength)) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                String("Failed to begin update: ") + Update.errorString(),
                "OTAManager::downloadFirmware"
            );
            http.end();
            return false;
        }

        WiFiClient *stream = http.getStreamPtr();
        size_t written = 0;
        uint8_t buff[1024] = { 0 };
        
        while (http.connected() && (written < contentLength)) {
            size_t size = stream->available();
            if (size) {
                int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                if (Update.write(buff, c) != c) {
                    ErrorManager::systemError(
                        ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                        String("Failed to write firmware: ") + Update.errorString(),
                        "OTAManager::downloadFirmware"
                    );
                    http.end();
                    return false;
                }
                written += c;
                // Log progress every 10%
                if (written % (contentLength / 10) < 1024) {
                    Serial.printf("Progress: %d%%\n", (written * 100) / contentLength);
                }
            }
            delay(1);  // Small delay to prevent watchdog reset
        }

        if (written != contentLength) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                "Download incomplete",
                "OTAManager::downloadFirmware"
            );
            http.end();
            return false;
        }

        if (!Update.end()) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                String("Failed to end update: ") + Update.errorString(),
                "OTAManager::downloadFirmware"
            );
            http.end();
            return false;
        }

        http.end();
        Serial.println("Firmware download complete");
        return true;
    }
    
    /**
     * Checks if a firmware update is available
     * @param url Output parameter for firmware URL
     * @param shouldUpdate Output parameter indicating if update is needed
     * @return true if check was successful
     */
    bool checkForUpdates(const char*& url, bool& shouldUpdate) {
        shouldUpdate = false;
        url = nullptr;
        
        // TODO: Implement version check logic
        // For now, just use the stored URL if available
        if (!firmwareUrl.isEmpty()) {
            url = firmwareUrl.c_str();
            shouldUpdate = true;
            return true;
        }
        
        return false;
    }

    /**
     * Verifies the downloaded firmware
     * @return true if verification was successful
     */
    bool verifyFirmware() const {
        // Check if firmware size is valid
        if (firmwareSize == 0) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                "Invalid firmware size: 0 bytes",
                "OTAManager::verifyFirmware"
            );
            return false;
        }

        if (!Update.end(true)) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                String("Firmware verification failed: ") + Update.errorString(),
                "OTAManager::verifyFirmware"
            );
            return false;
        }

        // Verify the total bytes written matches the expected firmware size
        if (Update.size() != firmwareSize) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                String("Firmware size mismatch. Expected: ") + String(firmwareSize) + 
                " bytes, Got: " + String(Update.size()) + " bytes",
                "OTAManager::verifyFirmware"
            );
            return false;
        }

        return true;
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
    OTAManager(FirebaseData& fbdo, const char* systemPath, const char* serialNumber,
               const char* firmwareUrl, const char* firmwarePath, const char* currentVersion)
        : fbdo(fbdo), initialized(false), updateInProgress(false), updateSize(0), 
          mutex(NULL), shouldUpdate(false), firmwareSize(0) {
        this->firmwareUrl = String(firmwareUrl);
        
        mutex = xSemaphoreCreateMutex();
        if (!mutex) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_CREATION_FAILED,
                "Failed to create mutex",
                "OTAManager::OTAManager"
            );
        }
    }
    
    /**
     * Destructor
     */
    ~OTAManager() {
        if (mutex != NULL) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Initializes OTA manager
     * @return true if initialization successful
     */
    bool initialize() {
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to acquire mutex",
                "OTAManager::initialize"
            );
            return false;
        }
        
        initialized = true;
        return true;
    }
    
    /**
     * Sets firmware update URL and size
     * @param url Firmware URL
     * @param size Firmware size
     */
    void setFirmwareUpdate(const String& url, size_t size) {
        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to acquire mutex",
                "OTAManager::setFirmwareUpdate"
            );
            return;
        }
        
        firmwareUrl = url;
        updateSize = size;
        updateInProgress = true;
    }
    
    /**
     * Checks for firmware updates
     * @return true if update check was successful
     */
    bool checkForUpdates() {
        if (!initialized) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                "OTA not initialized",
                "OTAManager::checkForUpdates"
            );
            return false;
        }

        ScopedLock lock(mutex);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to acquire mutex",
                "OTAManager::checkForUpdates"
            );
            return false;
        }

        if (updateInProgress) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INVALID_STATE,
                "Update already in progress",
                "OTAManager::checkForUpdates"
            );
            return false;
        }

        updateInProgress = true;
        bool success = true;

        // Check for updates and download if available
        if (!firmwareUrl.isEmpty()) {
            shouldUpdate = true;
            if (!downloadFirmware()) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                    "Failed to download firmware",
                    "OTAManager::checkForUpdates"
                );
                success = false;
            }
        }

        if (success && shouldUpdate) {
            Serial.println("Update successful, restarting...");
            ESP.restart();
        } else {
            updateInProgress = false;
        }

        return success;
    }
    
    /**
     * Updates system version in Firebase
     * @return true if update successful
     */
    bool updateSystemVersion() {
        // TODO: Implement version update in Firebase
        return true;
    }
};

#endif // OTA_MANAGER_H 