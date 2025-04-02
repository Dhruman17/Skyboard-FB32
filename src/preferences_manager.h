#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include "config.h"
#include <Preferences.h>

/**
 * PreferencesManager Class
 * 
 * Handles all non-volatile storage operations using ESP32's Preferences library.
 * Manages system settings persistence across reboots.
 */
class PreferencesManager {
private:
    Preferences preferences;
    static constexpr const char* NAMESPACE = "skyboard";
    
    // Keys for storing settings
    static constexpr const char* KEY_LIGHT_MASTER = "lightMaster";
    static constexpr const char* KEY_TIME_CYCLE = "timeCycle";
    static constexpr const char* KEY_LIGHT_ON = "lightOn";
    static constexpr const char* KEY_LIGHT_OFF = "lightOff";
    
    /**
     * Generates a key for a specific unit setting
     * @param unitIndex Index of the unit
     * @param setting Type of setting (Enabled, OnInt, OffInt)
     * @return Formatted key string
     */
    String getUnitKey(int unitIndex, const char* setting) {
        return String("unit") + String(unitIndex) + String(setting);
    }

public:
    /**
     * Constructor
     */
    PreferencesManager() {
        preferences.begin(NAMESPACE, false);
    }
    
    /**
     * Loads all settings from storage
     * @param state Reference to system state to update
     */
    void loadSettings(SystemState& state) {
        // Load light settings
        state.lightMasterSwitch = preferences.getBool(KEY_LIGHT_MASTER, DefaultValues::LIGHT_MASTER_SWITCH);
        state.timeCycleEnabled = preferences.getBool(KEY_TIME_CYCLE, false);  // Default to false, will use fallback if time not set
        state.lightOnTime = preferences.getULong(KEY_LIGHT_ON, 0);
        state.lightOffTime = preferences.getULong(KEY_LIGHT_OFF, 0);
        
        // Load unit settings
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            state.unitsEnabled[i] = preferences.getBool(
                getUnitKey(i, "Enabled").c_str(), 
                DefaultValues::UNITS_ENABLED[i]
            );
            state.atomizerOnIntervals[i] = preferences.getLong(
                getUnitKey(i, "OnInt").c_str(), 
                DefaultValues::ATOMIZER_ON_INTERVAL
            );
            state.atomizerOffIntervals[i] = preferences.getLong(
                getUnitKey(i, "OffInt").c_str(), 
                DefaultValues::ATOMIZER_OFF_INTERVAL
            );
        }
    }
    
    /**
     * Saves all settings to storage
     * @param state Reference to system state to save
     */
    void saveSettings(const SystemState& state) {
        // Save light settings
        preferences.putBool(KEY_LIGHT_MASTER, state.lightMasterSwitch);
        preferences.putBool(KEY_TIME_CYCLE, state.timeCycleEnabled);
        preferences.putULong(KEY_LIGHT_ON, state.lightOnTime);
        preferences.putULong(KEY_LIGHT_OFF, state.lightOffTime);
        
        // Save unit settings
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            preferences.putBool(getUnitKey(i, "Enabled").c_str(), state.unitsEnabled[i]);
            preferences.putLong(getUnitKey(i, "OnInt").c_str(), state.atomizerOnIntervals[i]);
            preferences.putLong(getUnitKey(i, "OffInt").c_str(), state.atomizerOffIntervals[i]);
        }
    }
    
    /**
     * Clears all stored settings
     */
    void clearSettings() {
        preferences.clear();
    }
};

#endif // PREFERENCES_MANAGER_H 