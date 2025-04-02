#ifndef UNIT_MANAGER_H
#define UNIT_MANAGER_H

#include "config.h"
#include "sensor_coms.h"
#include <Firebase_ESP_Client.h>

// Firebase path constant
const char* systemPath = "systems";

class UnitManager {
private:
    SystemState& systemState;
    FirebaseData& fbdo;
    SensorManager& sensorManager;
    String unitNames[SystemConfig::NUMBER_OF_UNITS];
    unsigned long previousMillis[SystemConfig::NUMBER_OF_UNITS];
    bool atomStates[SystemConfig::NUMBER_OF_UNITS];
    
    void readECSensorValue() {
        float ecValue = sensorManager.readECSensorValue();
        if (ecValue >= 0) {
            for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                if (unitNames[i] != "") {
                    String documentPath = systemPath + "/units/" + unitNames[i];
                    FirebaseJson content;
                    content.set("fields/ecValue/doubleValue", ecValue);
                    if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "ecValue")) {
                        Serial.println("Updated EC value for " + unitNames[i]);
                    } else {
                        Serial.println("Failed to update EC value for " + unitNames[i]);
                    }
                }
            }
        }
    }
    
    void updateWaterLevelStates(int i) {
        if (digitalRead(SystemConfig::WATER_LEVEL_PINS[i]) == LOW) {
            systemState.waterLevelStates[i] = false;
        } else {
            systemState.waterLevelStates[i] = true;
        }
        
        if (systemState.waterLevelStates[i] != systemState.previousWaterLevelStates[i]) {
            String documentPath = systemPath + "/units/" + unitNames[i];
            FirebaseJson content;
            content.set("fields/waterLevelState/booleanValue", systemState.waterLevelStates[i]);
            if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "waterLevelState")) {
                Serial.println("Updated water level state for " + unitNames[i]);
                systemState.previousWaterLevelStates[i] = systemState.waterLevelStates[i];
            } else {
                Serial.println("Failed to update water level state for " + unitNames[i]);
            }
        }
    }

public:
    UnitManager(SystemState& state, FirebaseData& fbdo, SensorManager& sensor)
        : systemState(state), fbdo(fbdo), sensorManager(sensor) {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            previousMillis[i] = 0;
            atomStates[i] = false;
        }
    }
    
    void updateUnitNames(const String names[SystemConfig::NUMBER_OF_UNITS]) {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            unitNames[i] = names[i];
        }
    }
    
    void updateUnitData() {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            String documentPath = systemPath + "/units/" + unitNames[i];
            if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())) {
                FirebaseJson json;
                json.setJsonData(fbdo.payload());
                FirebaseJsonData jsonData;
                
                if (json.get(jsonData, "fields/unitState/booleanValue")) {
                    bool newState = jsonData.boolValue;
                    if (systemState.unitsEnabled[i] != newState) {
                        systemState.unitsEnabled[i] = newState;
                        if (systemState.unitsEnabled[i]) {
                            Serial.println("Turning on LED for " + String(unitNames[i]));
                            ledcWrite(i, SystemConfig::PWM_ATOMIZER_ON);
                        } else {
                            Serial.println("Turning off LED for " + String(unitNames[i]));
                            ledcWrite(i, SystemConfig::PWM_ATOMIZER_OFF);
                        }
                    }
                }
                
                if (json.get(jsonData, "fields/Interval_On/integerValue")) {
                    systemState.atomizerOnIntervals[i] = jsonData.intValue * 1000;
                }
                
                if (json.get(jsonData, "fields/Interval_Off/integerValue")) {
                    systemState.atomizerOffIntervals[i] = jsonData.intValue * 1000;
                }
            }
        }
    }
    
    void update() {
        unsigned long currentMillis = millis();
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (systemState.unitsEnabled[i]) {
                if (currentMillis - previousMillis[i] >= (atomStates[i] ? systemState.atomizerOnIntervals[i] : systemState.atomizerOffIntervals[i])) {
                    atomStates[i] = !atomStates[i];
                    ledcWrite(i, atomStates[i] ? SystemConfig::PWM_ATOMIZER_ON : SystemConfig::PWM_ATOMIZER_OFF);
                    
                    if (atomStates[i] == false) {
                        updateWaterLevelStates(i);
                    }
                    
                    previousMillis[i] = currentMillis;
                }
            }
        }
    }
    
    void readWaterLevel() {
        float waterLevels[SystemConfig::NUMBER_OF_UNITS];
        if (sensorManager.readAllWaterLevels(waterLevels)) {
            for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                if (unitNames[i] != "") {
                    String documentPath = systemPath + "/units/" + unitNames[i];
                    FirebaseJson content;
                    content.set("fields/waterLevel/doubleValue", waterLevels[i]);
                    if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "waterLevel")) {
                        Serial.println("Updated water level for " + unitNames[i]);
                    } else {
                        Serial.println("Failed to update water level for " + unitNames[i]);
                    }
                }
            }
        }
    }
};

#endif // UNIT_MANAGER_H 