#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <HTTPClient.h>
#include <Update.h>
#include "http_client_wrapper.h"
#include "config.h"
#include <Firebase_ESP_Client.h>

class OTAManager {
private:
    FirebaseData& fbdo;
    String systemPath;
    String serialNumber;
    
    void printPartitionInfo() {
        const esp_partition_t *running = esp_ota_get_running_partition();
        Serial.printf("Running partition: %s\n", running->label);
    }
    
    void updateFirmwareVersionInFirestore(float newVersion) {
        if (systemPath != "") {
            String documentPath = systemPath;
            FirebaseJson content;
            content.set("fields/version/doubleValue", newVersion);

            if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "version")) {
                Serial.printf("Updated Firestore firmware version to: %.2f\n", newVersion);
            } else {
                Serial.println("Failed to update firmware version in Firestore.");
                Serial.println(fbdo.errorReason());
            }
        }
    }
    
    float fetchLatestVersion() {
        HTTPClient http;
        ScopedHttpClient scopedHttp(http);
        
        String versionUrl = "https://firebasestorage.googleapis.com/v0/b/" + String(FIREBASE_PROJECT_ID) +
                            ".appspot.com/o/Version_" + serialNumber + ".txt?alt=media";

        if (!scopedHttp.begin(versionUrl)) {
            Serial.println("Failed to begin HTTP request");
            return SystemConfig::FIRMWARE_VERSION;
        }

        int httpCode = scopedHttp.get().GET();

        if (httpCode == HTTP_CODE_OK) {
            String versionString = scopedHttp.get().getString();
            return versionString.toFloat();
        } else {
            Serial.printf("Failed to fetch latest firmware version. HTTP Error: %s\n", 
                         scopedHttp.get().errorToString(httpCode).c_str());
            return SystemConfig::FIRMWARE_VERSION;
        }
    }
    
    void performOTAUpdate(String firmwareUrl, float newFirmwareVersion) {
        HTTPClient http;
        ScopedHttpClient scopedHttp(http);
        
        Serial.println("Connecting to firmware URL...");
        if (!scopedHttp.begin(firmwareUrl)) {
            Serial.println("Failed to begin HTTP request");
            return;
        }

        int httpCode = scopedHttp.get().GET();

        if (httpCode == HTTP_CODE_OK) {
            int contentLength = scopedHttp.get().getSize();
            WiFiClient *stream = scopedHttp.get().getStreamPtr();

            Serial.printf("Firmware size (expected): %d\n", contentLength);
            Serial.printf("Available Flash Space: %d\n", ESP.getFreeSketchSpace());

            printPartitionInfo();

            if (contentLength > ESP.getFreeSketchSpace()) {
                Serial.println("Not enough space for OTA update! Aborting...");
                return;
            }

            Serial.println("Initializing OTA update...");
            if (!Update.begin(contentLength)) {
                Serial.println("Update.begin() failed! Not enough space?");
                return;
            }

            Serial.println("Writing firmware...");
            size_t written = 0;
            int chunkSize = 1024;
            unsigned long timeout = millis();
            
            while (written < contentLength) {
                if (stream->available()) {
                    uint8_t buffer[chunkSize];
                    int bytesRead = stream->read(buffer, chunkSize);
                    if (bytesRead <= 0) {
                        Serial.println("Read error or no data.");
                        break;
                    }

                    written += Update.write(buffer, bytesRead);
                    Serial.printf("Total bytes written: %u\n", written);
                    timeout = millis();
                } else {
                    delay(10);
                    if (millis() - timeout > 10000) {
                        Serial.println("Timeout waiting for more data.");
                        break;
                    }
                }
            }

            Serial.printf("Final bytes written: %u\n", written);

            if (written == contentLength) {
                Serial.println("OTA update successful!");
                updateFirmwareVersionInFirestore(newFirmwareVersion);

                Serial.println("Finishing update...");
                if (Update.end()) {
                    Serial.println("Rebooting ESP32...");
                    delay(3000);
                    ESP.restart();
                } else {
                    Serial.println("Update.end() failed!");
                }
            } else {
                Serial.println("OTA update failed: Incomplete write!");
            }
        } else {
            Serial.printf("Failed to download firmware: HTTP Error %d\n", httpCode);
        }
    }

public:
    OTAManager(FirebaseData& fbdo, const String& path, const String& serial) 
        : fbdo(fbdo), systemPath(path), serialNumber(serial) {}
    
    void checkForUpdates() {
        String documentPath = systemPath;

        if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())) {
            FirebaseJson json;
            json.setJsonData(fbdo.payload());
            FirebaseJsonData jsonData;

            float cloudVersion = SystemConfig::FIRMWARE_VERSION;
            if (json.get(jsonData, "fields/version/doubleValue")) {
                cloudVersion = jsonData.floatValue;
                Serial.printf("Cloud Firmware Version: %.2f\n", cloudVersion);
            } else {
                Serial.println("Failed to get firmware version from Firestore.");
            }

            float storageVersion = fetchLatestVersion();
            Serial.printf("Storage Firmware Version: %.2f\n", storageVersion);

            if (storageVersion > cloudVersion) {
                Serial.println("New firmware available. Proceeding with update...");

                String firmwareUrl;
                firmwareUrl.reserve(200);
                firmwareUrl = "https://firebasestorage.googleapis.com/v0/b/";
                firmwareUrl += FIREBASE_PROJECT_ID;
                firmwareUrl += ".appspot.com/o/firmware_";
                firmwareUrl += serialNumber;
                firmwareUrl += ".bin?alt=media";

                performOTAUpdate(firmwareUrl, storageVersion);
            } else {
                Serial.println("Firmware is up to date. No update needed.");
            }
        } else {
            Serial.println("Failed to check Firestore for firmware update.");
            Serial.println(fbdo.errorReason());
        }
    }
    
    void updateSystemVersion() {
        if (systemPath != "") {
            String documentPath = systemPath;
            FirebaseJson content;

            content.set("fields/version/doubleValue", SystemConfig::FIRMWARE_VERSION);

            String firmwareUrl = "https://firebasestorage.googleapis.com/v0/b/" + String(FIREBASE_PROJECT_ID) +
                                ".appspot.com/o/firmware_" + serialNumber + ".bin?alt=media";

            content.set("fields/firmware_url/stringValue", firmwareUrl);

            if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "version,firmware_url")) {
                Serial.println("System version and firmware URL updated successfully in Firestore.");
            } else {
                Serial.println("Failed to update system version.");
                Serial.println(fbdo.errorReason());
            }
        } else {
            Serial.println("System path is not defined. Cannot update version.");
        }
    }
};

#endif // OTA_MANAGER_H 