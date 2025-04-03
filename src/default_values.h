#ifndef DEFAULT_VALUES_H
#define DEFAULT_VALUES_H

#include "config.h"

/**
 * Default Values
 * Contains default values for system configuration
 */
namespace DefaultValues {
    // Light Control
    constexpr bool LIGHT_MASTER_SWITCH = false;
    constexpr unsigned long FALLBACK_LIGHT_ON_DURATION = 21600000;  // 6 hours in milliseconds
    constexpr unsigned long FALLBACK_LIGHT_OFF_DURATION = 64800000;  // 18 hours in milliseconds
    
    // Unit Settings
    constexpr bool UNITS_ENABLED[SystemConfig::NUMBER_OF_UNITS] = {true, true, true};
    constexpr unsigned long ATOMIZER_ON_INTERVAL = 300;  // 5 minutes
    constexpr unsigned long ATOMIZER_OFF_INTERVAL = 1800;  // 30 minutes
    constexpr unsigned long DEFAULT_ATOMIZER_ON_INTERVAL = 300;  // 5 minutes
    constexpr unsigned long DEFAULT_ATOMIZER_OFF_INTERVAL = 1800;  // 30 minutes
    
    // Sensor Settings
    constexpr float DEFAULT_WATER_LEVEL = 0.0f;
    constexpr float DEFAULT_EC_VALUE = 0.0f;
    
    // System Settings
    constexpr const char* DEFAULT_SYSTEM_NAME = "Skyboard System";
    constexpr const char* DEFAULT_FIRMWARE_VERSION = "1.0.0";
}

#endif // DEFAULT_VALUES_H 