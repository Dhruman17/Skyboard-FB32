#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "firebase_config_page.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <Preferences.h>
#include <vector>
#include "mutex_manager.h"

// Static buffer sizes
static constexpr size_t ERROR_BUFFER_SIZE = 256;
static constexpr size_t ERROR_LOCATION_SIZE = 64;
static constexpr size_t ERROR_LOG_COUNT = 10;

// Error log structure
struct ErrorLog {
    char message[ERROR_BUFFER_SIZE];
    char location[ERROR_LOCATION_SIZE];
    ErrorManager::ErrorCode code;
    unsigned long timestamp;
};

// Static error buffers and ring buffer
static char errorMsgBuffer[ERROR_BUFFER_SIZE];
static char errorLocBuffer[ERROR_LOCATION_SIZE];
static ErrorLog errorLogs[ERROR_LOG_COUNT];
static uint8_t currentErrorIndex = 0;

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
    // Constants
    static constexpr uint32_t WIFI_TIMEOUT = 20000;  // 20 seconds
    static constexpr uint32_t FIREBASE_TIMEOUT = SystemConfig::FIREBASE_TIMEOUT;
    static constexpr uint32_t RECONNECT_DELAY = 5000;  // 5 seconds
    static constexpr uint8_t MAX_RECONNECT_ATTEMPTS = 3;
    static constexpr uint32_t CONNECTION_CHECK_INTERVAL = 30000;  // 30 seconds
    static constexpr uint32_t CONFIG_PORTAL_TIMEOUT = 180;  // 3 minutes
    static constexpr int MIN_SIGNAL_QUALITY = 30;  // Minimum WiFi signal quality in dBm
    static constexpr float BACKOFF_MULTIPLIER = 1.5f;  // Exponential backoff multiplier
    static constexpr uint32_t MAX_BACKOFF_DELAY = 30000;  // Maximum 30 seconds between attempts
    static constexpr uint32_t MUTEX_TIMEOUT_MS = 500;  // 500ms timeout for mutex operations
    static constexpr size_t CREDENTIAL_MAX_LENGTH = 128;
    static constexpr size_t SYSTEM_NAME_MAX_LENGTH = 64;  // Maximum length for system names
    
    // AP configuration
    static constexpr const char* AP_NAME = "Skyboard_AP";
    static constexpr const char* AP_PASSWORD = "skyboard123";
    
    // Preferences constants
    static constexpr const char* PREF_NAMESPACE = "firebase";
    static constexpr const char* PREF_EMAIL_KEY = "email";
    static constexpr const char* PREF_PASSWORD_KEY = "password";
    
    // OTA configuration
    static constexpr const char* DEFAULT_OTA_PASSWORD = "admin";
    
    // Firebase objects
    FirebaseData& fbdo;
    FirebaseAuth& auth;
    FirebaseConfig& config;
    LightManager& lightManager;
    
    // WiFi Manager reference
    WiFiManager& wifiManager;
    
    // WiFi parameters
    WiFiManagerParameter* custom_email;
    WiFiManagerParameter* custom_password;
    
    // Connection state
    uint8_t reconnectAttempts;
    bool wasConnected;
    uint32_t currentBackoffDelay;
    unsigned long lastReconnectAttempt;
    unsigned long lastConnectionCheckMillis;
    unsigned long lastFirmwareCheckMillis;
    unsigned long previousHeartbeatMillis;
    unsigned long lastHeapCheckMillis;
    unsigned long minHeapSeen;
    unsigned long lastConnectionCheck;
    unsigned long lastFirebaseCheck;
    
    // Credentials
    String apiKey;
    String email;
    String password;
    String otaPassword;
    
    // System state
    bool initialized;
    bool timeSet;  // Track if time has been synchronized
    
    MutexManager mutexManager;

    /**
     * Logs an error with appropriate category
     * Thread-safe: Yes
     * @param code Error code
     * @param message Error message
     * @param location Error location
     */
    void logError(ErrorManager::ErrorCode code, const String& message, const String& location) {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "NetworkManager::logError"
            );
            return;
        }

        // Store in ring buffer
        ErrorLog& log = errorLogs[currentErrorIndex];
        strncpy(log.message, message.c_str(), ERROR_BUFFER_SIZE - 1);
        log.message[ERROR_BUFFER_SIZE - 1] = '\0';
        strncpy(log.location, location.c_str(), ERROR_LOCATION_SIZE - 1);
        log.location[ERROR_LOCATION_SIZE - 1] = '\0';
        log.code = code;
        log.timestamp = millis();
        
        currentErrorIndex = (currentErrorIndex + 1) % ERROR_LOG_COUNT;
        
        // Forward to ErrorManager
        switch (code) {
            case ErrorManager::ErrorCode::MUTEX_TIMEOUT:
            case ErrorManager::ErrorCode::MUTEX_CREATE_FAILED:
                ErrorManager::mutexError(code, message, location);
                break;
            case ErrorManager::ErrorCode::NETWORK_CONNECTION_FAILED:
            case ErrorManager::ErrorCode::NETWORK_INVALID_CREDENTIALS:
                ErrorManager::networkError(code, message, location);
                break;
            case ErrorManager::ErrorCode::FIREBASE_CONNECTION_FAILED:
            case ErrorManager::ErrorCode::FIREBASE_INIT_FAILED:
            case ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED:
                ErrorManager::firebaseError(code, message, location);
                break;
            default:
                ErrorManager::systemError(code, message, location);
                break;
        }
    }
    
    /**
     * Loads Firebase credentials from Preferences
     * @return true if credentials were loaded successfully
     */
    bool loadFirebaseCredentials() {
        const int MAX_RETRIES = 3;
        int retryCount = 0;
        
        while (retryCount < MAX_RETRIES) {
            MutexManager::ScopedLock lock(mutexManager);
            if (lock.isLocked()) {
                Serial.println("[NetworkManager] Starting to load Firebase credentials");
                
                // Initialize Preferences
                Preferences preferences;
                Serial.println("[NetworkManager] Opening Preferences namespace");
                
                // First try to open in read-write mode to create namespace if it doesn't exist
                if (!preferences.begin(PREF_NAMESPACE, false)) {
                    Serial.println("[NetworkManager] ERROR: Failed to begin Preferences in read-write mode");
                    return false;
                }
                
                // Read credentials from Preferences
                Serial.println("[NetworkManager] Reading email...");
                email = preferences.getString(PREF_EMAIL_KEY, "");
                Serial.println("[NetworkManager] Reading password...");
                password = preferences.getString(PREF_PASSWORD_KEY, "");
                
                // Close Preferences
                preferences.end();
                
                // Validate credentials
                return !email.isEmpty() && !password.isEmpty();
            }
            
            retryCount++;
            delay(100);  // Small delay between retries
        }
        
        Serial.println("[NetworkManager] ERROR: Failed to acquire mutex after multiple retries");
        return false;
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
     * @param newEmail New email to save
     * @param newPassword New password to save
     * @return true if credentials were saved successfully
     */
    bool saveFirebaseCredentials(const String& newEmail, const String& newPassword) {
        const int MAX_RETRIES = 3;
        int retryCount = 0;
        
        while (retryCount < MAX_RETRIES) {
            MutexManager::ScopedLock lock(mutexManager);
            if (lock.isLocked()) {
                Serial.println("[NetworkManager] Starting to save Firebase credentials");
                
                // Validate input
                if (newEmail.isEmpty() || newPassword.isEmpty()) {
                    Serial.println("[NetworkManager] ERROR: Empty email or password provided");
                    logError(ErrorManager::ErrorCode::NETWORK_INVALID_CREDENTIALS, 
                            "Empty email or password provided", 
                            "NetworkManager::saveFirebaseCredentials");
                    return false;
                }
                
                // Initialize Preferences
                Preferences preferences;
                Serial.println("[NetworkManager] Opening Preferences namespace");
                
                // First try to open in read-write mode
                if (!preferences.begin(PREF_NAMESPACE, false)) {
                    Serial.println("[NetworkManager] ERROR: Failed to begin Preferences in read-write mode");
                    logError(ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                            "Failed to open Preferences namespace",
                            "NetworkManager::saveFirebaseCredentials");
                    return false;
                }
                
                // Save credentials with verification
                bool success = true;
                
                Serial.println("[NetworkManager] Saving email...");
                if (!preferences.putString(PREF_EMAIL_KEY, newEmail)) {
                    Serial.println("[NetworkManager] ERROR: Failed to save email");
                    logError(ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                            "Failed to save email to Preferences",
                            "NetworkManager::saveFirebaseCredentials");
                    success = false;
                } else {
                    // Verify email was saved
                    String savedEmail = preferences.getString(PREF_EMAIL_KEY, "");
                    if (savedEmail != newEmail) {
                        Serial.println("[NetworkManager] ERROR: Email verification failed");
                        logError(ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                                "Email verification failed after save",
                                "NetworkManager::saveFirebaseCredentials");
                        success = false;
                    }
                }
                
                Serial.println("[NetworkManager] Saving password...");
                if (!preferences.putString(PREF_PASSWORD_KEY, newPassword)) {
                    Serial.println("[NetworkManager] ERROR: Failed to save password");
                    logError(ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                            "Failed to save password to Preferences",
                            "NetworkManager::saveFirebaseCredentials");
                    success = false;
                } else {
                    // Verify password was saved
                    String savedPassword = preferences.getString(PREF_PASSWORD_KEY, "");
                    if (savedPassword != newPassword) {
                        Serial.println("[NetworkManager] ERROR: Password verification failed");
                        logError(ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                                "Password verification failed after save",
                                "NetworkManager::saveFirebaseCredentials");
                        success = false;
                    }
                }
                
                // Close Preferences
                preferences.end();
                
                if (success) {
                    email = newEmail;
                    password = newPassword;
                    Serial.println("[NetworkManager] Successfully saved and verified Firebase credentials");
                    return true;
                } else {
                    Serial.println("[NetworkManager] ERROR: Failed to save Firebase credentials");
                    logError(ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                            "Failed to save and verify Firebase credentials",
                            "NetworkManager::saveFirebaseCredentials");
                    return false;
                }
            }
            
            retryCount++;
            delay(100);  // Small delay between retries
        }
        
        Serial.println("[NetworkManager] ERROR: Failed to acquire mutex after multiple retries");
        logError(ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to acquire mutex for credential save",
                "NetworkManager::saveFirebaseCredentials");
        return false;
    }
    
    /**
     * Cleans up credential memory
     */
    void cleanupCredentials() {
        apiKey = "";
        email = "";
        password = "";
        otaPassword = "";
    }
    
    /**
     * Sets up WiFi Manager with custom parameters
     * Thread-safe: Yes
     * @return true if setup successful
     */
    bool setupWiFiManager() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return false;
        }

        bool success = true;
        
        // Set up callback for saving credentials
        wifiManager.setSaveConfigCallback([this]() {
            Serial.println("[NetworkManager] WiFi Manager save callback triggered");
            
            MutexManager::ScopedLock callbackLock(mutexManager);
            if (!callbackLock.isLocked()) {
                logError(ErrorManager::ErrorCode::MUTEX_TIMEOUT, "Failed to take mutex in callback", "NetworkManager::setupWiFiManager");
                return;
            }
            
            // Save new credentials
            if (custom_email != nullptr && custom_password != nullptr) {
                String newEmail = custom_email->getValue();
                String newPassword = custom_password->getValue();
                
                Serial.printf("[NetworkManager] Saving new credentials - Email: %s\n", newEmail.c_str());
                
                if (!saveFirebaseCredentials(newEmail, newPassword)) {
                    logError(ErrorManager::ErrorCode::SYSTEM_INIT_FAILED, "Failed to save Firebase credentials", "NetworkManager::setupWiFiManager");
                } else {
                    Serial.println("[NetworkManager] Successfully saved Firebase credentials");
                }
            } else {
                Serial.println("[NetworkManager] ERROR: Custom parameters are null in save callback");
                logError(ErrorManager::ErrorCode::SYSTEM_INIT_FAILED, "Custom parameters are null in save callback", "NetworkManager::setupWiFiManager");
            }
        });
        
        return success;
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
     * Connects to WiFi network
     * Thread-safe: Yes
     * @return true if connection successful
     */
    bool connectToWiFi() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return false;
        }

        bool success = true;
        
        // Set up WiFi Manager
        if (!setupWiFiManager()) {
            logError(ErrorManager::ErrorCode::SYSTEM_INIT_FAILED, "Failed to set up WiFi Manager", "NetworkManager::connectToWiFi");
            success = false;
        }
        
        if (success) {
            // Try to connect to saved network
            if (!wifiManager.autoConnect(AP_NAME, AP_PASSWORD)) {
                logError(ErrorManager::ErrorCode::NETWORK_CONNECTION_FAILED, "Failed to connect to WiFi", "NetworkManager::connectToWiFi");
                success = false;
            } else {
                Serial.printf("[NetworkManager] Connected to WiFi network: %s\n", WiFi.SSID().c_str());
            }
        }
        
        return success;
    }
    
    /**
     * Initializes time synchronization
     * @return true if initialization successful
     */
    bool initializeTime() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return false;
        }

        bool success = true;
        timeSet = false;
        
        // Configure NTP
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        
        // Wait for time to be set
        int retries = 0;
        time_t now;
        time(&now);
        
        while (now < 1000000000 && retries < 10) {  // Check if time is set (after year 2000)
            delay(1000);
            time(&now);
            retries++;
        }
        
        if (now < 1000000000) {
            Serial.println("[NetworkManager] Failed to set time");
            success = false;
        } else {
            timeSet = true;
            Serial.println("[NetworkManager] Time set successfully");
        }
        
        return success;
    }
    
    /**
     * Initializes Firebase connection
     * @return true if initialization successful
     */
    bool initializeFirebase() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return false;
        }

        bool success = true;
        
        // Load credentials from Preferences
        if (!loadFirebaseCredentials()) {
            Serial.println("[NetworkManager] Failed to load Firebase credentials");
            success = false;
        }
        
        if (success) {
            // Configure Firebase
            config.api_key = apiKey;
            config.database_url = SystemConfig::FIREBASE_PROJECT_ID;
            
            // Sign in with email/password
            auth.user.email = email.c_str();
            auth.user.password = password.c_str();
            
            // Initialize Firebase
            Firebase.begin(&config, &auth);
            Firebase.reconnectWiFi(true);
            
            // Set timeouts and buffer sizes
            fbdo.setResponseSize(SystemConfig::FIREBASE_PATH_BUFFER_SIZE);
            fbdo.setBSSLBufferSize(512, 2048);
            
            Serial.println("[NetworkManager] Firebase configuration completed");
        }
        
        return success;
    }
    
    /**
     * Checks if WiFi connection is stable
     * @return true if connection is stable
     */
    bool checkWiFiConnection() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "NetworkManager::checkWiFiConnection"
            );
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
        
        return isConnected;
    }
    
    /**
     * Attempts to reconnect to WiFi if disconnected
     * @return true if reconnection successful
     */
    bool attemptReconnect() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
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
        
        return success;
    }
    
    /**
     * Checks Firebase connection and reauthenticates if needed
     * @return true if Firebase is ready
     */
    bool checkFirebaseConnection() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "NetworkManager::checkFirebaseConnection"
            );
            return false;
        }
        
        bool isReady = Firebase.ready();
        
        if (!isReady) {
            ErrorManager::firebaseError(
                ErrorManager::ErrorCode::FIREBASE_CONNECTION_FAILED,
                "Firebase connection lost or token expired",
                "NetworkManager::checkFirebaseConnection"
            );
            
            // Attempt to reinitialize Firebase
            if (!initializeFirebase()) {
                ErrorManager::firebaseError(
                    ErrorManager::ErrorCode::FIREBASE_INIT_FAILED,
                    "Failed to reinitialize Firebase",
                    "NetworkManager::checkFirebaseConnection"
                );
                return false;
            }
        }
        
        return isReady;
    }

public:
    /**
     * Constructor
     * @param fbdo Reference to FirebaseData
     * @param fa Reference to FirebaseAuth
     * @param fc Reference to FirebaseConfig
     * @param lm Reference to LightManager
     * @param wm Reference to WiFiManager
     */
    NetworkManager(FirebaseData& fbdo, FirebaseAuth& fa, FirebaseConfig& fc,
                  LightManager& lm, WiFiManager& wm)
        : fbdo(fbdo), auth(fa), config(fc), lightManager(lm), wifiManager(wm),
          initialized(false), reconnectAttempts(0), wasConnected(false), currentBackoffDelay(RECONNECT_DELAY),
          lastReconnectAttempt(0), lastConnectionCheckMillis(0),
          lastFirmwareCheckMillis(0), previousHeartbeatMillis(0),
          lastHeapCheckMillis(0), minHeapSeen(UINT32_MAX),
          lastConnectionCheck(0), lastFirebaseCheck(0),
          custom_email(nullptr), custom_password(nullptr),
          timeSet(false) {
        
        Serial.println("[NetworkManager] Starting constructor");
        
        // Pre-allocate space for credentials
        apiKey.reserve(CREDENTIAL_MAX_LENGTH);
        email.reserve(CREDENTIAL_MAX_LENGTH);
        password.reserve(CREDENTIAL_MAX_LENGTH);
        
        // Initialize WiFiManager with safe defaults
        wifiManager.setDebugOutput(false);
        wifiManager.setMinimumSignalQuality(MIN_SIGNAL_QUALITY);
        wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
        
        // Initialize WiFi parameters
        custom_email = new WiFiManagerParameter("email", "Firebase Email", "", 64);
        custom_password = new WiFiManagerParameter("password", "Firebase Password", "", 64, "type=\"password\"");
        
        Serial.println("[NetworkManager] Constructor completed successfully");
    }
    
    /**
     * Destructor
     * Cleans up resources
     */
    ~NetworkManager() {
        Serial.println("[NetworkManager] Starting destructor");
        
        // Clean up WiFi parameters
        Serial.println("[NetworkManager] Cleaning up WiFi parameters");
        if (custom_email != nullptr) {
            delete custom_email;
            custom_email = nullptr;
        }
        if (custom_password != nullptr) {
            delete custom_password;
            custom_password = nullptr;
        }
        
        // Clean up credentials
        Serial.println("[NetworkManager] Cleaning up credentials");
        cleanupCredentials();
        
        Serial.println("[NetworkManager] Destructor completed");
    }
    
    /**
     * Initializes network components
     * Sets up WiFi, Firebase, and time synchronization
     * @return true if initialization successful
     */
    bool begin() {
        if (initialized) {
            return true;
        }

        Serial.println("[NetworkManager] Starting initialization");
        
        // Add custom parameters to WiFi Manager
        wifiManager.addParameter(custom_email);
        wifiManager.addParameter(custom_password);
        
        // Configure WiFi Manager
        wifiManager.setMinimumSignalQuality(MIN_SIGNAL_QUALITY);
        wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
        
        // Set up callback for saving credentials
        wifiManager.setSaveConfigCallback([this]() {
            Serial.println("[NetworkManager] WiFi Manager save callback triggered");
            
            MutexManager::ScopedLock callbackLock(mutexManager);
            if (!callbackLock.isLocked()) {
                ErrorManager::mutexError(
                    ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                    "Failed to take mutex in callback",
                    "NetworkManager::begin"
                );
                return;
            }
            
            // Save new credentials
            if (custom_email != nullptr && custom_password != nullptr) {
                String newEmail = custom_email->getValue();
                String newPassword = custom_password->getValue();
                
                Serial.printf("[NetworkManager] Saving new credentials - Email: %s\n", newEmail.c_str());
                
                if (!saveFirebaseCredentials(newEmail, newPassword)) {
                    ErrorManager::systemError(
                        ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                        "Failed to save Firebase credentials",
                        "NetworkManager::begin"
                    );
                } else {
                    Serial.println("[NetworkManager] Successfully saved Firebase credentials");
                }
            } else {
                Serial.println("[NetworkManager] ERROR: Custom parameters are null in save callback");
                ErrorManager::systemError(
                    ErrorManager::ErrorCode::SYSTEM_INIT_FAILED,
                    "Custom parameters are null in save callback",
                    "NetworkManager::begin"
                );
            }
        });
        
        // Initialize WiFi Manager
        if (!wifiManager.autoConnect(AP_NAME, AP_PASSWORD)) {
            logError(ErrorManager::ErrorCode::NETWORK_INIT_FAILED,
                    "Failed to connect to WiFi",
                    "NetworkManager::begin");
            return false;
        }
        
        initialized = true;
        return true;
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
        
        // Check WiFi connection every CONNECTION_CHECK_INTERVAL
        if (currentMillis - lastConnectionCheck >= CONNECTION_CHECK_INTERVAL) {
            if (!checkWiFiConnection()) {
                attemptReconnect();
            }
            lastConnectionCheck = currentMillis;
        }
        
        // Check Firebase connection every FIREBASE_CHECK_INTERVAL
        if (currentMillis - lastFirebaseCheck >= SystemConfig::FIREBASE_CHECK_INTERVAL) {
            checkFirebaseConnection();
            lastFirebaseCheck = currentMillis;
        }
    }
    
    /**
     * Checks if the network is connected
     * @return true if connected
     */
    bool isConnected() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return false;
        }
        return WiFi.status() == WL_CONNECTED;
    }
    
    /**
     * Gets the current connection state
     * @return WiFi connection state
     */
    wl_status_t getConnectionState() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return WL_DISCONNECTED;
        }
        return WiFi.status();
    }
    
    /**
     * Gets the current RSSI
     * @return RSSI value
     */
    int32_t getRSSI() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return 0;
        }
        return WiFi.RSSI();
    }
    
    /**
     * Gets the current IP address
     * @return IP address as string
     */
    String getIPAddress() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return "";
        }
        return WiFi.localIP().toString();
    }
    
    /**
     * Gets the MAC address
     * @return MAC address as string
     */
    String getMACAddress() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return "";
        }
        return WiFi.macAddress();
    }
    
    /**
     * Sets the OTA password
     * @param password New OTA password
     */
    void setOTAPassword(const String& password) {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return;
        }
        otaPassword = password;
    }
    
    /**
     * Handles OTA update
     * @param password OTA password
     * @return true if update was successful
     */
    bool handleOTA(String password) {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (password != otaPassword) {
            return false;
        }
        
        ArduinoOTA.begin();
        return true;
    }
    
    /**
     * Resets the WiFi Manager
     * @return true if reset was successful
     */
    bool resetWiFiManager() {
        MutexManager::ScopedLock lock(mutexManager);
        if (!lock.isLocked()) {
            return false;
        }
        
        wifiManager.resetSettings();
        return true;
    }
};

#endif // NETWORK_MANAGER_H 