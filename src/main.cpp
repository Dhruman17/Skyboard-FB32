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
class NetworkManager;
class SystemManager;
class UnitManager;
class FirebaseManager;
class SensorManager;
class HardwareManager;
class LightManager;
class OTAManager;

// ============= Global Objects =============
// Firebase objects for cloud communication (lightweight, can be global)
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
HardwareManager* hardwareManager = nullptr;
SensorManager* sensorManager = nullptr;
FirebaseManager* firebaseManager = nullptr;
UnitManager* unitManager = nullptr;
AtomizerManager* atomizerManager = nullptr;
LightManager* lightManager = nullptr;
NetworkManager* networkManager = nullptr;
OTAManager* otaManager = nullptr;
SystemManager* systemManager = nullptr;
WiFiManager* wifiManager = nullptr;

// Non-volatile storage for settings (lightweight, can be global)
Preferences preferences;
PreferencesManager* preferencesManager = nullptr;

// FreeRTOS task handle for network initialization
TaskHandle_t networkInitTaskHandle = NULL;

// Network initialization task
void networkInitTask(void *parameter) {
    NetworkManager* networkManager = (NetworkManager*)parameter;
    
    // Initialize network with timeout and yield
    Serial.println("Starting network initialization...");
    if (!networkManager->begin()) {
        Serial.println("Failed to initialize network. Continuing with stored settings...");
    } else {
        Serial.println("Network initialized successfully");
    }
    
    // Initialize Firebase configuration
    Serial.println("Initializing Firebase configuration...");
    config.api_key = SystemConfig::FIREBASE_API_KEY;
    config.database_url = SystemConfig::FIREBASE_DATABASE_URL;
    Firebase.begin(&config, &auth);
    Serial.println("Firebase configuration initialized");
    
    // Delete the task when done
    vTaskDelete(NULL);
}

void loadSettingsFromStorage() {
    if (preferencesManager != nullptr) {
        preferencesManager->loadSettings(systemState);
    }
}

void saveSettingsToStorage() {
    if (preferencesManager != nullptr) {
        preferencesManager->saveSettings(systemState);
    }
}

/**
 * System initialization
 * 1. Initialize serial communication
 * 2. Set up random seed for connection offset
 * 3. Initialize hardware (I2C, multiplexers, sensors)
 * 4. Initialize system (network, Firebase, etc.)
 */
void setup() {
    // Disable all watchdog timers during initialization
    disableCore0WDT();
    disableCore1WDT();
    
    // Start serial communication for debugging
    Serial.begin(115200);  // Match platformio.ini setting
    delay(2000);  // Give more time for serial to initialize
    Serial.println("\n\n");  // Add some newlines to clear any garbage
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
    
    // Initialize Preferences with explicit namespace
    Serial.println("Initializing Preferences...");
    preferences.end();  // Ensure any existing preferences are closed
    if (!preferences.begin("skyboard", false)) {
        Serial.println("CRITICAL ERROR: Failed to initialize Preferences");
        delay(3000);
        ESP.restart();
    }
    Serial.println("Preferences initialized successfully");
    
    // Initialize PreferencesManager
    Serial.println("Initializing PreferencesManager...");
    preferencesManager = new PreferencesManager();
    if (preferencesManager == nullptr) {
        Serial.println("CRITICAL ERROR: Failed to create PreferencesManager");
        delay(3000);
        ESP.restart();
    }
    
    // Ensure preferences are properly initialized before calling begin
    delay(100);  // Give time for preferences to settle
    
    if (!preferencesManager->begin()) {
        Serial.println("CRITICAL ERROR: Failed to initialize PreferencesManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("PreferencesManager initialized successfully");
    
    // Load settings from storage first
    loadSettingsFromStorage();
    
    // Initialize hardware components
    Serial.println("Initializing hardware...");
    hardwareManager = new HardwareManager();
    if (hardwareManager == nullptr || !hardwareManager->begin()) {
        Serial.println("CRITICAL ERROR: Failed to initialize HardwareManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("HardwareManager initialized successfully");
    
    // Initialize SensorManager
    Serial.println("Initializing SensorManager...");
    sensorManager = new SensorManager(*hardwareManager);
    if (sensorManager == nullptr) {
        Serial.println("CRITICAL ERROR: Failed to create SensorManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("SensorManager created successfully");
    
    // Initialize FirebaseManager
    Serial.println("Initializing FirebaseManager...");
    firebaseManager = new FirebaseManager(fbdo);
    if (firebaseManager == nullptr) {
        Serial.println("CRITICAL ERROR: Failed to create FirebaseManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("FirebaseManager created successfully");
    
    // Initialize AtomizerManager
    Serial.println("Initializing AtomizerManager...");
    atomizerManager = new AtomizerManager();
    if (atomizerManager == nullptr) {
        Serial.println("CRITICAL ERROR: Failed to create AtomizerManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("AtomizerManager created successfully");
    
    // Initialize UnitManager
    Serial.println("Initializing UnitManager...");
    unitManager = new UnitManager(systemState, *firebaseManager, *sensorManager, *atomizerManager);
    if (unitManager == nullptr) {
        Serial.println("CRITICAL ERROR: Failed to create UnitManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("UnitManager created successfully");
    
    // Initialize LightManager
    Serial.println("Initializing LightManager...");
    lightManager = new LightManager();
    if (lightManager == nullptr) {
        Serial.println("CRITICAL ERROR: Failed to create LightManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("LightManager created successfully");
    
    // Initialize WiFiManager with shorter timeout
    Serial.println("Initializing WiFiManager...");
    wifiManager = new WiFiManager();
    if (wifiManager == nullptr) {
        Serial.println("CRITICAL ERROR: Failed to create WiFiManager");
        delay(3000);
        ESP.restart();
    }
    // Set shorter timeout for WiFi portal
    wifiManager->setConfigPortalTimeout(60);  // Reduce from 180 to 60 seconds
    Serial.println("WiFiManager created successfully");
    
    // Initialize NetworkManager
    Serial.println("Initializing NetworkManager...");
    networkManager = new NetworkManager(fbdo, auth, config, *lightManager, *wifiManager);
    if (networkManager == nullptr) {
        Serial.println("CRITICAL ERROR: Failed to create NetworkManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("NetworkManager created successfully");
    
    // Initialize OTAManager
    Serial.println("Initializing OTAManager...");
    otaManager = new OTAManager(fbdo, SystemConfig::systemPath, SystemConfig::SERIAL_NUMBER, 
                               "firmware.skyboard.com", "/firmware/latest.bin", 
                               SystemConfig::FIRMWARE_VERSION);
    if (otaManager == nullptr) {
        Serial.println("CRITICAL ERROR: Failed to create OTAManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("OTAManager created successfully");
    
    // Initialize SystemManager
    Serial.println("Initializing SystemManager...");
    systemManager = new SystemManager(*networkManager, *otaManager, *lightManager, 
                                    *unitManager, *firebaseManager, systemState);
    if (systemManager == nullptr) {
        Serial.println("CRITICAL ERROR: Failed to create SystemManager");
        delay(3000);
        ESP.restart();
    }
    Serial.println("SystemManager created successfully");
    
    // Initialize system components
    Serial.println("Starting system initialization...");
    if (!systemManager->begin()) {
        Serial.println("Failed to initialize system. Continuing with stored settings...");
    } else {
        Serial.println("System initialized successfully");
    }
    
    // Create network initialization task
    Serial.println("Creating network initialization task...");
    if (xTaskCreate(
        networkInitTask,          // Task function
        "NetworkInit",           // Task name
        4096,                    // Stack size
        networkManager,          // Task parameter
        1,                       // Priority
        &networkInitTaskHandle   // Task handle
    ) != pdPASS) {
        Serial.println("CRITICAL ERROR: Failed to create network initialization task");
        delay(3000);
        ESP.restart();
    }
    Serial.println("Network initialization task created successfully");
    
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
    if (systemManager == nullptr || networkManager == nullptr) {
        Serial.println("CRITICAL ERROR: System or Network manager not initialized");
        delay(1000);
        return;
    }
    
    // Core functionality runs regardless of WiFi status
    systemManager->update();
    
    // WiFi-dependent operations
    if (WiFi.status() == WL_CONNECTED) {
        // Only handle network-specific tasks here
        networkManager->update();
    } else {
        // Try to reconnect WiFi periodically
        if (millis() - systemState.lastReconnectAttempt >= SystemConfig::WIFI_RECONNECT_INTERVAL) {
            Serial.println("Wi-Fi disconnected. Attempting reconnect...");
            if (networkManager->isConnected()) {
                Serial.println("Wi-Fi reconnected successfully");
            } else {
                Serial.println("Failed to reconnect. Continuing with stored settings...");
            }
            systemState.lastReconnectAttempt = millis();
        }
    }
    
    // Allow other tasks to run and feed watchdog
    yield();
    delay(1);  // Small delay to ensure watchdog is fed
}

