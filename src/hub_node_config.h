#pragma once

#include <cstdint>

// Hardware constants used by the Hub-managed ESP-NOW firmware only.  Keeping
// them separate from config.h prevents Hub builds from pulling in the legacy
// direct-Firebase configuration and its global state.
#ifndef SKYBOARD_HUB_MODE
#define SKYBOARD_HUB_MODE 0
#endif

#if !SKYBOARD_HUB_MODE
#error "hub_node_config.h is only for Hub-managed firmware builds."
#endif

#define NUMBER_OF_UNITS 3

#ifdef ESP32_THREE_PORT
#define SYSTEM_LIGHTS_PIN 26
#elif defined(ESP32_S3)
#define SYSTEM_LIGHTS_PIN 7
#else
#error "Define ESP32_THREE_PORT or ESP32_S3 for Hub firmware builds."
#endif

#define SYSTEM_12V_POWER_PIN 12
constexpr int atomizerPins[NUMBER_OF_UNITS] = {4, 5, 2};

#define PWM_FREQUENCY_ATOMIZER 108000
#define PWM_RESOLUTION_ATOMIZER 4
#define PWM_ATOMIZER_ON 9
#define PWM_ATOMIZER_OFF 0

constexpr uint16_t HUB_FIRMWARE_VERSION_CENTIS = 161;
