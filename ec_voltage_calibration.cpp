/**
 * EC Sensor Voltage Calibration Tool
 *
 * This simple program reads and displays the raw voltage from the EC sensor
 * connected to Port 1 (TCA channel 1). Use this to test different EC solutions
 * and build a calibration curve (EC vs. Voltage).
 *
 * Hardware:
 * - EC sensor on Port 1 (TCA channel 1)
 * - MCP3021 ADC
 * - TCA9548A I2C multiplexer
 */

#include <Arduino.h>
#include <Wire.h>
#include "MCP3X21.h"

// I2C Pins (adjust based on your board)
#define SDA 21
#define SCL 22

// TCA9548A I2C multiplexer address
#define TCAADDR 0x77

// Port configuration
#define EC_PORT_1_CHANNEL 1  // TCA channel for Port 1

// MCP3021 ADC instance
MCP3021 mcp3021;

/**
 * Select TCA multiplexer channel
 */
void tcaselect(uint8_t channel) {
    if (channel > 7) return;

    Wire.beginTransmission(TCAADDR);
    Wire.write(1 << channel);
    Wire.endTransmission();
}

/**
 * Read voltage from EC sensor on Port 1
 */
float readECVoltage() {
    // Select Port 1 channel
    tcaselect(EC_PORT_1_CHANNEL);
    delay(50);  // Allow channel switch to complete

    // Read ADC value
    uint16_t adcValue = mcp3021.read();

    // Convert to voltage (in volts)
    // mcp3021.toVoltage returns millivolts, so divide by 1000
    float voltage = mcp3021.toVoltage(adcValue, 3300) / 1000.0;

    return voltage;
}

void setup() {
    Serial.begin(9600);
    delay(1000);

    Serial.println("========================================");
    Serial.println("EC Sensor Voltage Calibration Tool");
    Serial.println("Port 1 (TCA Channel 1)");
    Serial.println("========================================");
    Serial.println();

    // Initialize I2C
    Wire.begin(SDA, SCL);
    Serial.println("I2C initialized");

    // Initialize MCP3021 ADC
    mcp3021.init(&Wire);
    Serial.println("MCP3021 ADC initialized");
    Serial.println();

    Serial.println("Ready to read EC voltage values...");
    Serial.println("Format: Voltage (V) | ADC Raw Value");
    Serial.println("========================================");
    Serial.println();
}

void loop() {
    // Select Port 1 channel
    tcaselect(EC_PORT_1_CHANNEL);
    delay(50);

    // Read raw ADC value
    uint16_t adcRaw = mcp3021.read();

    // Convert to voltage
    float voltage = mcp3021.toVoltage(adcRaw, 3300) / 1000.0;

    // Display results
    Serial.print("Voltage: ");
    Serial.print(voltage, 4);  // 4 decimal places
    Serial.print(" V  |  ADC Raw: ");
    Serial.println(adcRaw);

    // Read every 2 seconds
    delay(2000);
}
