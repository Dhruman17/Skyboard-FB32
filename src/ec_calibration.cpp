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

// Use USB Serial for ESP32-S3
#if ARDUINO_USB_CDC_ON_BOOT
#define SerialOutput Serial
#else
#define SerialOutput Serial
#endif

// I2C Pins for ESP32-S3
#define SDA 13
#define SCL 14

// TCA9548A I2C multiplexer address
#define TCAADDR 0x77

// Port configuration
#define EC_PORT_1_CHANNEL 2  // TCA channel for Port 1 (found on channel 2 from scan)

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

void scanI2C() {
    Serial.println("Scanning main I2C bus...");
    byte count = 0;
    for (byte i = 1; i < 127; i++) {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            Serial.print("  Found device at 0x");
            if (i < 16) Serial.print("0");
            Serial.print(i, HEX);
            if (i == 0x77) Serial.print(" (TCA9548A Multiplexer)");
            if (i == 0x4D) Serial.print(" (MCP3021 ADC)");
            Serial.println();
            count++;
        }
    }
    if (count == 0) {
        Serial.println("  No I2C devices found!");
    } else {
        Serial.print("  Total: ");
        Serial.print(count);
        Serial.println(" device(s)");
    }
    Serial.println();
}

void scanTCAChannel(uint8_t channel) {
    tcaselect(channel);
    delay(100);

    Serial.print("Scanning TCA Channel ");
    Serial.print(channel);
    Serial.println("...");

    byte count = 0;
    for (byte i = 1; i < 127; i++) {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            Serial.print("  Found device at 0x");
            if (i < 16) Serial.print("0");
            Serial.print(i, HEX);
            if (i == 0x4D) Serial.print(" (MCP3021 ADC) <-- TARGET");
            Serial.println();
            count++;
        }
    }

    if (count == 0) {
        Serial.println("  No devices on this channel");
    }
    Serial.println();
}

void scanAllTCAChannels() {
    Serial.println("========================================");
    Serial.println("Scanning all TCA channels for MCP3021...");
    Serial.println("========================================");
    for (uint8_t ch = 0; ch < 8; ch++) {
        scanTCAChannel(ch);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========================================");
    Serial.println("EC Sensor Voltage Calibration Tool");
    Serial.println("========================================");
    Serial.println();

    // Initialize I2C
    Wire.begin(SDA, SCL);
    Serial.println("I2C initialized");

    // Initialize MCP3021 ADC
    mcp3021.init(&Wire);
    Serial.println("MCP3021 ADC initialized on TCA Channel 2");
    Serial.println();

    Serial.println("Ready to read EC voltage values...");
    Serial.println("Readings every 2 seconds:");
    Serial.println("Format: Voltage (V) | ADC Raw Value");
    Serial.println("========================================");
    Serial.println();
}

void loop() {
    // Select Port 1 channel (Channel 2)
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
