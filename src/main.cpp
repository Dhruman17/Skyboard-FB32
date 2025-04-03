#include <Arduino.h>
#include <Wire.h>
#include <nvs_flash.h>
#include <Firebase_ESP_Client.h>
#include <Preferences.h>
#include "config.h"  // For SystemState definition
#include "preferences_manager.h"
#include "light_manager.h"
#include "system_manager.h"
#include "firebase_manager.h"

// Forward declarations for other managers
class HardwareManager;
class SensorManager;
class UnitManager;
class AtomizerManager;
class NetworkManager;
class OTAManager;
class WiFiManager;

// Declare globals
FirebaseData* fbdo = nullptr;
FirebaseAuth* auth = nullptr;
FirebaseConfig* config = nullptr;
PreferencesManager* preferencesManager = nullptr;
SystemState systemState;
volatile bool networkInitComplete = false;
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
TaskHandle_t networkInitTaskHandle = nullptr;

// Function declarations
void networkInitTask(void* parameter);
void loadSettingsFromStorage();
void saveSettingsToStorage();

void setup() {
    // Initialize serial first - this should be the very first thing we do
    Serial.begin(115200);
    Serial.println("\n\nStarting up...");
    Serial.flush();
    delay(100);  // Give serial time to initialize
    
    // Initialize NVS - this should be done early as other components depend on it
    Serial.println("Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("NVS partition needs erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        Serial.print("NVS initialization failed with error: ");
        Serial.println(ret);
        return;
    }
    Serial.println("NVS initialized successfully");
    
    // Initialize preferences manager - depends on NVS being initialized
    Serial.println("Initializing preferences manager...");
    preferencesManager = new PreferencesManager();
    if (!preferencesManager) {
        Serial.println("Failed to create preferences manager");
        return;
    }
    if (!preferencesManager->begin()) {
        Serial.println("Failed to initialize preferences manager");
        return;
    }
    Serial.println("Preferences manager initialized successfully");
    
    // Give FreeRTOS scheduler time to start up
    Serial.println("Waiting for FreeRTOS scheduler to start...");
    delay(2000);  // Increased delay to ensure scheduler is running
    
    // Create network initialization task with higher priority
    xTaskCreatePinnedToCore(
        networkInitTask,
        "NetworkInit",
        8192,
        NULL,
        2,  // Higher priority
        &networkInitTaskHandle,
        1
    );
    
    Serial.println("Network initialization task created successfully");
}

/**
 * Main program loop
 * Handles all system updates through the SystemManager:
 * - Unit management (water levels, EC, atomizers)
 * - Light control (time-based on/off)
 * - System monitoring and updates
 */
void loop() {
    // Wait for network initialization to complete
    if (!networkInitComplete) {
        delay(100);  // Small delay to prevent tight loop
        return;
    }
    
    // Check if all managers are initialized
    if (networkManager && wifiManager && firebaseManager && otaManager && systemManager && preferencesManager) {
        // Update system state
        if (systemManager) {
            systemManager->update();
        }
        
        // Save settings periodically
        static unsigned long lastSaveTime = 0;
        if (millis() - lastSaveTime >= SystemConfig::SETTINGS_SAVE_INTERVAL) {
            saveSettingsToStorage();
            lastSaveTime = millis();
        }
    } else {
        Serial.println("One or more managers not initialized");
        delay(1000);  // Delay to prevent log spam
    }
    
    // Small delay to ensure watchdog is fed
    yield();
    delay(1);
}

void loadSettingsFromStorage() {
    if (preferencesManager) {
        preferencesManager->loadSettings(systemState);
    }
}

void saveSettingsToStorage() {
    if (preferencesManager) {
        preferencesManager->saveSettings(systemState);
    }
}

void networkInitTask(void* parameter) {
    Serial.println("Network initialization task started");
    
    // Initialize WiFi manager first
    wifiManager = new WiFiManager();
    if (!wifiManager) {
        Serial.println("Failed to create WiFiManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize light manager
    lightManager = new LightManager();
    if (!lightManager) {
        Serial.println("Failed to create LightManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize light manager
    if (!lightManager->begin()) {
        Serial.println("Failed to initialize LightManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Create Firebase objects first
    fbdo = new FirebaseData();
    auth = new FirebaseAuth();
    config = new FirebaseConfig();
    
    if (!fbdo || !auth || !config) {
        Serial.println("Failed to create Firebase objects");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize network manager with required parameters
    networkManager = new NetworkManager(*fbdo, *auth, *config, *lightManager, *wifiManager);
    if (!networkManager) {
        Serial.println("Failed to create NetworkManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize network manager and wait for WiFi connection
    if (!networkManager->begin()) {
        Serial.println("Failed to initialize NetworkManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Wait for WiFi connection with timeout
    Serial.println("Waiting for WiFi connection...");
    const unsigned long wifiTimeout = 30000;  // 30 seconds timeout
    const unsigned long startTime = millis();
    bool wifiConnected = false;
    
    while (!(wifiConnected = networkManager->isConnected()) && 
           (millis() - startTime) < wifiTimeout) {
        delay(1000);
        Serial.println("Waiting for WiFi connection...");
    }
    
    if (!wifiConnected) {
        Serial.println("WiFi connection timeout - proceeding in offline mode");
        // Set offline mode
        config->api_key = SystemConfig::FIREBASE_API_KEY;
        config->timeout.serverResponse = 0;  // No timeout for offline mode
        
        // Initialize Firebase manager
        firebaseManager = new FirebaseManager(*fbdo);
        if (!firebaseManager) {
            Serial.println("Failed to create FirebaseManager");
            vTaskDelete(NULL);
            return;
        }
        
        // Set system state to offline
        systemState.isOnline = false;
        systemState.lastConnectionAttempt = millis();
        systemState.connectionStatus = "offline";
        
        // Continue with initialization in offline mode
        Serial.println("Proceeding with offline initialization...");
    } else {
        Serial.println("WiFi connected successfully");
        
        // Configure Firebase for online mode
        config->api_key = SystemConfig::FIREBASE_API_KEY;
        
        // Initialize Firebase manager
        firebaseManager = new FirebaseManager(*fbdo);
        if (!firebaseManager) {
            Serial.println("Failed to create FirebaseManager");
            vTaskDelete(NULL);
            return;
        }
        
        // Set system state to online
        systemState.isOnline = true;
        systemState.lastConnectionAttempt = millis();
        systemState.connectionStatus = "online";
    }
    
    // Initialize hardware manager
    hardwareManager = new HardwareManager();
    if (!hardwareManager) {
        Serial.println("Failed to create HardwareManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize hardware manager
    if (!hardwareManager->begin()) {
        Serial.println("Failed to initialize HardwareManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize sensor manager
    sensorManager = new SensorManager(*hardwareManager);
    if (!sensorManager) {
        Serial.println("Failed to create SensorManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize sensor manager
    if (!sensorManager->begin()) {
        Serial.println("Failed to initialize SensorManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize atomizer manager
    atomizerManager = new AtomizerManager();
    if (!atomizerManager) {
        Serial.println("Failed to create AtomizerManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize atomizer manager
    if (!atomizerManager->begin()) {
        Serial.println("Failed to initialize AtomizerManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize unit manager
    unitManager = new UnitManager(systemState, *firebaseManager, *sensorManager, *atomizerManager);
    if (!unitManager) {
        Serial.println("Failed to create UnitManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize unit manager
    if (!unitManager->begin()) {
        Serial.println("Failed to initialize UnitManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize OTA manager with all required parameters
    otaManager = new OTAManager(*firebaseManager,
                              SystemConfig::SYSTEM_PATH_FORMAT,
                              SystemConfig::SERIAL_NUMBER,
                              SystemConfig::FIRMWARE_URL,
                              SystemConfig::FIRMWARE_PATH,
                              String(SystemConfig::FIRMWARE_VERSION));
    if (!otaManager) {
        Serial.println("Failed to create OTAManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize OTA manager
    if (!otaManager->begin()) {
        Serial.println("Failed to initialize OTAManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize system manager with all required parameters
    systemManager = new SystemManager(*networkManager, *otaManager, *lightManager,
                                    *unitManager, *firebaseManager, systemState);
    if (!systemManager) {
        Serial.println("Failed to create SystemManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Initialize system manager
    if (!systemManager->begin()) {
        Serial.println("Failed to initialize SystemManager");
        vTaskDelete(NULL);
        return;
    }
    
    // Signal that initialization is complete
    networkInitComplete = true;
    Serial.println("Network initialization completed");
    vTaskDelete(NULL);
}

