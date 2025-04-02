#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "config.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoOTA.h>
#include <time.h>

/**
 * NetworkManager Class
 * 
 * Handles all network-related operations:
 * 1. WiFi Connection:
 *    - Automatic connection to known networks
 *    - Fallback to AP mode for manual configuration
 *    - Connection state monitoring and recovery
 * 
 * 2. Firebase Integration:
 *    - Authentication and configuration
 *    - Connection state management
 *    - Error handling and recovery
 * 
 * 3. OTA Updates:
 *    - Hostname configuration
 *    - Update server setup
 * 
 * Connection Management:
 * - Auto-reconnect enabled
 * - Persistent WiFi settings
 * - Connection check every 30 seconds
 * - Reconnection attempts every 5 seconds
 */
class NetworkManager {
private:
    FirebaseData& fbdo;
    FirebaseAuth& auth;
    FirebaseConfig& config;
    WiFiManager wifiManager;
    bool initialized;
    SemaphoreHandle_t mutex;
    
    // Connection management constants
    static constexpr uint32_t WIFI_TIMEOUT = 10000;  // 10 seconds timeout for WiFi connection
    static constexpr uint32_t FIREBASE_TIMEOUT = 10000;  // 10 seconds timeout for Firebase
    static constexpr uint32_t RECONNECT_DELAY = 5000;  // 5 seconds between reconnection attempts
    static constexpr uint8_t MAX_RECONNECT_ATTEMPTS = 3;  // Maximum number of reconnection attempts
    
    // Connection state tracking
    unsigned long lastConnectionCheck;
    unsigned long lastReconnectAttempt;
    uint8_t reconnectAttempts;
    bool wasConnected;
    
    /**
     * Safely takes the mutex with timeout
     * @return true if mutex was taken successfully
     */
    bool takeMutex() {
        return xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE;
    }
    
    /**
     * Safely gives the mutex
     */
    void giveMutex() {
        xSemaphoreGive(mutex);
    }
    
    /**
     * Connects to WiFi with timeout and retry mechanism
     * @return true if connection successful
     */
    bool connectToWiFi() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in connectToWiFi");
            return false;
        }
        
        bool success = false;
        uint8_t attempts = 0;
        
        while (!success && attempts < MAX_RECONNECT_ATTEMPTS) {
            Serial.println("Attempting to connect to WiFi...");
            
            // Configure WiFiManager
            wifiManager.setConfigPortalTimeout(60);  // 60 second timeout for config portal
            wifiManager.setMinimumSignalQuality(30);  // Minimum signal quality (30%)
            
            // Try to connect
            if (wifiManager.autoConnect("Skyboard_AP")) {
                Serial.println("WiFi connected successfully");
                success = true;
            } else {
                Serial.println("Failed to connect to WiFi");
                attempts++;
                if (attempts < MAX_RECONNECT_ATTEMPTS) {
                    delay(RECONNECT_DELAY);
                }
            }
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Initializes time synchronization
     * @return true if initialization successful
     */
    bool initializeTime() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in initializeTime");
            return false;
        }
        
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        
        // Wait for time to be set
        unsigned long startTime = millis();
        time_t now;
        time(&now);
        while (now < 1000000000 && millis() - startTime < WIFI_TIMEOUT) { // Check if time is set (after year 2000)
            delay(100);
            time(&now);
        }
        
        bool success = (now >= 1000000000);
        if (!success) {
            Serial.println("Failed to initialize time");
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Initializes Firebase with timeout and retry mechanism
     * @return true if initialization successful
     */
    bool initializeFirebase() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in initializeFirebase");
            return false;
        }
        
        bool success = false;
        uint8_t attempts = 0;
        
        while (!success && attempts < MAX_RECONNECT_ATTEMPTS) {
            Serial.println("Initializing Firebase...");
            
            config.api_key = API_KEY;
            auth.user.email = USER_EMAIL;
            auth.user.password = USER_PASSWORD;
            
            Firebase.begin(&config, &auth);
            Firebase.reconnectWiFi(true);
            
            // Wait for Firebase to be ready
            unsigned long startTime = millis();
            while (!Firebase.ready() && millis() - startTime < FIREBASE_TIMEOUT) {
                delay(100);
            }
            
            if (Firebase.ready()) {
                Serial.println("Firebase initialized successfully");
                success = true;
            } else {
                Serial.println("Failed to initialize Firebase");
                attempts++;
                if (attempts < MAX_RECONNECT_ATTEMPTS) {
                    delay(RECONNECT_DELAY);
                }
            }
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Checks if WiFi connection is stable
     * @return true if connection is stable
     */
    bool checkWiFiConnection() {
        if (!takeMutex()) {
            return false;
        }
        
        bool isConnected = WiFi.status() == WL_CONNECTED;
        
        // If connection state changed
        if (isConnected != wasConnected) {
            wasConnected = isConnected;
            if (isConnected) {
                Serial.println("WiFi reconnected");
                reconnectAttempts = 0;  // Reset reconnect attempts on successful connection
            } else {
                Serial.println("WiFi disconnected");
            }
        }
        
        giveMutex();
        return isConnected;
    }
    
    /**
     * Attempts to reconnect to WiFi if disconnected
     * @return true if reconnection successful
     */
    bool attemptReconnect() {
        if (!takeMutex()) {
            return false;
        }
        
        bool success = false;
        if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
            if (millis() - lastReconnectAttempt >= RECONNECT_DELAY) {
                Serial.println("Attempting to reconnect to WiFi...");
                success = connectToWiFi();
                lastReconnectAttempt = millis();
                reconnectAttempts++;
            }
        }
        
        giveMutex();
        return success;
    }

public:
    /**
     * Constructor
     * @param fbdo Reference to Firebase data object
     * @param auth Reference to Firebase auth object
     * @param config Reference to Firebase config object
     */
    NetworkManager(FirebaseData& fbdo, FirebaseAuth& auth, FirebaseConfig& config)
        : fbdo(fbdo), auth(auth), config(config), initialized(false),
          lastConnectionCheck(0), lastReconnectAttempt(0),
          reconnectAttempts(0), wasConnected(false) {
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("Failed to create mutex in NetworkManager");
        }
    }
    
    /**
     * Destructor
     */
    ~NetworkManager() {
        if (mutex != NULL) {
            vSemaphoreDelete(mutex);
        }
    }
    
    /**
     * Initializes network components
     * Sets up WiFi, Firebase, and time synchronization
     * @return true if initialization successful
     */
    bool begin() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in begin");
            return false;
        }
        
        bool success = true;
        
        if (!connectToWiFi()) {
            Serial.println("Wi-Fi setup failed");
            success = false;
        }
        
        if (success && !initializeTime()) {
            Serial.println("Time initialization failed");
            success = false;
        }
        
        if (success && !initializeFirebase()) {
            Serial.println("Firebase initialization failed");
            success = false;
        }
        
        if (success) {
            initialized = true;
            wasConnected = true;
        }
        
        giveMutex();
        return success;
    }
    
    /**
     * Updates network state and handles reconnection
     * Should be called periodically in the main loop
     */
    void update() {
        if (!initialized) {
            return;
        }
        
        unsigned long currentMillis = millis();
        
        // Check connection every 30 seconds
        if (currentMillis - lastConnectionCheck >= 30000) {
            if (!checkWiFiConnection()) {
                attemptReconnect();
            }
            lastConnectionCheck = currentMillis;
        }
    }
    
    /**
     * Checks if WiFi is connected
     * @return true if connected
     */
    bool isConnected() {
        if (!takeMutex()) {
            return false;
        }
        
        bool connected = WiFi.status() == WL_CONNECTED;
        giveMutex();
        return connected;
    }
    
    /**
     * Gets the current WiFi signal strength
     * @return Signal strength in dBm
     */
    int getSignalStrength() {
        if (!takeMutex()) {
            return 0;
        }
        
        int strength = WiFi.RSSI();
        giveMutex();
        return strength;
    }

    /**
     * Handles OTA updates for the system
     * @param systemName Name of the system
     * @return true if OTA handling successful
     */
    bool handleOTA(String systemName) {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in handleOTA");
            return false;
        }
        
        bool success = true;
        
        // Configure ArduinoOTA
        ArduinoOTA.setHostname(systemName.c_str());
        ArduinoOTA.setPassword("admin");
        
        // Start OTA server
        ArduinoOTA.begin();
        Serial.println("OTA server started");
        
        giveMutex();
        return success;
    }
};

#endif // NETWORK_MANAGER_H 