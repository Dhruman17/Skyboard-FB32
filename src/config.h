#ifndef CONFIG_H
#define CONFIG_H
#include <addons/TokenHelper.h>
#include <time.h>

<<<<<<< Updated upstream
// Variables to track timing
// Pin Definitions
#define NUMBER_OF_UNITS 3
#define SYSTEM_LIGHTS_PIN 26   // GPIO 26 for Lights
#define SYSTEM_12V_POWER_PIN 12 // GPIO for 12V Power
=======
// System Configuration
struct SystemConfig {
    // Version
    static const double FIRMWARE_VERSION = 1.4;
    
    // Hardware Configuration
    static const int NUMBER_OF_UNITS = 3;
    static const int SYSTEM_LIGHTS_PIN = 26;    // GPIO 26 for Lights
    static const int SYSTEM_12V_POWER_PIN = 12; // GPIO for 12V Power
    
    // Pin Arrays
    static const int ATOMIZER_PINS[NUMBER_OF_UNITS];
    static const int WATER_LEVEL_PINS[NUMBER_OF_UNITS];
    
    // PWM Configuration
    static const int PWM_FREQUENCY_ATOMIZER = 108000;
    static const int PWM_RESOLUTION_ATOMIZER = 4; // 8-bit resolution
    static const int PWM_ATOMIZER_ON = 9;
    static const int PWM_ATOMIZER_OFF = 0;
    
    // I2C Configuration
    static const int TCAADDR = 0x77;
    
    // Timing Configuration
    static const unsigned long INTERVAL_30_SECONDS = 30000;
    static const unsigned long WIFI_RESET_INTERVAL = 60000;
    static const unsigned long FIRMWARE_CHECK_INTERVAL = 3600; // Check every hour
    static const unsigned long WATER_LEVEL_READ_INTERVAL = 100;
    static const unsigned long RESET_INTERVAL = 21600000; // 6 hours in milliseconds
    
    // Sensor Configuration
    static const float WATER_LEVEL_CALIBRATION_OFFSET = 1.58;
    static const float WATER_LEVEL_CALIBRATION_FACTOR = 0.107;
    static const int16_t UPPER_BOUND = 0X4000; // max readout capacitance
    static const int16_t LOWER_BOUND = -1 * UPPER_BOUND;
    static const int CHANNEL = 2;    // channel to be read
    static const int MEASURMENT = 0; // measurment channel
};

// Initialize static members
const int SystemConfig::ATOMIZER_PINS[NUMBER_OF_UNITS] = {4, 5, 2};
const int SystemConfig::WATER_LEVEL_PINS[NUMBER_OF_UNITS] = {23, 25, 13};
>>>>>>> Stashed changes

// State Variables
struct SystemState {
    bool unitsEnabled[NUMBER_OF_UNITS] = {false, false, false};
    bool previousWaterLevelStates[NUMBER_OF_UNITS] = {false, false, false};
    bool waterLevelStates[NUMBER_OF_UNITS] = {false, false, false};
    long atomizerOnIntervals[NUMBER_OF_UNITS] = {5000, 5000, 5000};
    long atomizerOffIntervals[NUMBER_OF_UNITS] = {5000, 5000, 5000};
    bool atomStates[NUMBER_OF_UNITS] = {false, false, false};
    unsigned long previousMillis[NUMBER_OF_UNITS] = {0, 0, 0};
    unsigned long previousHeartbeatMillis = 0;
    unsigned long lastConnectionCheckMillis = 0;
    unsigned long lastNotificationMillis = 0;
    unsigned long lastResetMillis = 0;
    unsigned long startTime = 0;
    bool isConnected = false;
    unsigned long lastFirmwareCheckMillis = 0;
};

<<<<<<< Updated upstream
// PWM Configuration
#define PWM_FREQUENCY_ATOMIZER 108000
#define PWM_RESOLUTION_ATOMIZER 4 // 8-bit resolution
#define PWM_RESOLUTION_ATOMIZER 4 // 8-bit resolution
#define PWM_ATOMIZER_ON 9
#define PWM_ATOMIZER_OFF 0

// Timing Configurations
const unsigned long INTERVAL_30_SECONDS = 30000;
const unsigned long WIFI_RESET_INTERVAL = 60000;

// Serial number
extern String serialNumber;

// Variables for system name and units
extern String systemPath;
extern String unitNames[3];
extern String systemName;

bool unitsEnabled[3] = {false, false, false}; 
bool previousWaterLevelStates[3] = {false, false, false};
bool waterLevelStates[3] = {false, false, false};
long atomizerOnIntervals[3] = {5000, 5000, 5000};
long atomizerOffIntervals[3] = {5000, 5000, 5000};
bool atomStates[3] = {false, false, false};
unsigned long previousMillis[3] = {0, 0, 0};

unsigned long previousHeartbeatMillis = 0;
unsigned long lastConnectionCheckMillis = 0;
unsigned long lastNotificationMillis = 0;
// Constants for reset interval
const unsigned long resetInterval = 21600000; // 6 hours in milliseconds
unsigned long lastResetMillis = 0;
unsigned long startTime = 0; // Variable to store the start time of the delay
bool isConnected = false;  // Flag to track Wi-Fi connection status
=======
// External declarations
extern String serialNumber;
extern SystemState systemState;
>>>>>>> Stashed changes

#endif // CONFIG_H