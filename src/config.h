#ifndef CONFIG_H
#define CONFIG_H
#include <addons/TokenHelper.h>
#include <time.h>
// Pin Definitions
#define ATOMIZER_PIN_5 5
#define ATOMIZER_PIN_4 4
#define ATOMIZER_PIN_2 2
#define WATER_LEVEL_PIN_25 25 // Float sensor for Unit1
#define WATER_LEVEL_PIN_23 23 // Float sensor for Unit2
#define WATER_LEVEL_PIN_13 13 // Float sensor for Unit3
#define SYSTEM_LIGHTS_PIN_26 26   // GPIO 26 for LED
#define SYSTEM_POWER_PIN_12 12

// PWM Configuration
#define ATOMIZER_PWM_CHANNEL_1 1
#define ATOMIZER_PWM_CHANNEL_0 0
#define ATOMIZER_PWM_CHANNEL_2 2
#define SYSTEM_LIGHTS_PWM_CHANNEL_3 3
#define PWM_FREQUENCY 108000
#define PWM_RESOLUTION 4 // 8-bit resolution

// Timing Configurations
const unsigned long HEARTBEAT_INTERVAL = 300000;  // 5 minutes for heartbeat

// Serial number
extern String serialNumber;

// Variables for system name and units
extern String systemPath;
extern String units[3];
extern String systemName;

// Light control settings
extern time_t atomizerOnTime;
extern time_t atomizerOffTime;
extern bool systemLightSwitch;
extern bool systemLightTimeCycleSwitch;

bool unitStates[3] = {false, false, false}; 
bool previousUnitStates[3] = {false, false, false};
bool waterLevelStates[3] = {false, false, false};
bool previousWaterLevelStates[3] = {false, false, false};
long intervalOn[3] = {5000, 5000, 5000};
long intervalOff[3] = {5000, 5000, 5000};
bool ledStates[3] = {false, false, false};
unsigned long previousMillis[3] = {0, 0, 0};

unsigned long previousHeartbeatMillis = 0;
unsigned long lastConnectionCheckMillis = 0;
unsigned long lastNotificationMillis = 0;
// Constants for reset interval
const unsigned long resetInterval = 21600000; // 6 hours in milliseconds
unsigned long lastResetMillis = 0;
unsigned long startTime = 0; // Variable to store the start time of the delay
unsigned long randomDelay = 0; // Random delay before connecting to Wi-Fi
bool isConnected = false;  // Flag to track Wi-Fi connection status

#endif // CONFIG_H
