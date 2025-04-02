#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>

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
#include "preferences_manager.h"
#include "firebase_manager.h"

#define FIREBASEJSON_USE_PSRAM

// ============= Global Objects =============
// Firebase objects for cloud communication
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Hardware sensors (3 units, each with their own sensors)
// Each unit has its own PCA9546A multiplexer connecting to:
// - FDC1004 (capacitive sensor for water level)
// - MCP3021 (ADC for EC sensor)
FDC1004 fdc[3];     // One per unit
MCP3021 mcp3021[3]; // One per unit
SystemState systemState;  // System state tracking

// ============= System Managers =============
// Core hardware management
// Handles both TCA9548APWR (system level) and PCA9546A (unit level) multiplexers
HardwareManager hardwareManager;
SensorManager sensorManager(hardwareManager);

// Initialize Firebase
FirebaseManager firebaseManager(fbdo);

// Unit management (3 vertical farming units)
UnitManager unitManager(systemState, firebaseManager, sensorManager);

// System-wide control
LightManager lightManager;  // Shared lighting system
NetworkManager networkManager(fbdo, auth, config);
OTAManager otaManager(fbdo, SystemConfig::systemPath, SystemConfig::SERIAL_NUMBER, 
                     "firmware.skyboard.com", "/firmware/latest.bin", 
                     SystemConfig::FIRMWARE_VERSION);

// Main system coordinator
SystemManager systemManager(networkManager, otaManager, lightManager, unitManager, firebaseManager, systemState);

// Non-volatile storage for settings
PreferencesManager preferencesManager;

void loadSettingsFromStorage() {
    preferencesManager.loadSettings(systemState);
}

void saveSettingsToStorage() {
    preferencesManager.saveSettings(systemState);
}

// WiFi Manager for handling WiFi credentials
WiFiManager wifiManager;

/**
 * System initialization
 * 1. Initialize serial communication
 * 2. Set up random seed for connection offset
 * 3. Initialize hardware (I2C, multiplexers, sensors)
 * 4. Initialize system (network, Firebase, etc.)
 */
void setup() {
    // Start serial communication for debugging
    Serial.begin(115200);
    randomSeed(analogRead(0));
    
    // Load settings from storage first
    loadSettingsFromStorage();
    
    // Initialize Firebase
    config.api_key = SystemConfig::FIREBASE_API_KEY;
    config.database_url = SystemConfig::FIREBASE_DATABASE_URL;
    Firebase.begin(&config, &auth);
    
    // Initialize hardware components (required for operation)
    if (!hardwareManager.begin()) {
        Serial.println("Failed to initialize hardware. Restarting...");
        delay(3000);
        ESP.restart();
    }
    
    // Try to initialize network and system components
    bool networkSuccess = networkManager.begin();
    bool systemSuccess = systemManager.begin();
    
    if (!networkSuccess) {
        Serial.println("Failed to initialize network. Continuing with stored settings...");
    }
    
    if (!systemSuccess) {
        Serial.println("Failed to initialize system. Continuing with stored settings...");
    }
    
    Serial.println("Initialization complete");
}

/**
 * Main program loop
 * Handles all system updates through the SystemManager:
 * - Unit management (water levels, EC, atomizers)
 * - Light control (time-based on/off)
 * - System monitoring and updates
 */
void loop() {
    // Core functionality runs regardless of WiFi status
    // systemManager.update();  // Removed redundant call
    
    // WiFi-dependent operations
    if (WiFi.status() == WL_CONNECTED) {
        systemManager.update();
    } else {
        // Try to reconnect WiFi periodically
        if (millis() - systemState.lastReconnectAttempt >= SystemConfig::WIFI_RECONNECT_INTERVAL) {
            Serial.println("Wi-Fi disconnected. Attempting reconnect...");
            if (networkManager.isConnected()) {
                Serial.println("Wi-Fi reconnected successfully");
            } else {
                Serial.println("Failed to reconnect. Continuing with stored settings...");
            }
            systemState.lastReconnectAttempt = millis();
        }
    }
}

