#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "config.h"
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Firebase_ESP_Client.h>

class OTAManager {
private:
    FirebaseData& fbdo;
    String systemPath;
    String serialNumber;
    
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
                
                ScopedHttpClient http;
                if (!http.begin(firmwareUrl)) {
                    Serial.println("Failed to begin HTTP request");
                    return false;
                }
                
                int httpCode = http.getClient().GET();
                if (httpCode != HTTP_CODE_OK) {
                    Serial.printf("HTTP GET failed, error: %s\n", http.getClient().errorToString(httpCode).c_str());
                    return false;
                }
                
                int contentLength = http.getClient().getSize();
                Serial.printf("Content length: %d\n", contentLength);
                
                if (!Update.begin(contentLength)) {
                    Serial.println("Failed to begin update");
                    return false;
                }
                
                size_t written = Update.writeStream(http.getClient());
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

public:
    OTAManager(FirebaseData& fbdo, String systemPath, String serialNumber)
        : fbdo(fbdo), systemPath(systemPath), serialNumber(serialNumber) {}
    
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
    
    void updateSystemVersion() {
        updateFirmwareVersionInFirestore();
    }
    
    void checkForUpdates() {
        String latestVersion = fetchLatestVersion();
        if (latestVersion != "" && latestVersion != SystemConfig::FIRMWARE_VERSION) {
            Serial.println("New firmware version available: " + latestVersion);
            if (performOTAUpdate()) {
                Serial.println("Update successful, restarting...");
                ESP.restart();
            }
        }
    }
    
    void handle() {
        ArduinoOTA.handle();
    }
};

#endif // OTA_MANAGER_H 