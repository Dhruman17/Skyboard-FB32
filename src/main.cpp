#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <time.h>
#include <credentials.h>
#include <wifi_handler.h>
#include <config.h>
#include <sensor_coms.h>
#include <firebase_coms.h>
#include <WiFiManager.h> // WiFiManager by Tzapu
#include <ArduinoOTA.h>  // OTA functionality
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Update.h>
#include "Wire.h"
#include "MCP3X21.h" // ADC library for float sensor
#include "esp_ota_ops.h"
#include <Protocentral_FDC1004.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mutex_handler.h" // if you created this
#define FIREBASEJSON_USE_PSRAM

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

String systemName = ""; // Will be fetched from Firestore
time_t lightOnTime;
time_t lightOffTime;
time_t atomizerOffTime;
bool lightMasterSwitch = false; // The master light switch from Firebase
bool timeCycleEnabled = false;
bool lightState = false;
String unitNames[NUMBER_OF_UNITS]; // Storing the names of units with suffixes
unsigned long startupTime = 0;

// Generate the random delays
int randomDelay;
int connectionOffset;
bool wifiConnected = false; // Track Wi-Fi connection status
bool configPortalRunning = false;

WiFiManager wm;

int capdac = 0;
char result[100];
FDC1004 FDC;
MCP3021 mcp3021;
double measuredCap;
double waterLevel;
double temp;
// mod-mutex inititalization
SemaphoreHandle_t sensorMutex = xSemaphoreCreateMutex();
SemaphoreHandle_t firebaseMutex = xSemaphoreCreateMutex();

// Function to initialize NTP
void initializeTime()
{
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

// Modify systemLights to consider Light Master Switch and Time Cycle Switch
void systemLights()
{
    time_t now;
    struct tm *currentTime;
    time(&now);
    currentTime = localtime(&now);

    struct tm currentTimeOfDay = *currentTime;
    currentTimeOfDay.tm_year = 70; // Epoch year
    currentTimeOfDay.tm_mon = 0;   // January
    currentTimeOfDay.tm_mday = 1;  // 1st of the month
    time_t currentTime_t = mktime(&currentTimeOfDay);
    if (timeCycleEnabled)
    {
        if (lightOffTime < lightOnTime) // if On time happens the following day (eg. 5pm off, 2am +1 on)
        {

            if (currentTime_t >= lightOnTime || currentTime_t <= lightOffTime)
            // if the current time is after the on time or the current time is before or the same as the off time
            {
                if (!lightState)
                { // if the lights are off, turn them on
                    digitalWrite(SYSTEM_LIGHTS_PIN, HIGH);
                    lightState = true;
                }
            }
            else
            {
                // if the current time is before the on time or the current time is after the off time
                if (lightState)
                {
                    digitalWrite(SYSTEM_LIGHTS_PIN, LOW);
                    lightState = false;
                }
            }
        }
        else
        // if the On time and off time happens the same day (eg. 9am to 5pm)
        {
            if (currentTime_t >= lightOnTime && currentTime_t <= lightOffTime)
            // if the current time is after the on time or the same time as the off time,
            // and the current time is before or the same as the off time
            {
                if (!lightState)
                {
                    digitalWrite(SYSTEM_LIGHTS_PIN, HIGH);
                    lightState = true;
                }
            }
            else
            {
                if (lightState)
                {
                    digitalWrite(SYSTEM_LIGHTS_PIN, LOW);
                    lightState = false;
                }
            }
        }
    }
    else
    {
        if (lightState != lightMasterSwitch)
        {
            if (lightMasterSwitch)
            {
                digitalWrite(SYSTEM_LIGHTS_PIN, HIGH);
                lightState = true;
            }
            else
            {
                digitalWrite(SYSTEM_LIGHTS_PIN, LOW);
                lightState = false;
            }
        }
    }
}
void readWaterLevel()
{
    if (xSemaphoreTake(sensorMutex, portMAX_DELAY))
    {
        int capdacs[NUMBER_OF_UNITS] = {0, 0, 0};
        int tcaChannels[NUMBER_OF_UNITS] = {0, 2, 4};

        for (int i = 0; i < NUMBER_OF_UNITS; i++)
        {
            if (useCapacitiveSensor) // new system-wide flag

            {
                tcaselect(tcaChannels[i]);
                FDC.configureMeasurementSingle(MEASURMENT, CHANNEL, capdacs[i]);
                FDC.triggerSingleMeasurement(MEASURMENT, FDC1004_100HZ);
                delay(100);

                uint16_t value[2];
                if (!FDC.readMeasurement(MEASURMENT, value))
                {
                    int16_t msb = (int16_t)value[0];
                    int32_t capacitance = ((int32_t)457) * msb;
                    capacitance /= 1000;
                    capacitance += ((int32_t)3028) * capdacs[i];
                    measuredCap = (float)capacitance / 1000;
                    waterLevel = (measuredCap - 1.58) / 0.107;

                    Serial.print("Unit ");
                    Serial.print(i);
                    Serial.print(" | Capacitance: ");
                    Serial.print(measuredCap, 4);
                    Serial.print(" pf | Water Level: ");
                    Serial.println(waterLevel);

                    if (!unitNames[i].isEmpty())
                    {
                        sendUnitCapValueToFirebase(&fbdo, unitNames[i], measuredCap);
                    }

                    // Auto-calibrate capdac
                    if (msb > UPPER_BOUND && capdacs[i] < FDC1004_CAPDAC_MAX)
                        capdacs[i]++;
                    else if (msb < LOWER_BOUND && capdacs[i] > 0)
                        capdacs[i]--;
                }
            }
            else
            {
                // :white_check_mark: New ADC-based float sensor reading
                int tcaChannels[NUMBER_OF_UNITS] = {0, 2, 4}; // Channels you use for MCP3021 per unit
                tcaselect(tcaChannels[i]);
                delay(100);
                uint16_t result = mcp3021.read();
                float floatSignal = (mcp3021.toVoltage(result, 3300) / 1000.000);
                bool floatState = (floatSignal > 0.5); // Above 0.5V means water present
                waterLevelStates[i] = floatState;
                Serial.print("Unit ");
                Serial.print(i);
                Serial.print(" | Float Sensor (via ADC): ");
                Serial.println(floatState ? "WATER PRESENT" : "DRY");
                if (!unitNames[i].isEmpty())
                {
                    sendFloatSensorState(&fbdo, unitNames[i], floatState);
                }
            }
        }
        xSemaphoreGive(sensorMutex);
    }
}
uint32_t getAbsoluteHumidity(float temperature, float humidity)
{
    // approximation formula from Sensirion SGP30 Driver Integration chapter 3.15
    const float absoluteHumidity = 216.7f * ((humidity / 100.0f) * 6.112f * exp((17.62f * temperature) / (243.12f + temperature)) / (273.15f + temperature)); // [g/m^3]
    const uint32_t absoluteHumidityScaled = static_cast<uint32_t>(1000.0f * absoluteHumidity);                                                                // [mg/m^3]
    return absoluteHumidityScaled;
}
void readSensors()
{
    int tcaChannels[NUMBER_OF_UNITS] = {0, 2, 4};
    hdc1080.begin(0x40); // Only needs to be called once outside loop

    //  for (int i = 0; i < NUMBER_OF_UNITS; i++) {// Kept is multiple sensor boards are connected change all 0 values to i if incorporating the loop
    tcaselect(tcaChannels[0]);

    float temp = hdc1080.readTemperature();
    float hum = hdc1080.readHumidity();

    Serial.print("Unit ");
    Serial.print(0);
    Serial.print(" | Temp: ");
    Serial.print(temp);
    Serial.print(" °C, Humidity: ");
    Serial.print(hum);
    Serial.println(" %");

    // Optionally, store per-unit data
    // Or compute average if needed
    // }

    // If you're storing one set of values globally for Firebase:
    temperature = hdc1080.readTemperature(); // Optionally pick from channel 0 again
    humidity = hdc1080.readHumidity();
}

void readCO2()
{
    int tcaChannels[NUMBER_OF_UNITS] = {0, 2, 4};
    int sum = 0;
    int validReadings = 0;

    // for (int i = 0; i < NUMBER_OF_UNITS; i++) {  // Kept is multiple sensor boards are connected change all 0 values to i if incorporating the loop
    tcaselect(tcaChannels[0]);

    if (!sgp.begin())
    {
        Serial.print("SGP30 init failed on channel ");
        Serial.println(tcaChannels[0]);
        // continue;
    }

    sgp.setHumidity(getAbsoluteHumidity(temperature, humidity));

    if (sgp.IAQmeasure())
    {
        Serial.print("Unit ");
        Serial.print(0);
        Serial.print(" | eCO2: ");
        Serial.println(sgp.eCO2);
        sum += sgp.eCO2;
        validReadings++;
    }
    else
    {
        Serial.print("Failed CO2 read on unit ");
        Serial.println(0);
    }
    // }

    if (validReadings > 0)
    {
        co2ppm = sum / validReadings;
        Serial.print("Averaged CO2 ppm: ");
        Serial.println(co2ppm);
    }
    else
    {
        Serial.println("No valid CO2 readings.");
        co2ppm = 0;
    }
}

// Function to read EC sensor value from MCP3021 ADC
void readECSensorValue()
{
    if (xSemaphoreTake(sensorMutex, portMAX_DELAY))
    {
        float calibratedECs[NUMBER_OF_UNITS];
        int tcaChannels[NUMBER_OF_UNITS] = {1, 3, 5};

        for (int i = 0; i < NUMBER_OF_UNITS; i++)
        {
            tcaselect(tcaChannels[i]);
            uint16_t result = mcp3021.read();
            float rawEc = (mcp3021.toVoltage(result, 3300) / 1000.0);
            float calibratedEC = 0.727 - (0.365 * rawEc) + (0.416 * rawEc * rawEc);
            calibratedECs[i] = calibratedEC;

            Serial.print("EC sensor ");
            Serial.print(i + 1);
            Serial.print(" reading: ");
            Serial.println(calibratedEC);

            if (!unitNames[i].isEmpty())
            {
                sendUnitECValueToFirebase(&fbdo, unitNames[i], calibratedEC);
            }
        }
        xSemaphoreGive(sensorMutex);
    }
}

void updateUnits()

// A function that updates and sends atomizer signals and sends a command to update the water level state when atomizers are off
{
    if (xSemaphoreTake(firebaseMutex, portMAX_DELAY)) {
    unsigned long currentMillis = millis();
    for (int i = 0; i < NUMBER_OF_UNITS; i++)
    {
        if (unitsEnabled[i])
        {
            if (currentMillis - previousMillis[i] >= (atomStates[i] ? atomizerOnIntervals[i] : atomizerOffIntervals[i]))
            {
                // if more time has passed than the (atomizer on interval if the atomizer state is on, or atomizer off interval if the atomizer state is off)
                atomStates[i] = !atomStates[i];                                   // Record the atomizer state as the opposite
                ledcWrite(i, atomStates[i] ? PWM_ATOMIZER_ON : PWM_ATOMIZER_OFF); // Send the atomizer signal according to this opposite state
                if (!atomStates[0] && !atomStates[1] && !atomStates[2])
                {
                    // updateWaterLevelStates();
                } // read the water level state only if the atomizers are off
                previousMillis[i] = currentMillis; // reset the time counter
            }
    } xSemaphoreGive(firebaseMutex);
  }}
}
void printPartitionInfo()
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    Serial.print("Running partition: ");
    Serial.println(running->label);
}

void updateSystemVersion()
{
    if (systemPath != "")
    {
        String documentPath = systemPath;
        FirebaseJson content;

        // Update firmware version
        content.set("fields/version/doubleValue", firmware_version);

        // Construct firmware URL based on serial number
        String firmwareUrl = "https://firebasestorage.googleapis.com/v0/b/" + String(FIREBASE_PROJECT_ID) +
                             ".appspot.com/o/firmware_" + serialNumber + ".bin?alt=media";

        // Store firmware URL in Firestore
        content.set("fields/firmware_url/stringValue", firmwareUrl);

        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "version,firmware_url"))
        {
            Serial.println("System version and firmware URL updated successfully in Firestore.");
        }
        else
        {
            Serial.println("Failed to update system version.");
            Serial.println(fbdo.errorReason());
        }
    }
    else
    {
        Serial.println("System path is not defined. Cannot update version.");
    }
}
float fetchLatestVersion()
{
     if (xSemaphoreTake(firebaseMutex, portMAX_DELAY)) {
    HTTPClient http;

    String versionUrl = "https://firebasestorage.googleapis.com/v0/b/";
    versionUrl += FIREBASE_PROJECT_ID;
    versionUrl += ".appspot.com/o/";
    versionUrl += serialNumber + "%2FVersion.txt?alt=media"; // %2F = "/"

    http.begin(versionUrl);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        String versionString = http.getString();
        http.end();
        return versionString.toFloat();
    }
    else
    {
        Serial.println("Failed to fetch Version.txt");
        Serial.println(http.errorToString(httpCode));
        http.end();
        return firmware_version; // fallback
           xSemaphoreGive(firebaseMutex);
  }
    }
}

void updateFirmwareVersionInFirestore(float newVersion)
{
if (xSemaphoreTake(firebaseMutex, portMAX_DELAY)) {
    if (systemPath != "")
    {
        String documentPath = systemPath;
        FirebaseJson content;

        // Update Firestore with the new firmware version
        content.set("fields/version/doubleValue", newVersion);

        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "version"))
        {
            Serial.print("Updated Firestore firmware version to: ");
            Serial.println(newVersion);
        }
        else
        {
            Serial.println("Failed to update firmware version in Firestore.");
            Serial.println(fbdo.errorReason());
        }
    }
       xSemaphoreGive(firebaseMutex);
  }
}
void performOTAUpdate(String firmwareUrl, float newFirmwareVersion)
{
    HTTPClient http;
    Serial.println("Connecting to firmware URL...");
    http.begin(firmwareUrl);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        int contentLength = http.getSize();
        WiFiClient *stream = http.getStreamPtr();

        Serial.print("Firmware size (expected): ");
        Serial.println(contentLength);
        Serial.print("🛠 Available Flash Space: ");
        Serial.println(ESP.getFreeSketchSpace());

        printPartitionInfo(); // Print partition info

        if (contentLength > ESP.getFreeSketchSpace())
        {
            Serial.println("Not enough space for OTA update! Aborting...");
            return;
        }

        Serial.println("Initializing OTA update...");
        if (!Update.begin(contentLength))
        {
            Serial.println("Update.begin() failed! Not enough space?");
            return;
        }

        Serial.println("Writing firmware...");
        size_t written = 0;
        int chunkSize = 1024;
        unsigned long timeout = millis();
        while (written < contentLength)
        {
            if (stream->available())
            {
                uint8_t buffer[chunkSize];
                int bytesRead = stream->read(buffer, chunkSize);
                if (bytesRead <= 0)
                {
                    Serial.println("Read error or no data.");
                    break;
                }

                written += Update.write(buffer, bytesRead);
                Serial.printf("Total bytes written: %u\n", written);

                timeout = millis(); // Reset timeout on successful read
            }
            else
            {
                delay(10);
                if (millis() - timeout > 10000)
                { // 10s timeout
                    Serial.println("Timeout waiting for more data.");
                    break;
                }
            }
        }

        Serial.print("Final bytes written: ");
        Serial.println(written);

        if (written == contentLength)
        {
            Serial.println("OTA update successful!");

            // **Update Firestore before rebooting**
            updateFirmwareVersionInFirestore(newFirmwareVersion);

            Serial.println("Finishing update...");
            if (Update.end())
            {
                Serial.println("Rebooting ESP32...");
                delay(3000);
                ESP.restart();
            }
            else
            {
                Serial.println("Update.end() failed!");
            }
        }
        else
        {
            Serial.println("OTA update failed: Incomplete write!");
        }
    }
    else
    {
        Serial.println("Failed to download firmware: HTTP Error " + String(httpCode));
    }

    http.end();
}

void checkForFirmwareUpdate()
{
    if (xSemaphoreTake(firebaseMutex, portMAX_DELAY))
    {
        String documentPath = systemPath;

        if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str()))
        {
            FirebaseJson json;
            json.setJsonData(fbdo.payload());
            FirebaseJsonData jsonData;

            // Fetch the version from Firestore
            float cloudVersion = firmware_version;
            if (json.get(jsonData, "fields/version/doubleValue"))
            {
                cloudVersion = jsonData.floatValue;
                Serial.print("Cloud Firmware Version: ");
                Serial.println(cloudVersion);
            }
            else
            {
                Serial.println("Failed to get firmware version from Firestore.");
            }

            // Fetch the latest version from Firebase Storage
            float storageVersion = fetchLatestVersion();
            Serial.print("Storage Firmware Version: ");
            Serial.println(storageVersion);

            // Compare versions
            if (storageVersion > cloudVersion)
            {
                Serial.println("New firmware available. Proceeding with update...");

                String firmwareUrl;
                firmwareUrl.reserve(200);
                firmwareUrl = "https://firebasestorage.googleapis.com/v0/b/";
                firmwareUrl += FIREBASE_PROJECT_ID;
                firmwareUrl += ".appspot.com/o/";
                firmwareUrl += serialNumber + "%2Ffirmware.bin?alt=media"; // %2F is URL-encoded "/"

                performOTAUpdate(firmwareUrl, storageVersion);
            }
            else
            {
                Serial.println("Firmware is up to date. No update needed.");
            }
        }
        else
        {
            Serial.println("Failed to check Firestore for firmware update.");
            Serial.println(fbdo.errorReason());
        }
        xSemaphoreGive(firebaseMutex);
    }
}

void sendHeartbeat()
{ if (xSemaphoreTake(firebaseMutex, portMAX_DELAY)) {
    String documentPath = systemPath;
    FirebaseJson content;
    content.set("fields/lastSeen/timestampValue", formatTimestamp());
    if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "lastSeen"))
    {
        Serial.println("Heartbeat sent.");
        Serial.println(formatTimestamp());
    }
    else
    {
        Serial.println("Failed to send heartbeat.");
        Serial.println(fbdo.errorReason());
    } xSemaphoreGive(firebaseMutex);
  }
}
void scanI2C()
{
    Serial.println("🔍 Scanning I2C bus...");
    byte count = 0;
    for (byte i = 1; i < 127; ++i)
    {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0)
        {
            Serial.print("✅ Found I2C device at 0x");
            Serial.println(i, HEX);
            count++;
        }
    }
    if (count == 0)
        Serial.println("❌ No I2C devices found!");
}
unsigned long lastRestartMillis = 0;                              // Track last restart time
const unsigned long DAILY_RESTART_INTERVAL = 2 * 60 * 60 * 1000; // 2 hours in milliseconds

void setup()
{
    // 🔍 Log last reset cause
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.print("🔁 Last reset reason: ");
    Serial.println(reason);
    Serial.begin(9600);
    randomSeed(analogRead(0));
    config.token_status_callback = tokenStatusCallback;
    // === Derive Static IP from serial number ===
    IPAddress staticIP = deriveStaticIP(serialNumber); // last 3 digits used
    IPAddress gateway(192, 168, 1, 1);                 // your router's IP
    IPAddress subnet(255, 255, 255, 0);                // standard /24 subnet
    IPAddress dns1(192, 168, 1, 254);                  // Assuming your router IP
                                                       // Google DNS
    IPAddress dns2(8, 8, 4, 4);                        // Optional backup DNS

    // Apply static IP configuration (MUST include DNS to avoid SSL issues)
    // WiFi.config(staticIP, gateway, subnet, dns1, dns2);

    // === Attempt to connect using WiFiManager ===
    wm.setConnectTimeout(20);
    wm.setConfigPortalTimeout(60);
    if (!wm.autoConnect(setupWifiName.c_str()))
    {
        Serial.println(" WiFiManager failed. Restarting...");
        delay(3000);
        ESP.restart();
    }
    Serial.println("WiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());
    startupTime = millis();
    lastRestartMillis = millis();

    // === Confirm network status ===
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("✅ WiFi connected.");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("DNS Server: ");
        Serial.println(WiFi.dnsIP());
    }
    else
    {
        Serial.println("WiFi not connected.");
    }
    initializeTime(); // NTP sync
    Serial.println("➡️ Next: Starting Firebase...");
    // === Initialize Firebase ===
    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // Wait for Firebase to be ready
    
    while (!Firebase.ready())
    {
        delay(100);
    }

    // === Sync data and setup system ===
    fetchFirebaseSystemData(&fbdo, &systemName, &lightOnTime, &lightOffTime, &lightMasterSwitch, &timeCycleEnabled, unitNames);
    fetchFirebaseUnitData(&fbdo, unitsEnabled, atomizerOnIntervals, atomizerOffIntervals, unitNames);
    updateSystemVersion();
    Serial.println("➡️ Setting up sensors...");
    // === Pin setup ===
    for (int i = 0; i < NUMBER_OF_UNITS; i++)
    {
        pinMode(waterLevelPins[i], INPUT_PULLUP);
        ledcSetup(i, PWM_FREQUENCY_ATOMIZER, PWM_RESOLUTION_ATOMIZER);
        ledcAttachPin(atomizerPins[i], i);
        Serial.printf("Attached atomizer pin %d to PWM channel %d\n", atomizerPins[i], i);
    }

    pinMode(SYSTEM_12V_POWER_PIN, OUTPUT);
    digitalWrite(SYSTEM_12V_POWER_PIN, HIGH);
    pinMode(SYSTEM_LIGHTS_PIN, OUTPUT);
    digitalWrite(SYSTEM_LIGHTS_PIN, LOW);

    for (int i = 0; i < NUMBER_OF_UNITS; i++)
    {
        Serial.print("Sensor Mode Unit ");
        Serial.print(i);
        Serial.print(": ");
        Serial.println(useCapacitiveSensor ? "Capacitive" : "Float");
    }


    // === mDNS Setup ===
    if (systemName != "")
    {
        if (!MDNS.begin(systemName.c_str()))
        {
            Serial.println("Error setting up mDNS.");
            delay(1000);
        }
    }

    // === OTA Setup ===
    ArduinoOTA.setHostname(systemName.c_str());
    ArduinoOTA.onStart([]()
                       {
        String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
        Serial.println("Start updating " + type); });
    ArduinoOTA.onEnd([]()
                     { Serial.println("\nUpdate Complete!"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                          { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
    ArduinoOTA.onError([](ota_error_t error)
                       {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed"); });

    ArduinoOTA.begin(); // ✅ Start the OTA server
    // === I2C Init (must come BEFORE any sensor init or scan) ===
    Wire.begin(SDA, SCL); // Initialize the I2C bus only ONCE
    mcp3021.init(&Wire);  // MCP3021 ADC init
    Serial.println("✅ Serial ready. Starting I2C init...");
    delay(200); // Give USB host time to open terminal

    // === Safe I2C Scan (optional but now safe) ===
    scanI2C(); // make sure it uses sensorMutex internally
    Serial.println("➡️ Setup done.");
    // === Sensor initialization AFTER I2C is ready ===
    // sensors::initSensors(); // Now it's safe to talk to sensors
}

void loop()
{
    ArduinoOTA.handle();
    updateUnits();
    checkWiFiFailsafe();
    // Log heap every 1 minute
    static unsigned long lastHeapLogTime = 0;
    if (millis() - lastHeapLogTime >= 60000)
    {
        Serial.print("[MEM] Free Heap: ");
        Serial.print(ESP.getFreeHeap());
        Serial.print(" | Min Heap: ");
        Serial.println(ESP.getMinFreeHeap());
        lastHeapLogTime = millis();
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        unsigned long currentMillis = millis();

        // // Check for daily restart
        // if (currentMillis - lastRestartMillis >= DAILY_RESTART_INTERVAL)
        // {
        //     Serial.println("Performing daily restart to clear memory...");
        //     delay(1000); // Give time for the message to be sent
        //     ESP.restart();
        // }

        if (currentMillis - previousHeartbeatMillis >= INTERVAL_30_SECONDS)
        {
            if (Firebase.ready())
            {
                // float currentEC = readECSensorValue();   // Read EC value
                // Serial.println(currentEC);
                // sendECValueToFirebase(&fbdo, currentEC); // Send EC value to Firebase
                fetchFirebaseSystemData(&fbdo, &systemName, &lightOnTime, &lightOffTime, &lightMasterSwitch, &timeCycleEnabled, unitNames);
                fetchFirebaseUnitData(&fbdo, unitsEnabled, atomizerOnIntervals, atomizerOffIntervals, unitNames);
                Serial.println(unitsEnabled[0]);
                sendHeartbeat();
                
                systemLights();
                previousHeartbeatMillis = currentMillis;
                if (ENABLE_I2C_SENSORS)
                {
                    readECSensorValue();
                    readWaterLevel(); // Read water level states
                    readSensors();
                    readCO2();
                }
                else
                {
                    Serial.println("🔒 I2C sensors disabled by flag. Skipping read.");
                }
               

                for (int i = 0; i < NUMBER_OF_UNITS; i++)
                {
                    Serial.print("Sensor Mode Unit ");
                    Serial.print(i);
                    Serial.print(": ");
                    Serial.println(useCapacitiveSensor ? "Capacitive" : "Float");
                }
            }
        }
    
 

        // 🔹 **Check for Firmware Update Every 6 Hour**
        if (currentMillis - lastFirmwareCheckMillis >= FIRMWARE_CHECK_INTERVAL)
        {
            Serial.println("Checking for firmware updates...");
            checkForFirmwareUpdate();
            lastFirmwareCheckMillis = currentMillis;
        }
    }
    else
    {
        Serial.println("Wi-Fi disconnected. Retrying...");
        unsigned long wifiTimeoutCheck = millis();
        unsigned long currentMillisWiFi;

        while (WiFi.status() != WL_CONNECTED)
        {
            currentMillisWiFi = millis();
            delay(1000); // Retry every second
            if (!wm.autoConnect(setupWifiName.c_str()))
            {
                Serial.println("Failed to configure WiFi. Restarting...");
                delay(3000);
                ESP.restart();
            }
        }

        Serial.println("Wi-Fi reconnected. Reinitializing Firebase...");
        config.api_key = API_KEY;
        auth.user.email = USER_EMAIL;
        auth.user.password = USER_PASSWORD;
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);

        initializeTime(); // Reinitialize time after reconnection
    }
}
