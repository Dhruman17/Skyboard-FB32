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
        if (isOpen) {
            preferences.end();
        }
        
        if (!preferences.begin(ns, readOnly)) {
            ::ErrorManager::systemError(
                ::ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to open preferences namespace",
                "PreferencesManager::openNamespace"
            );
            return false;
        }
        
        isOpen = true;
        return true;
    }

public:
    /**
     * Constructor
     */
    PreferencesManager() : isOpen(false) {
        // Don't initialize preferences here, wait for begin()
    }
    
    /**
     * Initializes the preferences manager
     * @return true if initialization successful
     */
    bool begin() {
        if (isOpen) {
            preferences.end();
        }
        return true;
    }
    
    /**
     * Destructor
     * Ensures Preferences is properly closed
     */
    ~PreferencesManager() {
        if (isOpen) {
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