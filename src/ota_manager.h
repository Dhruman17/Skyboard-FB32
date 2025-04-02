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
    String systemPath;
    String serialNumber;
    HTTPClient httpClient;
    const char* updateServer;
    const char* updatePath;
    const char* currentVersion;
    bool updateAvailable = false;
    bool updateInProgress = false;
    unsigned long lastCheck = 0;
    const unsigned long CHECK_INTERVAL = 3600000; // Check every hour
    
    void printPartitionInfo() {
        const esp_partition_t* running = esp_ota_get_running_partition();
        Serial.printf("Running partition: %s\n", running->label);
        
        const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
        Serial.printf("Next update partition: %s\n", next->label);
    }
    
    bool updateFirmwareVersionInFirestore() {
        if (systemPath == "") {
            Serial.println("System path is not defined. Cannot update version.");
            return false;
        }
        
        String documentPath = systemPath;
        FirebaseJson content;
        content.set("fields/version/stringValue", SystemConfig::FIRMWARE_VERSION);
        
        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "version")) {
            Serial.println("System version updated successfully in Firestore.");
            return true;
        } else {
            Serial.println("Failed to update system version.");
            Serial.println(fbdo.errorReason());
            return false;
        }
    }
    
    String fetchLatestVersion() {
        String documentPath = systemPath;
        if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())) {
            FirebaseJson json;
            json.setJsonData(fbdo.payload());
            FirebaseJsonData jsonData;
            
            if (json.get(jsonData, "fields/version/stringValue")) {
                return jsonData.stringValue;
            }
        }
        return "";
    }
    
    bool performOTAUpdate() {
        String documentPath = systemPath;
        if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())) {
            FirebaseJson json;
            json.setJsonData(fbdo.payload());
            FirebaseJsonData jsonData;
            
            if (json.get(jsonData, "fields/firmwareUrl/stringValue")) {
                String firmwareUrl = jsonData.stringValue;
                Serial.println("Firmware URL: " + firmwareUrl);
                
                ScopedHttpClient http(httpClient);
                if (!http.begin(firmwareUrl)) {
                    Serial.println("Failed to begin HTTP request");
                    return false;
                }
                
                int httpCode = http.get().GET();
                if (httpCode != HTTP_CODE_OK) {
                    Serial.printf("HTTP GET failed, error: %s\n", http.get().errorToString(httpCode).c_str());
                    return false;
                }
                
                int contentLength = http.get().getSize();
                Serial.printf("Content length: %d\n", contentLength);
                
                if (!Update.begin(contentLength)) {
                    Serial.println("Failed to begin update");
                    return false;
                }
                
                WiFiClient* stream = http.get().getStreamPtr();
                size_t written = Update.writeStream(*stream);
                Serial.printf("Written: %d\n", written);
                
                if (written != contentLength) {
                    Serial.println("Written size mismatch");
                    return false;
                }
                
                if (!Update.end()) {
                    Serial.println("Error occurred: " + String(Update.errorString()));
                    return false;
                }
                
                if (Update.isRunning()) {
                    Serial.println("Update completed successfully");
                    return true;
                } else {
                    Serial.println("Update failed");
                    return false;
                }
            }
        }
        return false;
    }

    /**
     * Compares version strings
     * @param v1 First version string
     * @param v2 Second version string
     * @return true if v1 is newer than v2
     */
    bool isNewerVersion(const char* v1, const char* v2) {
        return strcmp(v1, v2) > 0;
    }
    
    /**
     * Performs the firmware update process
     * Downloads and applies the update in chunks
     * @return true if update successful
     */
    bool performUpdate() {
        HTTPClient http;
        http.begin(updateServer, 80, updatePath);
        int httpCode = http.GET();
        
        if (httpCode != HTTP_CODE_OK) {
            Serial.println("Failed to connect to update server");
            http.end();
            return false;
        }
        
        int contentLength = http.getSize();
        if (contentLength <= 0) {
            Serial.println("Invalid content length");
            http.end();
            return false;
        }
        
        if (!Update.begin(contentLength)) {
            Serial.println("Failed to begin update");
            http.end();
            return false;
        }
        
        Serial.println("Update started");
        Serial.println("Downloading update...");
        
        WiFiClient* stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);
        
        if (written != contentLength) {
            Serial.println("Written size mismatch");
            http.end();
            return false;
        }
        
        if (Update.end()) {
            Serial.println("Update completed successfully");
            http.end();
            return true;
        } else {
            Serial.println("Update failed");
            http.end();
            return false;
        }
    }

public:
    /**
     * Constructor
     * @param server Update server URL
     * @param path Path to firmware file
     * @param version Current firmware version
     */
    OTAManager(FirebaseData& fbdo, String systemPath, String serialNumber, const char* server, const char* path, const char* version)
        : fbdo(fbdo), systemPath(systemPath), serialNumber(serialNumber), updateServer(server), updatePath(path), currentVersion(version) {}
    
    void begin(const String& hostname) {
        ArduinoOTA.setHostname(hostname.c_str());
        ArduinoOTA.onStart([]() {
            String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
            Serial.printf("Start updating %s\n", type.c_str());
        });
        ArduinoOTA.onEnd([]() { Serial.println("\nUpdate Complete!"); });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        });
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR) Serial.println("End Failed");
        });
        ArduinoOTA.begin();
    }
    
    /**
     * Checks for available updates
     * @return true if update is available
     */
    bool checkForUpdates() {
        unsigned long currentMillis = millis();
        if (currentMillis - lastCheck < CHECK_INTERVAL) {
            return updateAvailable;
        }
        
        lastCheck = currentMillis;
        Serial.println("Checking for updates...");
        
        HTTPClient http;
        http.begin(updateServer, 80, updatePath);
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            String newVersion = http.getString();
            updateAvailable = isNewerVersion(newVersion.c_str(), currentVersion);
            
            if (updateAvailable) {
                Serial.println("New version available: " + newVersion);
            } else {
                Serial.println("No update available");
            }
        } else {
            Serial.println("Failed to check for updates");
            updateAvailable = false;
        }
        
        http.end();
        return updateAvailable;
    }
    
    /**
     * Handles OTA update process
     * Checks for updates and performs update if available
     */
    void handle() {
        if (!updateInProgress && checkForUpdates()) {
            updateInProgress = true;
            if (performUpdate()) {
                Serial.println("Update completed successfully");
            } else {
                Serial.println("Update failed");
                updateInProgress = false;
            }
        }
    }
    
    /**
     * Updates system version in Firebase
     * @param fbdo Reference to Firebase data object
     */
    void updateSystemVersion() {
        updateFirmwareVersionInFirestore();
    }
    
    void checkForUpdatesFromFirebase() {
        String latestVersion = fetchLatestVersion();
        if (latestVersion != "" && latestVersion != SystemConfig::FIRMWARE_VERSION) {
            Serial.println("New firmware version available: " + latestVersion);
            if (performOTAUpdate()) {
                Serial.println("Update successful, restarting...");
                ESP.restart();
            }
        }
    }
};

#endif // OTA_MANAGER_H 