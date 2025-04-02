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
    String currentVersion;
    String latestVersion;
    unsigned long lastUpdateAttempt;
    uint8_t updateAttempts;
    static constexpr uint8_t MAX_UPDATE_ATTEMPTS = 3;
    static constexpr uint32_t UPDATE_BACKOFF_DELAY = 300000; // 5 minutes
    
    /**
     * Creates a Firebase document path
     * @param buffer Output buffer for the path
     * @param bufferSize Size of the buffer
     * @param format Format string for the path
     * @param serialNumber System serial number
     * @return true if path was created successfully
     */
    bool createPath(char* buffer, size_t bufferSize, const char* format, const char* serialNumber) {
        if (!buffer || !format || !serialNumber) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INVALID_STATE,
                "Invalid parameters for path creation",
                "OTAManager::createPath"
            );
            return false;
        }

        int written = snprintf(buffer, bufferSize, format, serialNumber);
        if (written < 0 || written >= bufferSize) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INVALID_STATE,
                "Path buffer overflow",
                "OTAManager::createPath"
            );
            return false;
        }

        return true;
    }
    
    /**
     * Fetches the latest firmware version from Firebase
     * @return true if version was fetched successfully
     */
    bool fetchLatestVersion() {
        if (!initialized) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                "OTA not initialized",
                "OTAManager::fetchLatestVersion"
            );
            return false;
        }

        // Create path buffer for Firebase
        char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
        if (!createPath(pathBuffer, sizeof(pathBuffer), 
                       SystemConfig::SYSTEM_PATH_FORMAT, 
                       SystemConfig::SERIAL_NUMBER)) {
            return false;
        }

        // Get the latest version from Firebase
        if (!Firebase.Firestore.getDocument(&fbdo, 
                                          SystemConfig::FIREBASE_PROJECT_ID, 
                                          "", pathBuffer)) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
                String("Failed to fetch latest version: ") + fbdo.errorReason().c_str(),
                "OTAManager::fetchLatestVersion"
            );
            return false;
        }

        // Parse the version from the response
        FirebaseJson json;
        json.setJsonData(fbdo.payload().c_str());
        FirebaseJsonData result;
        
        if (!json.get(result, "fields/firmwareVersion/stringValue")) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                "Failed to parse firmware version from Firebase",
                "OTAManager::fetchLatestVersion"
            );
            return false;
        }

        latestVersion = result.stringValue;
        return true;
    }

    /**
     * Compares version strings
     * @param v1 First version string
     * @param v2 Second version string
     * @return true if v1 is less than v2 (needs update)
     */
    bool compareVersions(const String& v1, const String& v2) {
        // Split versions into components
        int v1Major, v1Minor, v1Patch;
        int v2Major, v2Minor, v2Patch;
        
        sscanf(v1.c_str(), "%d.%d.%d", &v1Major, &v1Minor, &v1Patch);
        sscanf(v2.c_str(), "%d.%d.%d", &v2Major, &v2Minor, &v2Patch);
        
        // Compare major version
        if (v1Major < v2Major) return true;
        if (v1Major > v2Major) return false;
        
        // Compare minor version
        if (v1Minor < v2Minor) return true;
        if (v1Minor > v2Minor) return false;
        
        // Compare patch version
        return v1Patch < v2Patch;
    }
    
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

        // Check if we should back off from update attempts
        if (updateAttempts >= MAX_UPDATE_ATTEMPTS) {
            unsigned long currentMillis = millis();
            if (currentMillis - lastUpdateAttempt < UPDATE_BACKOFF_DELAY) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                    "Too many update attempts, backing off",
                    "OTAManager::downloadFirmware"
                );
                return false;
            }
            // Reset attempts after backoff period
            updateAttempts = 0;
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
            updateAttempts++;
            lastUpdateAttempt = millis();
            return false;
        }

        int contentLength = http.getSize();
        bool isChunked = contentLength < 0;
        
        // For chunked transfers, we need to handle the download differently
        if (isChunked) {
            // Start update with a default size for chunked transfers
            if (!Update.begin(0)) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                    String("Failed to begin update: ") + Update.errorString(),
                    "OTAManager::downloadFirmware"
                );
                http.end();
                updateAttempts++;
                lastUpdateAttempt = millis();
                return false;
            }
        } else if (contentLength <= 0) {
            ErrorManager::networkError(
                ErrorManager::ErrorCode::NETWORK_CONNECTION_FAILED,
                "Invalid content length",
                "OTAManager::downloadFirmware"
            );
            http.end();
            updateAttempts++;
            lastUpdateAttempt = millis();
            return false;
        } else if (!Update.begin(contentLength)) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                String("Failed to begin update: ") + Update.errorString(),
                "OTAManager::downloadFirmware"
            );
            http.end();
            updateAttempts++;
            lastUpdateAttempt = millis();
            return false;
        }

        WiFiClient *stream = http.getStreamPtr();
        size_t written = 0;
        uint8_t buff[1024] = { 0 };
        bool success = true;
        
        while (http.connected() && (isChunked || written < contentLength)) {
            size_t size = stream->available();
            if (size) {
                int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                if (Update.write(buff, c) != c) {
                    ErrorManager::systemError(
                        ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                        String("Failed to write firmware: ") + Update.errorString(),
                        "OTAManager::downloadFirmware"
                    );
                    success = false;
                    break;
                }
                written += c;
                // Log progress every 10%
                if (!isChunked && written % (contentLength / 10) < 1024) {
                    Serial.printf("Progress: %d%%\n", (written * 100) / contentLength);
                }
            }
            delay(1);  // Small delay to prevent watchdog reset
        }

        // Clean up HTTP connection
        http.end();

        // Handle download completion
        if (!success) {
            Update.abort();  // Clean up any allocated resources
            updateAttempts++;
            lastUpdateAttempt = millis();
            return false;
        }

        // For chunked transfers, we need to verify the download is complete
        if (isChunked) {
            if (!Update.end()) {
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                    String("Failed to end chunked update: ") + Update.errorString(),
                    "OTAManager::downloadFirmware"
                );
                updateAttempts++;
                lastUpdateAttempt = millis();
                return false;
            }
        } else if (written != contentLength) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                "Download incomplete",
                "OTAManager::downloadFirmware"
            );
            Update.abort();  // Clean up any allocated resources
            updateAttempts++;
            lastUpdateAttempt = millis();
            return false;
        } else if (!Update.end()) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                String("Failed to end update: ") + Update.errorString(),
                "OTAManager::downloadFirmware"
            );
            updateAttempts++;
            lastUpdateAttempt = millis();
            return false;
        }

        Serial.println("Firmware download complete");
        updateAttempts = 0; // Reset attempts on success
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
        
        // Fetch latest version from Firebase
        if (!fetchLatestVersion()) {
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_UPDATE_FAILED,
                "Failed to fetch latest version",
                "OTAManager::checkForUpdates"
            );
            return false;
        }
        
        // Compare versions
        if (compareVersions(currentVersion, latestVersion)) {
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
          mutex(NULL), shouldUpdate(false), firmwareSize(0), 
          currentVersion(currentVersion), updateAttempts(0), lastUpdateAttempt(0) {
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