#ifndef CONFIG_H
#define CONFIG_H
#include <addons/TokenHelper.h>
#include <time.h>
extern bool useCapacitiveSensor; // declared in config.h

const double firmware_version = 1.52;

// Variables to track timing
// Pin Definitions
#define NUMBER_OF_UNITS 3
#define SYSTEM_LIGHTS_PIN 26    // GPIO 26 for Lights
#define SYSTEM_12V_POWER_PIN 12 // GPIO for 12V Power

// Pin Arrays:
const int atomizerPins[NUMBER_OF_UNITS] = {4, 5, 2};
const int waterLevelPins[NUMBER_OF_UNITS] = {23, 25, 13};

// PWM Configuration
#define PWM_FREQUENCY_ATOMIZER 108000
#define PWM_RESOLUTION_ATOMIZER 4 // 8-bit resolution
#define PWM_ATOMIZER_ON 9
#define PWM_ATOMIZER_OFF 0
#define TCAADDR 0x77
// Timing Configurations
const unsigned long INTERVAL_30_SECONDS = 30000;
const unsigned long WIFI_RESET_INTERVAL = 60000;
unsigned long lastFirmwareCheckMillis = 0;
const unsigned long FIRMWARE_CHECK_INTERVAL = 360000; // Check every hour

// Serial number
extern String serialNumber;

bool unitsEnabled[NUMBER_OF_UNITS] = {false, false, false};
bool previousWaterLevelStates[3] = {false, false, false};
bool waterLevelStates[3] = {false, false, false};
bool useCapacitiveSensor = true; // New system-wide flag
long atomizerOnIntervals[NUMBER_OF_UNITS] = {5000, 5000, 5000};
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
bool isConnected = false;    // Flag to track Wi-Fi connection status

#endif // CONFIG_H