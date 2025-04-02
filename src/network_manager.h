#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "config.h"
#include "firebase_config_page.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <Preferences.h>
#include <vector>

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
    
    // WiFiManager parameters (only email and password)
    WiFiManagerParameter custom_email{"email", "Firebase Email", "", CREDENTIAL_MAX_LENGTH};
    WiFiManagerParameter custom_password{"password", "Firebase Password", "", CREDENTIAL_MAX_LENGTH};
    
    // Connection management constants
    static constexpr uint32_t WIFI_TIMEOUT = 10000;  // 10 seconds timeout for WiFi connection
    static constexpr uint32_t FIREBASE_TIMEOUT = SystemConfig::FIREBASE_TIMEOUT;
    static constexpr uint32_t RECONNECT_DELAY = SystemConfig::FIREBASE_RETRY_DELAY_MS;  // Use Firebase retry delay for reconnection
    static constexpr uint8_t MAX_RECONNECT_ATTEMPTS = SystemConfig::MAX_FIREBASE_RETRIES;
    static constexpr uint32_t CONNECTION_CHECK_INTERVAL = 30000;  // 30 seconds
    static constexpr uint32_t CONFIG_PORTAL_TIMEOUT = 60;  // 60 seconds
    static constexpr uint8_t MIN_SIGNAL_QUALITY = 30;  // 30%
    static constexpr float BACKOFF_MULTIPLIER = 1.5f;  // Exponential backoff multiplier
    static constexpr uint32_t MAX_BACKOFF_DELAY = 30000;  // Maximum 30 seconds between attempts
    
    // Connection state tracking
    unsigned long lastConnectionCheck;
    unsigned long lastReconnectAttempt;
    uint8_t reconnectAttempts;
    bool wasConnected;
    uint32_t currentBackoffDelay;
    
    // Firebase credentials with pre-allocated space
    static constexpr size_t CREDENTIAL_MAX_LENGTH = 128;
    String apiKey;  // Will be set from hardcoded value
    String email;
    String password;
    
    // OTA configuration
    static constexpr const char* DEFAULT_OTA_PASSWORD = "admin";
    String otaPassword;
    
    // Preferences for credential storage
    Preferences preferences;
    static constexpr const char* PREF_NAMESPACE = "firebase";
    static constexpr const char* PREF_EMAIL_KEY = "email";
    static constexpr const char* PREF_PASSWORD_KEY = "password";
    
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
     * Loads Firebase credentials from Preferences
     */
    void loadFirebaseCredentials() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in loadFirebaseCredentials");
            return;
        }
        
        // Initialize Preferences
        preferences.begin(PREF_NAMESPACE, false);
        
        // Set hardcoded API key
        apiKey = SystemConfig::FIREBASE_API_KEY;
        
        // Read Email and Password from Preferences
        email = preferences.getString(PREF_EMAIL_KEY, "");
        password = preferences.getString(PREF_PASSWORD_KEY, "");
        
        giveMutex();
    }
    
    /**
     * Validates Firebase credentials
     * @return true if credentials are valid
     */
    bool validateCredentials() {
        // API key is hardcoded, no need to validate
        
        if (email.length() < 5 || email.length() > CREDENTIAL_MAX_LENGTH) {
            Serial.println("Invalid email length");
            return false;
        }
        
        if (password.length() < 6 || password.length() > CREDENTIAL_MAX_LENGTH) {
            Serial.println("Invalid password length");
            return false;
        }
        
        // Basic email format validation
        if (email.indexOf('@') == -1 || email.indexOf('.') == -1) {
            Serial.println("Invalid email format");
            return false;
        }
        
        return true;
    }
    
    /**
     * Saves Firebase credentials to Preferences
     */
    void saveFirebaseCredentials() {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in saveFirebaseCredentials");
            return;
        }
        
        // Validate credentials before saving
        if (!validateCredentials()) {
            Serial.println("Invalid credentials, not saving to Preferences");
            giveMutex();
            return;
        }
        
        // Save to Preferences
        preferences.putString(PREF_EMAIL_KEY, email);
        preferences.putString(PREF_PASSWORD_KEY, password);
        
        giveMutex();
    }
    
    /**
     * Cleans up credential memory
     */
    void cleanupCredentials() {
        apiKey = "";
        email = "";
        password = "";
        otaPassword = "";
        
        // Clear Preferences
        preferences.clear();
    }
    
    /**
     * Sets up the WiFi Manager portal with Firebase configuration
     */
    void setupWiFiManager() {
        // Remove any existing parameters
        wifiManager.resetSettings();
        
        // Add parameters using references
        wifiManager.addParameter(&custom_email);
        wifiManager.addParameter(&custom_password);
        
        // Set custom HTML page
        wifiManager.setCustomHeadElement("<style>body{font-family:Arial,sans-serif;margin:20px;background-color:#f0f0f0;}</style>");
        wifiManager.setCustomHeadElement("<div style='text-align:center;margin-bottom:20px;'><h1>Skyboard Configuration</h1></div>");
        
        // Set custom menu items
        wifiManager.setCustomMenuHTML("<a href='/firebase'>Firebase Settings</a>");
        
        // Set custom save callback
        wifiManager.setSaveConfigCallback([this]() {
            // Get values directly from our parameter objects
            email = custom_email.getValue();
            password = custom_password.getValue();
            
            // Save to Preferences
            saveFirebaseCredentials();
            
            // Reinitialize Firebase with new credentials
            initializeFirebase();
        });
    }
    
    /**
     * Calculates the next backoff delay
     * @return Delay in milliseconds
     */
    uint32_t calculateBackoffDelay() {
        uint32_t delay = RECONNECT_DELAY * pow(BACKOFF_MULTIPLIER, reconnectAttempts);
        return min(delay, MAX_BACKOFF_DELAY);
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
            wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
            wifiManager.setMinimumSignalQuality(MIN_SIGNAL_QUALITY);
            
            // Try to connect
            if (wifiManager.autoConnect("Skyboard_AP")) {
                Serial.println("WiFi connected successfully");
                success = true;
                reconnectAttempts = 0;  // Reset on successful connection
                currentBackoffDelay = RECONNECT_DELAY;  // Reset backoff delay
            } else {
                Serial.println("Failed to connect to WiFi");
                attempts++;
                if (attempts < MAX_RECONNECT_ATTEMPTS) {
                    currentBackoffDelay = calculateBackoffDelay();
                    delay(currentBackoffDelay);
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
            
            config.api_key = apiKey.c_str();
            auth.user.email = email.c_str();
            auth.user.password = password.c_str();
            
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
            if (millis() - lastReconnectAttempt >= currentBackoffDelay) {
                Serial.println("Attempting to reconnect to WiFi...");
                success = connectToWiFi();
                lastReconnectAttempt = millis();
                reconnectAttempts++;
                currentBackoffDelay = calculateBackoffDelay();
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
          reconnectAttempts(0), wasConnected(false),
          custom_email("email", "Firebase Email", "", CREDENTIAL_MAX_LENGTH),
          custom_password("password", "Firebase Password", "", CREDENTIAL_MAX_LENGTH),
          otaPassword(DEFAULT_OTA_PASSWORD) {
        // Pre-allocate space for credentials
        apiKey.reserve(CREDENTIAL_MAX_LENGTH);
        email.reserve(CREDENTIAL_MAX_LENGTH);
        password.reserve(CREDENTIAL_MAX_LENGTH);
        
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("Failed to create mutex in NetworkManager");
        }
        
        // Load Firebase credentials from Preferences
        loadFirebaseCredentials();
        
        // Set up WiFi Manager with Firebase configuration
        setupWiFiManager();
    }
    
    /**
     * Destructor
     */
    ~NetworkManager() {
        if (mutex != NULL) {
            vSemaphoreDelete(mutex);
        }
        
        // Clean up WiFi parameters
        wifiManager.resetSettings();
        
        // Clean up credentials
        cleanupCredentials();
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
        
        // Check connection every CONNECTION_CHECK_INTERVAL
        if (currentMillis - lastConnectionCheck >= CONNECTION_CHECK_INTERVAL) {
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
     * Sets the OTA password
     * @param password New OTA password
     */
    void setOTAPassword(const String& password) {
        if (!takeMutex()) {
            Serial.println("Failed to take mutex in setOTAPassword");
            return;
        }
        
        if (password.length() >= 6 && password.length() <= CREDENTIAL_MAX_LENGTH) {
            otaPassword = password;
        } else {
            Serial.println("Invalid OTA password length");
        }
        
        giveMutex();
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
        ArduinoOTA.setPassword(otaPassword.c_str());
        
        // Start OTA server
        ArduinoOTA.begin();
        Serial.println("OTA server started");
        
        giveMutex();
        return success;
    }
};

#endif // NETWORK_MANAGER_H 