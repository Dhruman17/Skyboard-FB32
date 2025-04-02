#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include "config.h"
#include "light_manager.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "unit_manager.h"
#include "firebase_coms.h"
#include <Firebase_ESP_Client.h>

class SystemManager {
private:
    NetworkManager& networkManager;
    OTAManager& otaManager;
    LightManager& lightManager;
    UnitManager& unitManager;
    FirebaseData& fbdo;
    SystemState& systemState;
    
    String systemName;
    String unitNames[SystemConfig::NUMBER_OF_UNITS];
    int connectionOffset;
    
    void sendHeartbeat() {
        String documentPath = systemPath;
        FirebaseJson content;
        content.set("fields/lastSeen/timestampValue", formatTimestamp());
        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "lastSeen")) {
            Serial.println("Heartbeat sent.");
            Serial.println(formatTimestamp());
        } else {
            Serial.println("Failed to send heartbeat.");
            Serial.println(fbdo.errorReason());
        }
    }
    
    void updateSystemData() {
        fetchFirebaseSystemData(&fbdo, &systemName, &lightOnTime, &lightOffTime, &lightMasterSwitch, &timeCycleEnabled, unitNames);
        unitManager.updateUnitNames(unitNames);
        unitManager.updateUnitData();
    }

public:
    SystemManager(NetworkManager& network, OTAManager& ota, LightManager& light, 
                 UnitManager& unit, FirebaseData& fbdo, SystemState& state)
        : networkManager(network), otaManager(ota), lightManager(light), 
          unitManager(unit), fbdo(fbdo), systemState(state) {
        connectionOffset = 1000 + random(100, 10000);
    }
    
    bool begin() {
        delay(connectionOffset);
        
        if (!networkManager.begin()) {
            return false;
        }
        
        updateSystemData();
        otaManager.updateSystemVersion();
        
        // Initialize system power
        pinMode(SystemConfig::SYSTEM_12V_POWER_PIN, OUTPUT);
        digitalWrite(SystemConfig::SYSTEM_12V_POWER_PIN, HIGH);
        
        // Initialize MDNS if system name is available
        if (systemName != "") {
            if (!MDNS.begin(systemName.c_str())) {
                Serial.println("Error setting up MDNS responder!");
                delay(1000);
            }
            networkManager.handleOTA(systemName);
        }
        
        return true;
    }
    
    void update() {
        if (networkManager.isConnected()) {
            unsigned long currentMillis = millis();
            
            // Handle periodic updates (heartbeat, sensors, etc.)
            if (currentMillis - systemState.previousHeartbeatMillis >= SystemConfig::INTERVAL_30_SECONDS + connectionOffset) {
                updateSystemData();
                sendHeartbeat();
                unitManager.readWaterLevel();
                unitManager.readECSensorValue();
                systemState.previousHeartbeatMillis = currentMillis;
                ArduinoOTA.handle();
            }
            
            // Handle unit and light updates
            if (currentMillis - systemState.lastConnectionCheckMillis >= connectionOffset) {
                unitManager.update();
                lightManager.updateSettings(lightMasterSwitch, timeCycleEnabled, lightOnTime, lightOffTime);
                lightManager.update();
                systemState.lastConnectionCheckMillis = currentMillis;
            }
            
            // Check for firmware updates
            if (currentMillis - systemState.lastFirmwareCheckMillis >= SystemConfig::FIRMWARE_CHECK_INTERVAL) {
                Serial.println("Checking for firmware updates...");
                otaManager.checkForUpdates();
                systemState.lastFirmwareCheckMillis = currentMillis;
            }
        } else {
            if (!networkManager.reconnect()) {
                Serial.println("Failed to reconnect. Restarting...");
                ESP.restart();
            }
        }
    }
    
    const String& getSystemName() const { return systemName; }
};

#endif // SYSTEM_MANAGER_H 