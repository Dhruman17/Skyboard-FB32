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
    
    // Mutex for thread safety
    SemaphoreHandle_t mutex;
    
    // System state
    bool initialized;
    bool timeSet;  // Track if time has been synchronized
    
    /**
     * Logs an error to the ring buffer and ErrorManager
     * @param code Error code
     * @param message Error message
     * @param location Error location
     */
    void logError(ErrorManager::ErrorCode code, const char* message, const char* location) {
        // Store in ring buffer
        ErrorLog& log = errorLogs[currentErrorIndex];
        strncpy(log.message, message, ERROR_BUFFER_SIZE - 1);
        log.message[ERROR_BUFFER_SIZE - 1] = '\0';
        strncpy(log.location, location, ERROR_LOCATION_SIZE - 1);
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
     * Safely takes the mutex with timeout
     * Thread-safe: Yes
     * @return true if mutex was taken successfully
     */
    bool takeMutex() {
        if (mutex == NULL) {
            Serial.println("[NetworkManager] ERROR: Mutex is NULL");
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_CREATE_FAILED,
                "Mutex is NULL",
                "NetworkManager::takeMutex"
            );
            return false;
        }
        
        // Try to take mutex with timeout
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
            Serial.println("[NetworkManager] ERROR: Failed to take mutex");
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "NetworkManager::takeMutex"
            );
            return false;
        }
        return true;
    }
    
    /**
     * Safely gives the mutex
     * Thread-safe: Yes
     */
    void giveMutex() {
        if (mutex == NULL) {
            Serial.println("[NetworkManager] ERROR: Attempting to give NULL mutex");
            return;
        }
        xSemaphoreGive(mutex);
    }
    
    /**
     * Loads Firebase credentials from Preferences
     * @return true if credentials were loaded successfully
     */
    bool loadFirebaseCredentials() {
        // Initialize Preferences
        Preferences preferences;
        preferences.begin(PREF_NAMESPACE, true);  // Read-only mode
        
        // Read credentials from Preferences
        email = preferences.getString(PREF_EMAIL_KEY, "");
        password = preferences.getString(PREF_PASSWORD_KEY, "");
        
        // Close Preferences
        preferences.end();
        
        // Validate credentials
        bool valid = !email.isEmpty() && !password.isEmpty();
        
        if (!valid) {
            snprintf(errorMsgBuffer, ERROR_BUFFER_SIZE, "Firebase credentials not found in Preferences");
            snprintf(errorLocBuffer, ERROR_LOCATION_SIZE, "NetworkManager::loadFirebaseCredentials");
            logError(ErrorManager::ErrorCode::NETWORK_INVALID_CREDENTIALS, errorMsgBuffer, errorLocBuffer);
        }
        
        return valid;
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
        Serial.println("[NetworkManager] Starting to save Firebase credentials");
        
        // Initialize Preferences
        Preferences preferences;
        if (!preferences.begin(PREF_NAMESPACE, false)) {  // Read-write mode
            Serial.println("[NetworkManager] ERROR: Failed to begin Preferences");
            return false;
        }
        
        // Save credentials
        bool success = true;
        
        if (!preferences.putString(PREF_EMAIL_KEY, newEmail)) {
            Serial.println("[NetworkManager] ERROR: Failed to save email");
            success = false;
        }
        
        if (!preferences.putString(PREF_PASSWORD_KEY, newPassword)) {
            Serial.println("[NetworkManager] ERROR: Failed to save password");
            success = false;
        }
        
        // Close Preferences
        preferences.end();
        
        if (success) {
            email = newEmail;
            password = newPassword;
            Serial.println("[NetworkManager] Successfully saved Firebase credentials to Preferences");
        } else {
            Serial.println("[NetworkManager] ERROR: Failed to save Firebase credentials");
            snprintf(errorMsgBuffer, ERROR_BUFFER_SIZE, "Failed to save Firebase credentials");
            snprintf(errorLocBuffer, ERROR_LOCATION_SIZE, "NetworkManager::saveFirebaseCredentials");
            logError(ErrorManager::ErrorCode::NETWORK_INVALID_CREDENTIALS, errorMsgBuffer, errorLocBuffer);
        }
        
        return success;
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
        bool success = true;
        
        // Load saved credentials
        if (!loadFirebaseCredentials()) {
            logError(ErrorManager::ErrorCode::SYSTEM_INIT_FAILED, "Failed to load Firebase credentials", "NetworkManager::setupWiFiManager");
            success = false;
        }
        
        // Set up callback for saving credentials
        wifiManager.setSaveConfigCallback([this]() {
            Serial.println("[NetworkManager] WiFi Manager save callback triggered");
            
            if (!takeMutex()) {
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
            
            giveMutex();
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
        if (!takeMutex()) {
            Serial.println("[NetworkManager] ERROR: Failed to take mutex in initializeTime");
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
        
        giveMutex();
        return success;
    }
    
    /**
     * Initializes Firebase connection
     * @return true if initialization successful
     */
    bool initializeFirebase() {
        if (!takeMutex()) {
            Serial.println("[NetworkManager] ERROR: Failed to take mutex in initializeFirebase");
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
    
    /**
     * Checks Firebase connection and reauthenticates if needed
     * @return true if Firebase is ready
     */
    bool checkFirebaseConnection() {
        if (!takeMutex()) {
            snprintf(errorMsgBuffer, ERROR_BUFFER_SIZE, "Failed to check Firebase connection");
            snprintf(errorLocBuffer, ERROR_LOCATION_SIZE, "NetworkManager::checkFirebaseConnection");
            logError(ErrorManager::ErrorCode::MUTEX_TIMEOUT, errorMsgBuffer, errorLocBuffer);
            return false;
        }
        
        bool isReady = Firebase.ready();
        
        if (!isReady) {
            logError(ErrorManager::ErrorCode::FIREBASE_CONNECTION_FAILED, "Firebase connection lost or token expired", "NetworkManager::checkFirebaseConnection");
            
            // Attempt to reinitialize Firebase
            if (!initializeFirebase()) {
                logError(ErrorManager::ErrorCode::FIREBASE_INIT_FAILED, "Failed to reinitialize Firebase", "NetworkManager::checkFirebaseConnection");
                giveMutex();
                return false;
            }
        }
        
        giveMutex();
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
          initialized(false), reconnectAttempts(0), wasConnected(false), currentBackoffDelay(0),
          lastReconnectAttempt(0), lastConnectionCheckMillis(0),
          lastFirmwareCheckMillis(0), previousHeartbeatMillis(0),
          lastHeapCheckMillis(0), minHeapSeen(0),
          lastConnectionCheck(0), lastFirebaseCheck(0),
          custom_email(nullptr), custom_password(nullptr),
          timeSet(false) {
        
        Serial.println("[NetworkManager] Starting constructor");
        
        // Create mutex first
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            Serial.println("[NetworkManager] CRITICAL ERROR: Failed to create mutex");
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_CREATE_FAILED,
                "Failed to create NetworkManager mutex",
                "NetworkManager::NetworkManager"
            );
            // Don't proceed with initialization if mutex creation failed
            return;
        }
        Serial.println("[NetworkManager] Successfully created mutex");
        
        // Pre-allocate space for credentials without taking mutex
        Serial.println("[NetworkManager] Pre-allocating credential strings");
        apiKey.reserve(CREDENTIAL_MAX_LENGTH);
        email.reserve(CREDENTIAL_MAX_LENGTH);
        password.reserve(CREDENTIAL_MAX_LENGTH);
        
        // Initialize WiFiManager with safe defaults without taking mutex
        Serial.println("[NetworkManager] Initializing WiFiManager defaults");
        wifiManager.setDebugOutput(false);
        wifiManager.setMinimumSignalQuality(MIN_SIGNAL_QUALITY);
        wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
        
        Serial.println("[NetworkManager] Constructor completed successfully");
    }
    
    /**
     * Destructor
     * Cleans up resources
     */
    ~NetworkManager() {
        Serial.println("[NetworkManager] Starting destructor");
        if (mutex != NULL) {
            Serial.println("[NetworkManager] Deleting mutex");
            vSemaphoreDelete(mutex);
        }
        
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
        Serial.println("[NetworkManager] Starting begin()");
        
        if (!takeMutex()) {
            Serial.println("[NetworkManager] Failed to take mutex in begin");
            return false;
        }
        
        bool success = true;
        
        // Load Firebase credentials first
        if (!loadFirebaseCredentials()) {
            Serial.println("[NetworkManager] No Firebase credentials found. Will use default configuration.");
        }
        
        // Clean up old parameters
        if (custom_email != nullptr) {
            delete custom_email;
            custom_email = nullptr;
        }
        if (custom_password != nullptr) {
            delete custom_password;
            custom_password = nullptr;
        }
        
        // Create new parameters with current values
        custom_email = new WiFiManagerParameter("email", "Firebase Email", email.c_str(), CREDENTIAL_MAX_LENGTH);
        custom_password = new WiFiManagerParameter("password", "Firebase Password", password.c_str(), CREDENTIAL_MAX_LENGTH);
        
        // Add parameters to WiFiManager
        wifiManager.addParameter(custom_email);
        wifiManager.addParameter(custom_password);
        
        // Release mutex before WiFi connection attempt
        giveMutex();
        
        // Try to connect to WiFi with timeout and yield
        Serial.println("[NetworkManager] Attempting to connect to WiFi...");
        unsigned long startTime = millis();
        unsigned long lastYieldTime = startTime;
        
        // Try to connect to WiFi without holding the mutex
        while (!wifiManager.autoConnect(AP_NAME, AP_PASSWORD)) {
            if (millis() - startTime > WIFI_TIMEOUT) {
                Serial.println("[NetworkManager] WiFi connection timeout");
                success = false;
                break;
            }
            
            // Yield and feed watchdog every 100ms
            if (millis() - lastYieldTime >= 100) {
                yield();
                delay(1);  // Small delay to ensure watchdog is fed
                lastYieldTime = millis();
            }
            
            delay(10);  // Short delay between connection attempts
        }
        
        // Re-acquire mutex for remaining initialization
        if (!takeMutex()) {
            Serial.println("[NetworkManager] Failed to re-acquire mutex after WiFi connection");
            return false;
        }
        
        if (success) {
            // Initialize time with timeout
            Serial.println("[NetworkManager] Initializing time...");
            giveMutex();  // Release mutex before time initialization
            if (!initializeTime()) {
                Serial.println("[NetworkManager] Time initialization failed");
                success = false;
            } else {
                Serial.println("[NetworkManager] Time initialized successfully");
            }
            
            // Re-acquire mutex for Firebase initialization
            if (!takeMutex()) {
                Serial.println("[NetworkManager] Failed to re-acquire mutex after time initialization");
                return false;
            }
            
            // Initialize Firebase with timeout
            if (success) {
                Serial.println("[NetworkManager] Initializing Firebase...");
                giveMutex();  // Release mutex before Firebase initialization
                if (!initializeFirebase()) {
                    Serial.println("[NetworkManager] Firebase initialization failed");
                    success = false;
                } else {
                    Serial.println("[NetworkManager] Firebase initialized successfully");
                }
                
                // Re-acquire mutex for final state update
                if (!takeMutex()) {
                    Serial.println("[NetworkManager] Failed to re-acquire mutex after Firebase initialization");
                    return false;
                }
            }
        }
        
        if (success) {
            initialized = true;
            wasConnected = true;
            Serial.println("[NetworkManager] Network initialization completed successfully");
        } else {
            Serial.println("[NetworkManager] Network initialization failed");
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
     * Checks if WiFi is connected
     * Thread-safe: Yes
     * @return true if connected
     */
    bool isConnected() {
        if (!takeMutex()) {
            logError(ErrorManager::ErrorCode::MUTEX_TIMEOUT, "Failed to check WiFi connection", "NetworkManager::isConnected");
            return false;
        }
        
        bool connected = WiFi.status() == WL_CONNECTED;
        giveMutex();
        return connected;
    }
    
    /**
     * Gets the current WiFi signal strength
     * Thread-safe: Yes
     * @return Signal strength in dBm
     */
    int getSignalStrength() {
        if (!takeMutex()) {
            logError(ErrorManager::ErrorCode::MUTEX_TIMEOUT, "Failed to get signal strength", "NetworkManager::getSignalStrength");
            return 0;
        }
        
        int strength = WiFi.RSSI();
        giveMutex();
        return strength;
    }

    /**
     * Sets the OTA password
     * Thread-safe: Yes
     * @param password New OTA password
     */
    void setOTAPassword(const String& password) {
        if (!takeMutex()) {
            logError(ErrorManager::ErrorCode::MUTEX_TIMEOUT, "Failed to set OTA password", "NetworkManager::setOTAPassword");
            return;
        }
        
        if (password.length() >= 6 && password.length() <= CREDENTIAL_MAX_LENGTH) {
            otaPassword = password;
        } else {
            logError(ErrorManager::ErrorCode::SYSTEM_INVALID_STATE, "Invalid OTA password length", "NetworkManager::setOTAPassword");
        }
        
        giveMutex();
    }
    
    /**
     * Handles OTA updates for the system
     * Thread-safe: Yes
     * @param systemName Name of the system
     * @return true if OTA handling successful
     */
    bool handleOTA(String systemName) {
        if (!takeMutex()) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to handle OTA",
                "NetworkManager::handleOTA"
            );
            return false;
        }
        
        bool success = true;
        
        // Ensure systemName doesn't exceed maximum length
        if (systemName.length() > SYSTEM_NAME_MAX_LENGTH) {
            systemName = systemName.substring(0, SYSTEM_NAME_MAX_LENGTH);
        }
        
        // Configure ArduinoOTA
        ArduinoOTA.setHostname(systemName.c_str());
        ArduinoOTA.setPassword(otaPassword.c_str());
        
        // Start OTA server
        ArduinoOTA.begin();
        Serial.println("OTA server started");
        
        giveMutex();
        return success;
    }

    /**
     * Resets WiFi Manager settings if enabled in config
     * Thread-safe: Yes
     * @return true if reset was successful
     */
    bool resetWiFiManager() {
        if (!SystemConfig::WIFI_MANAGER_RESET_ENABLED) {
            Serial.println("[NetworkManager] WiFi Manager reset disabled in config");
            return false;
        }

        if (!takeMutex()) {
            Serial.println("[NetworkManager] Failed to take mutex for WiFi Manager reset");
            return false;
        }

        Serial.println("[NetworkManager] Resetting WiFi Manager settings...");
        Serial.flush();  // Ensure message is sent
        
        wifiManager.resetSettings();
        
        Serial.println("[NetworkManager] WiFi Manager settings reset successfully");
        Serial.flush();  // Ensure message is sent
        
        delay(1000);  // Give time for serial output to be sent
        
        giveMutex();
        return true;
    }
};

#endif // NETWORK_MANAGER_H 