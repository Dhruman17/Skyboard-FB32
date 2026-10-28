#ifndef GATEWAY_SENSORS_H
#define GATEWAY_SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include <ClosedCube_HDC1080.h>
#include <SensirionI2cScd4x.h>

// The C6 gateway owns the shared ambient sensor package.  These are the same
// sensor families already used by the direct Skyboard firmware, kept local to
// the gateway namespace so the gateway build remains independent of main.cpp.
namespace GatewaySensors {

constexpr uint8_t kSdaPin = 23;
constexpr uint8_t kSclPin = 22;
constexpr uint8_t kHdc1080Address = 0x40;
constexpr uint8_t kScd4xAddress = 0x62;
constexpr float kTemperatureOffsetC = 4.0f;

ClosedCube_HDC1080 hdc1080;
SensirionI2cScd4x scd4x;
float temperatureC = 0.0f;
float humidityPct = 0.0f;
uint16_t co2Ppm = 0;
bool available = false;

inline void begin() {
  Wire.begin(kSdaPin, kSclPin);
  delay(100);
  hdc1080.begin(kHdc1080Address);
  scd4x.begin(Wire, kScd4xAddress);
  scd4x.stopPeriodicMeasurement();
  available = scd4x.startPeriodicMeasurement() == 0;
  if (!available) {
    Serial.println("[Gateway] SCD4x not detected; display will show zero CO2.");
  }
}

inline void read() {
  const float rawTemperatureC = hdc1080.readTemperature();
  const float rawHumidityPct = hdc1080.readHumidity();
  temperatureC = rawTemperatureC - kTemperatureOffsetC;
  const float rawSaturation = 6.112f * expf(17.62f * rawTemperatureC /
                                               (243.12f + rawTemperatureC));
  const float correctedSaturation = 6.112f * expf(17.62f * temperatureC /
                                                     (243.12f + temperatureC));
  humidityPct = constrain(rawHumidityPct * rawSaturation / correctedSaturation,
                          0.0f, 100.0f);

  if (!available) return;
  bool dataReady = false;
  if (scd4x.getDataReadyStatus(dataReady) != 0 || !dataReady) return;

  float sensorTemperature = 0.0f;
  float sensorHumidity = 0.0f;
  if (scd4x.readMeasurement(co2Ppm, sensorTemperature, sensorHumidity) != 0) {
    Serial.println("[Gateway] SCD4x measurement failed.");
  }
}

}  // namespace GatewaySensors

#endif  // GATEWAY_SENSORS_H
