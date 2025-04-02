#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <nvs_flash.h>

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
#include "atomizer_manager.h"

#define FIREBASEJSON_USE_PSRAM

// Forward declarations
class WiFiManager;

// ============= Global Objects =============
// Firebase objects for cloud communication
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// WiFi Manager for handling WiFi credentials
WiFiManager wifiManager;

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
SensorManager sensorManager(hardwareManager);  // Pass hardwareManager to constructor

// Initialize Firebase
FirebaseManager firebaseManager(fbdo);  // Only pass fbdo to constructor

// Unit management (3 vertical farming units)
AtomizerManager atomizerManager;
UnitManager unitManager(systemState, firebaseManager, sensorManager, atomizerManager);

// System-wide control
LightManager lightManager;  // Shared lighting system
NetworkManager networkManager(fbdo, auth, config, lightManager, wifiManager);
OTAManager otaManager(fbdo, SystemConfig::systemPath, SystemConfig::SERIAL_NUMBER, 
                     "firmware.skyboard.com", "/firmware/latest.bin", 
                     SystemConfig::FIRMWARE_VERSION);

// Main system coordinator
SystemManager systemManager(networkManager, otaManager, lightManager, unitManager, firebaseManager, systemState);  // Add otaManager to constructor

// Non-volatile storage for settings
Preferences preferences;
PreferencesManager preferencesManager;

void loadSettingsFromStorage() {
    preferencesManager.loadSettings(systemState);
}

void saveSettingsToStorage() {
    preferencesManager.saveSettings(systemState);
}

/**
 * System initialization
 * 1. Initialize serial communication
 * 2. Set up random seed for connection offset
 * 3. Initialize hardware (I2C, multiplexers, sensors)
 * 4. Initialize system (network, Firebase, etc.)
 */
void setup() {
    // Disable watchdog timer during initialization
    disableCore0WDT();
    disableCore1WDT();
    
    // Start serial communication for debugging
    Serial.begin(9600);
    delay(1000);  // Give serial time to initialize
    Serial.println("Starting initialization...");
    
    // Initialize NVS first, before any other components
    Serial.println("Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("NVS partition was truncated and needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        Serial.printf("NVS initialization failed with error: %d\n", ret);
        delay(3000);
        ESP.restart();
    }
    Serial.println("NVS initialized successfully");
    
    // Set up random seed for connection offset
    randomSeed(analogRead(0));
    
    // Initialize Preferences
    Serial.println("Initializing Preferences...");
    if (!preferences.begin("skyboard", false)) {
        Serial.println("CRITICAL ERROR: Failed to initialize Preferences");
        delay(3000);
        ESP.restart();
    }
    Serial.println("Preferences initialized successfully");
    
    // Initialize PreferencesManager
    Serial.println("Initializing PreferencesManager...");
    if (!preferencesManager.begin()) {
        Serial.println("CRITICAL ERROR: Failed to initialize PreferencesManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("PreferencesManager initialized successfully");
    
    // Load settings from storage first
    loadSettingsFromStorage();
    
    // Initialize hardware components (required for operation)
    Serial.println("Initializing hardware...");
    if (!hardwareManager.begin()) {
        Serial.println("Failed to initialize hardware. Restarting...");
        delay(3000);
        ESP.restart();
    }
    Serial.println("Hardware initialized successfully");
    
    // Initialize Firebase configuration
    Serial.println("Initializing Firebase configuration...");
    config.api_key = SystemConfig::FIREBASE_API_KEY;
    config.database_url = SystemConfig::FIREBASE_DATABASE_URL;
    Firebase.begin(&config, &auth);
    Serial.println("Firebase configuration initialized");
    
    // Initialize network first, as it's required for system initialization
    Serial.println("Initializing network...");
    if (!networkManager.begin()) {
        Serial.println("Failed to initialize network. Continuing with stored settings...");
    } else {
        Serial.println("Network initialized successfully");
    }
    
    // Initialize system components
    Serial.println("Initializing system...");
    if (!systemManager.begin()) {
        Serial.println("Failed to initialize system. Continuing with stored settings...");
    } else {
        Serial.println("System initialized successfully");
    }
    
    // Re-enable watchdog timer after initialization
    enableCore0WDT();
    enableCore1WDT();
    
    Serial.println("Initialization complete");
    delay(1000);  // Give time for serial output to complete
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
    systemManager.update();
    
    // WiFi-dependent operations
    if (WiFi.status() == WL_CONNECTED) {
        // Only handle network-specific tasks here
        networkManager.update();
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
    
    // Allow other tasks to run
    yield();
}

