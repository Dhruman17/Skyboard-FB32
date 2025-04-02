#ifndef UNIT_MANAGER_H
#define UNIT_MANAGER_H

#include "config.h"
#include "sensor_coms.h"
#include "firebase_coms.h"
#include <Firebase_ESP_Client.h>

class UnitManager {
private:
    SystemState& systemState;
    FirebaseData& fbdo;
    SensorManager& sensorManager;
    String unitNames[SystemConfig::NUMBER_OF_UNITS];
    
    void readECSensorValue() {
        float calibratedECs[SystemConfig::NUMBER_OF_UNITS];
        int tcaChannels[SystemConfig::NUMBER_OF_UNITS] = {1, 3, 5};

        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            tcaselect(tcaChannels[i]);
            uint16_t result = mcp3021.read();
            float rawEc = (mcp3021.toVoltage(result, 3300) / 1000.0);
            float calibratedEC = 0.727 - (0.365 * rawEc) + (0.416 * rawEc * rawEc);
            calibratedECs[i] = calibratedEC;

            Serial.printf("EC sensor %d reading: %.3f\n", i + 1, calibratedEC);

            if (!unitNames[i].isEmpty()) {
                sendUnitECValueToFirebase(&fbdo, unitNames[i], calibratedEC);
            }
        }
    }

public:
    UnitManager(SystemState& state, FirebaseData& fbdo, SensorManager& sensor)
        : systemState(state), fbdo(fbdo), sensorManager(sensor) {
        // Initialize pins
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            pinMode(SystemConfig::WATER_LEVEL_PINS[i], INPUT_PULLUP);
            ledcSetup(i, SystemConfig::PWM_FREQUENCY_ATOMIZER, SystemConfig::PWM_RESOLUTION_ATOMIZER);
            ledcAttachPin(SystemConfig::ATOMIZER_PINS[i], i);
        }
    }
    
    void update() {
        unsigned long currentMillis = millis();
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            if (systemState.unitsEnabled[i]) {
                if (currentMillis - systemState.previousMillis[i] >= 
                    (systemState.atomStates[i] ? systemState.atomizerOnIntervals[i] : systemState.atomizerOffIntervals[i])) {
                    
                    systemState.atomStates[i] = !systemState.atomStates[i];
                    ledcWrite(i, systemState.atomStates[i] ? SystemConfig::PWM_ATOMIZER_ON : SystemConfig::PWM_ATOMIZER_OFF);
                    
                    if (!systemState.atomStates[0] && !systemState.atomStates[1] && !systemState.atomStates[2]) {
                        readECSensorValue();
                    }
                    
                    systemState.previousMillis[i] = currentMillis;
                }
            }
        }
    }
    
    void readWaterLevel() {
        float waterLevels[SystemConfig::NUMBER_OF_UNITS];
        
        if (sensorManager.readAllWaterLevels(waterLevels)) {
            for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                if (!unitNames[i].isEmpty()) {
                    sendUnitCapValueToFirebase(&fbdo, unitNames[i], waterLevels[i]);
                }
            }
        }
    }
    
    void updateUnitNames(const String names[SystemConfig::NUMBER_OF_UNITS]) {
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            unitNames[i] = names[i];
        }
    }
    
    void updateUnitData() {
        fetchFirebaseUnitData(&fbdo, systemState.unitsEnabled, 
                            systemState.atomizerOnIntervals, 
                            systemState.atomizerOffIntervals, 
                            unitNames);
    }
};

#endif // UNIT_MANAGER_H 