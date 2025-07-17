#ifndef CONFIG_H
#define CONFIG_H

#include <addons/TokenHelper.h>
#include <time.h>

// === Firmware Version ===
const double firmware_version = 1.56;

// === Select Board Type via build_flags ===
// Use -DESP32_THREE_PORT or -DESP32_S3 in platformio.ini

// === Pin Definitions ===
#define NUMBER_OF_UNITS 3

#ifdef ESP32_THREE_PORT
  #define SYSTEM_LIGHTS_PIN 26
  #define SDA 21
  #define SCL 22
#elif defined(ESP32_S3)
  #define SYSTEM_LIGHTS_PIN 7
  #define SDA 13
  #define SCL 14
#elif defined(OLD_FLOAT_BOARD)
  #define SYSTEM_LIGHTS_PIN 26
#else
  #error "Please define either ESP32_THREE_PORT or ESP32_S3 in build_flags."
#endif

#define SYSTEM_12V_POWER_PIN 12

// === Pin Arrays ===
const int atomizerPins[NUMBER_OF_UNITS] = {4, 5, 2};
const int waterLevelPins[NUMBER_OF_UNITS] = {23, 25, 13}; //Changes based on board mostly same

// === PWM Configuration ===
#define PWM_FREQUENCY_ATOMIZER 108000
#define PWM_RESOLUTION_ATOMIZER 4
#define PWM_ATOMIZER_ON 9
#define PWM_ATOMIZER_OFF 0

// === I2C Multiplexer ===
#define TCAADDR 0x77

// === Timing Configurations ===
const unsigned long INTERVAL_30_SECONDS = 30000;
const unsigned long WIFI_RESET_INTERVAL = 60000;
const unsigned long FIRMWARE_CHECK_INTERVAL = 360000;
const unsigned long resetInterval = 21600000; // 6 hours

// === Global State Variables ===
extern String serialNumber;
extern bool useCapacitiveSensor;

bool unitsEnabled[NUMBER_OF_UNITS] = {false, false, false};
bool previousWaterLevelStates[NUMBER_OF_UNITS] = {false, false, false};
bool waterLevelStates[NUMBER_OF_UNITS] = {false, false, false};
bool useCapacitiveSensor = true;

long atomizerOnIntervals[NUMBER_OF_UNITS] = {5000, 5000, 5000};
long atomizerOffIntervals[NUMBER_OF_UNITS] = {5000, 5000, 5000};
bool atomStates[NUMBER_OF_UNITS] = {false, false, false};
unsigned long previousMillis[NUMBER_OF_UNITS] = {0, 0, 0};

unsigned long previousHeartbeatMillis = 0;
unsigned long lastConnectionCheckMillis = 0;
unsigned long lastNotificationMillis = 0;
unsigned long lastResetMillis = 0;
unsigned long lastFirmwareCheckMillis = 0;
unsigned long startTime = 0;
bool isConnected = false;

// === Mutex Declarations ===
extern SemaphoreHandle_t sensorMutex;
extern SemaphoreHandle_t firebaseMutex;

#endif // CONFIG_H
