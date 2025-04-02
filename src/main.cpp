#include <Arduino.h>
#include <Wire.h>

// Hardware Libraries
#include "MCP3X21.h"
#include <Protocentral_FDC1004.h>

// Network Libraries
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Firebase_ESP_Client.h>

// System Headers
#include "config.h"
#include "credentials.h"
#include "hardware_manager.h"
#include "light_manager.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "sensor_coms.h"
#include "system_manager.h"
#include "unit_manager.h"

#define FIREBASEJSON_USE_PSRAM

// Global objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FDC1004 fdc;
MCP3021 mcp3021;
SystemState systemState;

// Initialize managers
HardwareManager hardwareManager(mcp3021, fdc);
SensorManager sensorManager(fdc);
NetworkManager networkManager(fbdo, auth, config, serialNumber);
OTAManager otaManager(fbdo, systemPath, serialNumber);
LightManager lightManager;
UnitManager unitManager(systemState, fbdo, sensorManager);
SystemManager systemManager(networkManager, otaManager, lightManager, unitManager, fbdo, systemState);

void setup() {
    Serial.begin(9600);
    randomSeed(analogRead(0));
    
    // Initialize hardware
    hardwareManager.begin();
    
    // Initialize system
    if (!systemManager.begin()) {
        Serial.println("Failed to initialize system. Restarting...");
        delay(3000);
        ESP.restart();
    }
}

void loop() {
    systemManager.update();
}

