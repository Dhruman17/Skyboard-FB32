#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include <Preferences.h>

/**
 * PreferencesManager Class
 * 
 * Manages non-volatile storage operations:
 * 1. Firebase Credentials:
 *    - Email and password storage
 *    - Secure credential handling
 * 
 * 2. System Configuration:
 *    - Light timing settings
 *    - Unit configurations
 *    - Network settings
 * 
 * 3. Error Handling:
 *    - Storage validation
 *    - Error recovery
 */
class PreferencesManager {
private:
    Preferences preferences;
    bool isOpen;
    
    // Namespace constants
    static constexpr const char* FIREBASE_NS = "firebase";
    static constexpr const char* SYSTEM_NS = "system";
    static constexpr const char* UNIT_NS = "units";
    
    /**
     * Opens a namespace with error handling
     * @param ns Namespace to open
     * @param readOnly Whether to open in read-only mode
     * @return true if namespace was opened successfully
     */
    bool openNamespace(const char* ns, bool readOnly = true) {
        Serial.printf("[PreferencesManager] Attempting to open namespace: %s (readOnly: %d)\n", ns, readOnly);
        
        if (isOpen) {
            Serial.println("[PreferencesManager] Closing currently open namespace");
            preferences.end();
        }
        
        if (!preferences.begin(ns, readOnly)) {
            Serial.printf("[PreferencesManager] ERROR: Failed to open namespace: %s\n", ns);
            ::ErrorManager::systemError(
                ::ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to open preferences namespace",
                "PreferencesManager::openNamespace"
            );
            return false;
        }
        
        Serial.printf("[PreferencesManager] Successfully opened namespace: %s\n", ns);
        isOpen = true;
        return true;
    }

public:
    /**
     * Constructor
     */
    PreferencesManager() : isOpen(false) {
        Serial.println("[PreferencesManager] Constructor called");
    }
    
    /**
     * Initializes the preferences manager
     * @return true if initialization successful
     */
    bool begin() {
        Serial.println("[PreferencesManager] Starting initialization...");
        
        if (isOpen) {
            Serial.println("[PreferencesManager] Closing currently open namespace");
            preferences.end();
        }
        
        // Initialize preferences with a default namespace
        Serial.println("[PreferencesManager] Attempting to initialize with default namespace 'system'");
        if (!preferences.begin("system", false)) {
            Serial.println("[PreferencesManager] ERROR: Failed to initialize with default namespace");
            ::ErrorManager::systemError(
                ::ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to initialize preferences",
                "PreferencesManager::begin"
            );
            return false;
        }
        Serial.println("[PreferencesManager] Successfully initialized with default namespace");
        preferences.end();
        
        // Create all required namespaces if they don't exist
        Serial.printf("[PreferencesManager] Creating namespace: %s\n", FIREBASE_NS);
        if (!preferences.begin(FIREBASE_NS, false)) {
            Serial.printf("[PreferencesManager] ERROR: Failed to create namespace: %s\n", FIREBASE_NS);
            ::ErrorManager::systemError(
                ::ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to create Firebase namespace",
                "PreferencesManager::begin"
            );
            return false;
        }
        Serial.printf("[PreferencesManager] Successfully created namespace: %s\n", FIREBASE_NS);
        preferences.end();
        
        Serial.printf("[PreferencesManager] Creating namespace: %s\n", SYSTEM_NS);
        if (!preferences.begin(SYSTEM_NS, false)) {
            Serial.printf("[PreferencesManager] ERROR: Failed to create namespace: %s\n", SYSTEM_NS);
            ::ErrorManager::systemError(
                ::ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to create System namespace",
                "PreferencesManager::begin"
            );
            return false;
        }
        Serial.printf("[PreferencesManager] Successfully created namespace: %s\n", SYSTEM_NS);
        preferences.end();
        
        Serial.printf("[PreferencesManager] Creating namespace: %s\n", UNIT_NS);
        if (!preferences.begin(UNIT_NS, false)) {
            Serial.printf("[PreferencesManager] ERROR: Failed to create namespace: %s\n", UNIT_NS);
            ::ErrorManager::systemError(
                ::ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to create Unit namespace",
                "PreferencesManager::begin"
            );
            return false;
        }
        Serial.printf("[PreferencesManager] Successfully created namespace: %s\n", UNIT_NS);
        preferences.end();
        
        // Initialize with system namespace
        Serial.printf("[PreferencesManager] Opening final namespace: %s\n", SYSTEM_NS);
        if (!preferences.begin(SYSTEM_NS, false)) {
            Serial.printf("[PreferencesManager] ERROR: Failed to open final namespace: %s\n", SYSTEM_NS);
            ::ErrorManager::systemError(
                ::ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to open System namespace",
                "PreferencesManager::begin"
            );
            return false;
        }
        
        // Verify we can read/write to the namespace
        if (!preferences.putString("test", "test") || preferences.getString("test", "") != "test") {
            Serial.println("[PreferencesManager] ERROR: Failed to verify namespace access");
            ::ErrorManager::systemError(
                ::ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to verify namespace access",
                "PreferencesManager::begin"
            );
            return false;
        }
        preferences.remove("test");
        
        Serial.println("[PreferencesManager] Successfully completed initialization");
        isOpen = true;
        return true;
    }
    
    /**
     * Destructor
     * Ensures Preferences is properly closed
     */
    ~PreferencesManager() {
        Serial.println("[PreferencesManager] Destructor called");
        if (isOpen) {
            Serial.println("[PreferencesManager] Closing open namespace");
            preferences.end();
        }
    }
    
    /**
     * Loads Firebase credentials
     * @param email Output parameter for email
     * @param password Output parameter for password
     * @return true if credentials were loaded successfully
     */
    bool loadFirebaseCredentials(String& email, String& password) {
        if (!openNamespace(FIREBASE_NS, true)) {
            return false;
        }
        
        email = preferences.getString("email", "");
        password = preferences.getString("password", "");
        
        preferences.end();
        isOpen = false;
        
        return !email.isEmpty() && !password.isEmpty();
    }
    
    /**
     * Saves Firebase credentials
     * @param email Email to save
     * @param password Password to save
     * @return true if credentials were saved successfully
     */
    bool saveFirebaseCredentials(const String& email, const String& password) {
        if (!openNamespace(FIREBASE_NS, false)) {
            return false;
        }
        
        bool success = preferences.putString("email", email) &&
                      preferences.putString("password", password);
        
        preferences.end();
        isOpen = false;
        
        return success;
    }
    
    /**
     * Loads system configuration
     * @return true if configuration was loaded successfully
     */
    bool loadSystemConfig() {
        if (!openNamespace(SYSTEM_NS, true)) {
            return false;
        }
        
        // Load system configuration here
        // TODO: Implement system configuration loading
        
        preferences.end();
        isOpen = false;
        
        return true;
    }
    
    /**
     * Saves system configuration
     * @return true if configuration was saved successfully
     */
    bool saveSystemConfig() {
        if (!openNamespace(SYSTEM_NS, false)) {
            return false;
        }
        
        // Save system configuration here
        // TODO: Implement system configuration saving
        
        preferences.end();
        isOpen = false;
        
        return true;
    }
    
    /**
     * Loads system settings from preferences
     * @param systemState Reference to the system state to load into
     * @return true if settings were loaded successfully
     */
    bool loadSettings(SystemState& systemState) {
        if (!openNamespace(SYSTEM_NS, true)) {
            return false;
        }
        
        // Load system settings
        systemState.timeValid = preferences.getBool("timeValid", false);
        systemState.lastSyncTime = preferences.getULong("lastSyncTime", 0);
        systemState.heapWarning = preferences.getBool("heapWarning", false);
        systemState.minHeapSeen = preferences.getULong("minHeapSeen", ESP.getFreeHeap());
        
        preferences.end();
        isOpen = false;
        
        return true;
    }
    
    /**
     * Saves system settings to preferences
     * @param systemState Reference to the system state to save
     * @return true if settings were saved successfully
     */
    bool saveSettings(const SystemState& systemState) {
        if (!openNamespace(SYSTEM_NS, false)) {
            return false;
        }
        
        // Save system settings
        bool success = preferences.putBool("timeValid", systemState.timeValid) &&
                      preferences.putULong("lastSyncTime", systemState.lastSyncTime) &&
                      preferences.putBool("heapWarning", systemState.heapWarning) &&
                      preferences.putULong("minHeapSeen", systemState.minHeapSeen);
        
        preferences.end();
        isOpen = false;
        
        return success;
    }
};

#endif // PREFERENCES_MANAGER_H 