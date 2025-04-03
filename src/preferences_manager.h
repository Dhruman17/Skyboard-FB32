#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include "config.h"
#include "error_manager.h"
#include "mutex_manager.h"
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
class PreferencesManager : public MutexManager {
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
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        Serial.printf("[PreferencesManager] Attempting to open namespace: %s (readOnly: %d)\n", ns, readOnly);
        
        if (isOpen) {
            Serial.println("[PreferencesManager] Closing currently open namespace");
            preferences.end();
        }
        
        if (!preferences.begin(ns, readOnly)) {
            Serial.printf("[PreferencesManager] ERROR: Failed to open namespace: %s\n", ns);
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to open preferences namespace",
                "PreferencesManager::openNamespace"
            );
            return false;
        }
        
        isOpen = true;
        Serial.printf("[PreferencesManager] Successfully opened namespace: %s\n", ns);
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
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        Serial.println("[PreferencesManager] Starting initialization...");
        
        if (isOpen) {
            Serial.println("[PreferencesManager] Closing currently open namespace");
            preferences.end();
        }
        
        // Initialize preferences with a default namespace
        Serial.println("[PreferencesManager] Attempting to initialize with default namespace 'system'");
        if (!preferences.begin("system", false)) {
            Serial.println("[PreferencesManager] ERROR: Failed to initialize with default namespace");
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
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
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
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
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to create system namespace",
                "PreferencesManager::begin"
            );
            return false;
        }
        Serial.printf("[PreferencesManager] Successfully created namespace: %s\n", SYSTEM_NS);
        preferences.end();
        
        Serial.printf("[PreferencesManager] Creating namespace: %s\n", UNIT_NS);
        if (!preferences.begin(UNIT_NS, false)) {
            Serial.printf("[PreferencesManager] ERROR: Failed to create namespace: %s\n", UNIT_NS);
            ErrorManager::systemError(
                ErrorManager::ErrorCode::SYSTEM_CONFIG_FAILED,
                "Failed to create units namespace",
                "PreferencesManager::begin"
            );
            return false;
        }
        Serial.printf("[PreferencesManager] Successfully created namespace: %s\n", UNIT_NS);
        preferences.end();
        
        Serial.println("[PreferencesManager] Initialization completed successfully");
        return true;
    }
    
    /**
     * Gets a string value from preferences
     * @param ns Namespace
     * @param key Key to get
     * @param defaultValue Default value if key not found
     * @return Value or default if not found
     */
    String getString(const char* ns, const char* key, const char* defaultValue = "") {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return defaultValue;
        }
        
        if (!openNamespace(ns)) {
            return defaultValue;
        }
        
        String value = preferences.getString(key, defaultValue);
        preferences.end();
        isOpen = false;
        return value;
    }
    
    /**
     * Gets a boolean value from preferences
     * @param ns Namespace
     * @param key Key to get
     * @param defaultValue Default value if key not found
     * @return Value or default if not found
     */
    bool getBool(const char* ns, const char* key, bool defaultValue = false) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return defaultValue;
        }
        
        if (!openNamespace(ns)) {
            return defaultValue;
        }
        
        bool value = preferences.getBool(key, defaultValue);
        preferences.end();
        isOpen = false;
        return value;
    }
    
    /**
     * Gets an integer value from preferences
     * @param ns Namespace
     * @param key Key to get
     * @param defaultValue Default value if key not found
     * @return Value or default if not found
     */
    int getInt(const char* ns, const char* key, int defaultValue = 0) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return defaultValue;
        }
        
        if (!openNamespace(ns)) {
            return defaultValue;
        }
        
        int value = preferences.getInt(key, defaultValue);
        preferences.end();
        isOpen = false;
        return value;
    }
    
    /**
     * Gets a float value from preferences
     * @param ns Namespace
     * @param key Key to get
     * @param defaultValue Default value if key not found
     * @return Value or default if not found
     */
    float getFloat(const char* ns, const char* key, float defaultValue = 0.0f) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return defaultValue;
        }
        
        if (!openNamespace(ns)) {
            return defaultValue;
        }
        
        float value = preferences.getFloat(key, defaultValue);
        preferences.end();
        isOpen = false;
        return value;
    }
    
    /**
     * Sets a string value in preferences
     * @param ns Namespace
     * @param key Key to set
     * @param value Value to set
     * @return true if successful
     */
    bool putString(const char* ns, const char* key, const String& value) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!openNamespace(ns, false)) {
            return false;
        }
        
        bool success = preferences.putString(key, value);
        preferences.end();
        isOpen = false;
        return success;
    }
    
    /**
     * Sets a boolean value in preferences
     * @param ns Namespace
     * @param key Key to set
     * @param value Value to set
     * @return true if successful
     */
    bool putBool(const char* ns, const char* key, bool value) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!openNamespace(ns, false)) {
            return false;
        }
        
        bool success = preferences.putBool(key, value);
        preferences.end();
        isOpen = false;
        return success;
    }
    
    /**
     * Sets an integer value in preferences
     * @param ns Namespace
     * @param key Key to set
     * @param value Value to set
     * @return true if successful
     */
    bool putInt(const char* ns, const char* key, int value) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!openNamespace(ns, false)) {
            return false;
        }
        
        bool success = preferences.putInt(key, value);
        preferences.end();
        isOpen = false;
        return success;
    }
    
    /**
     * Sets a float value in preferences
     * @param ns Namespace
     * @param key Key to set
     * @param value Value to set
     * @return true if successful
     */
    bool putFloat(const char* ns, const char* key, float value) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!openNamespace(ns, false)) {
            return false;
        }
        
        bool success = preferences.putFloat(key, value);
        preferences.end();
        isOpen = false;
        return success;
    }
    
    /**
     * Removes a key from preferences
     * @param ns Namespace
     * @param key Key to remove
     * @return true if successful
     */
    bool remove(const char* ns, const char* key) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!openNamespace(ns, false)) {
            return false;
        }
        
        bool success = preferences.remove(key);
        preferences.end();
        isOpen = false;
        return success;
    }
    
    /**
     * Clears all keys in a namespace
     * @param ns Namespace to clear
     * @return true if successful
     */
    bool clearNamespace(const char* ns) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!openNamespace(ns, false)) {
            return false;
        }
        
        bool success = preferences.clear();
        preferences.end();
        isOpen = false;
        return success;
    }
    
    /**
     * Loads settings from storage
     * @param state Reference to system state to load into
     * @return true if settings were loaded successfully
     */
    bool loadSettings(SystemState& state) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!openNamespace(SYSTEM_NS)) {
            return false;
        }
        
        // Load system settings
        state.systemName = preferences.getString("systemName", DefaultValues::DEFAULT_SYSTEM_NAME);
        state.lightOnTime = preferences.getULong("lightOnTime", DefaultValues::FALLBACK_LIGHT_ON_DURATION);
        state.lightOffTime = preferences.getULong("lightOffTime", DefaultValues::FALLBACK_LIGHT_OFF_DURATION);
        
        // Load unit settings
        if (!openNamespace(UNIT_NS)) {
            return false;
        }
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            String unitKey = "unit" + String(i);
            state.unitsEnabled[i] = preferences.getBool((unitKey + "_enabled").c_str(), DefaultValues::UNITS_ENABLED);
            state.atomizerOnIntervals[i] = preferences.getULong((unitKey + "_onInterval").c_str(), DefaultValues::DEFAULT_ATOMIZER_ON_INTERVAL);
            state.atomizerOffIntervals[i] = preferences.getULong((unitKey + "_offInterval").c_str(), DefaultValues::DEFAULT_ATOMIZER_OFF_INTERVAL);
        }
        
        return true;
    }
    
    /**
     * Saves settings to storage
     * @param state Reference to system state to save
     * @return true if settings were saved successfully
     */
    bool saveSettings(const SystemState& state) {
        ScopedLock lock(*this);
        if (!lock.isLocked()) {
            return false;
        }
        
        if (!openNamespace(SYSTEM_NS, false)) {
            return false;
        }
        
        // Save system settings
        preferences.putString("systemName", state.systemName);
        preferences.putULong("lightOnTime", state.lightOnTime);
        preferences.putULong("lightOffTime", state.lightOffTime);
        
        // Save unit settings
        if (!openNamespace(UNIT_NS, false)) {
            return false;
        }
        
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            String unitKey = "unit" + String(i);
            preferences.putBool((unitKey + "_enabled").c_str(), state.unitsEnabled[i]);
            preferences.putULong((unitKey + "_onInterval").c_str(), state.atomizerOnIntervals[i]);
            preferences.putULong((unitKey + "_offInterval").c_str(), state.atomizerOffIntervals[i]);
        }
        
        return true;
    }
};

#endif // PREFERENCES_MANAGER_H 