#ifndef SENSOR_COMS
#define SENSOR_COMS
#include <Wire.h>
#include <ClosedCube_HDC1080.h>
#include <SparkFun_SCD30_Arduino_Library.h>

SCD30 scd30;
ClosedCube_HDC1080 hdc1080;
#define UPPER_BOUND 0X4000
#define LOWER_BOUND (-1 * UPPER_BOUND)
#define CHANNEL 2
#define MEASURMENT 0
#ifndef SENSOR_COMS
#define SENSOR_COMS
#include <Wire.h>
#include <ClosedCube_HDC1080.h>
#include "Adafruit_SGP30.h"

ClosedCube_HDC1080 hdc1080;
Adafruit_SGP30 sgp;

#define UPPER_BOUND 0X4000
#define LOWER_BOUND (-1 * UPPER_BOUND)
#define CHANNEL 2
#define MEASURMENT 0

float temperature = 0.0;
float humidity = 0.0;
float co2ppm = 0.0; // Replace logic if not using SGP30
int counter = 0;
int count = 60;
int raw=400;
// TCA9548A I2C multiplexer address (usually 0x77)
#define TCAADDR 0x77

// External mutex declaration
extern SemaphoreHandle_t sensorMutex;

// Multiplexer channel for SHT31 sensor
#define SHT31_CHANNEL 6 // Change if your SHT31 is on a different channel

// I2C multiplexer selector
void tcaselect(uint8_t i)
{
    if (i > 7)
        return;
    Wire.beginTransmission(TCAADDR);
    Wire.write(1 << i);
    Wire.endTransmission();
}

// Device constants
namespace device
{
    float aref = 3.3;
}

#endif // SENSOR_COMS

float temperature = 0.0;
float humidity = 0.0;
float co2ppm = 0.0; // Replace logic if not using SGP30

// TCA9548A I2C multiplexer address (usually 0x77)
#define TCAADDR 0x77

// External mutex declaration
extern SemaphoreHandle_t sensorMutex;

// Multiplexer channel for SHT31 sensor
#define SHT31_CHANNEL 6 // Change if your SHT31 is on a different channel

// I2C multiplexer selector
void tcaselect(uint8_t i)
{
    if (i > 7)
        return;
    Wire.beginTransmission(TCAADDR);
    Wire.write(1 << i);
    Wire.endTransmission();
}

// Device constants
namespace device
{
    float aref = 3.3;
}

namespace sensors
{
    float temperature = 0.0;
    float humidity = 0.0;
    float co2ppm = 0.0;

    bool initSensors()
    {
        Wire.begin(SDA, SCL); // SDA and SCL are defined in config.h

        // Initialize HDC1080 (Temp + Humidity)
        hdc1080.begin(0x40); // Default I2C address
        Serial.println("✅ HDC1080 initialized.");

        // Initialize SCD30 (CO2)
        if (!scd30.begin())
        {
            Serial.println("❌ Failed to initialize SCD30 CO2 sensor.");
            return false;
        }

        Serial.println("✅ SCD30 CO2 sensor initialized.");
        return true;
    }

    void readTHSensors()
    {
        temperature = hdc1080.readTemperature();
        humidity = hdc1080.readHumidity();

        Serial.print("🌡️ Temp: ");
        Serial.print(temperature);
        Serial.print(" °C | 💧 Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");
    }

    void readCO2Sensor()
    {
        if (scd30.dataAvailable())
        {
            co2ppm = scd30.getCO2();
            Serial.print("🟢 CO2: ");
            Serial.print(co2ppm);
            Serial.println(" ppm");
        }
        else
        {
            Serial.println("⚠️ No new CO2 data available.");
            co2ppm = -1; // Invalid value for safety
        }
    }
}
#endif // SENSOR_COMS
