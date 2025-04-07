#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <time.h>
#include <credentials.h>
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
    tcaselect(0);
    FDC.configureMeasurementSingle(MEASURMENT, CHANNEL, capdac);
    FDC.triggerSingleMeasurement(MEASURMENT, FDC1004_100HZ);

    // wait for completion
    delay(100);
    uint16_t value[2];
    if (!FDC.readMeasurement(MEASURMENT, value))
    {
        int16_t msb = (int16_t)value[0];
        int32_t capacitance = ((int32_t)457) * ((int32_t)msb); // in attofarads
        capacitance /= 1000;                                   // in femtofarads
        capacitance += ((int32_t)3028) * ((int32_t)capdac);
        measuredCap = (float)capacitance / 1000; // in pF
        // Blynk.virtualWrite(V23, measuredCap);
        Serial.print((((float)capacitance / 1000)), 4);
        Serial.print("  pf, ");
        waterLevel = (measuredCap - 1.58) / 0.107;
        // Blynk.virtualWrite(V24, waterLevel);
        Serial.print(" |L = ");
        Serial.print(waterLevel);
        if (!unitNames[0].isEmpty())
        {
            sendUnitCapValueToFirebase(&fbdo, unitNames[0], measuredCap);
        }
        if (msb > UPPER_BOUND) // adjust capdac accordingly
        {
            if (capdac < FDC1004_CAPDAC_MAX)
                capdac++;
        }
        else if (msb < LOWER_BOUND)
        {
            if (capdac > 0)
                capdac--;
        }
    }

    tcaselect(2);
    FDC.configureMeasurementSingle(MEASURMENT, CHANNEL, capdac);
    FDC.triggerSingleMeasurement(MEASURMENT, FDC1004_100HZ);

    // wait for completion
    delay(100);
    if (!FDC.readMeasurement(MEASURMENT, value))
    {
        int16_t msb = (int16_t)value[0];
        int32_t capacitance = ((int32_t)457) * ((int32_t)msb); // in attofarads
        capacitance /= 1000;                                   // in femtofarads
        capacitance += ((int32_t)3028) * ((int32_t)capdac);
        measuredCap = (float)capacitance / 1000; // in pF
        // Blynk.virtualWrite(V23, measuredCap);
        Serial.print((((float)capacitance / 1000)), 4);
        Serial.print("  pf, ");
        waterLevel = (measuredCap - 1.58) / 0.107;
        // Blynk.virtualWrite(V24, waterLevel);
        Serial.print(" |L = ");
        Serial.print(waterLevel);
        if (!unitNames[1].isEmpty())
        {
            sendUnitCapValueToFirebase(&fbdo, unitNames[1], measuredCap);
        }
        if (msb > UPPER_BOUND) // adjust capdac accordingly
        {
            if (capdac < FDC1004_CAPDAC_MAX)
                capdac++;
        }
        else if (msb < LOWER_BOUND)
        {
            if (capdac > 0)
                capdac--;
        }
    }

    tcaselect(4);
    FDC.configureMeasurementSingle(MEASURMENT, CHANNEL, capdac);
    FDC.triggerSingleMeasurement(MEASURMENT, FDC1004_100HZ);

    // wait for completion
    delay(100);
    if (!FDC.readMeasurement(MEASURMENT, value))
    {
        int16_t msb = (int16_t)value[0];
        int32_t capacitance = ((int32_t)457) * ((int32_t)msb); // in attofarads
        capacitance /= 1000;                                   // in femtofarads
        capacitance += ((int32_t)3028) * ((int32_t)capdac);
        measuredCap = (float)capacitance / 1000; // in pF
        // Blynk.virtualWrite(V23, measuredCap);
        Serial.print((((float)capacitance / 1000)), 4);
        Serial.print("  pf, ");
        waterLevel = (measuredCap - 1.58) / 0.107;
        // Blynk.virtualWrite(V24, waterLevel);
        Serial.print(" |L = ");
        Serial.print(waterLevel);
        if (!unitNames[2].isEmpty())
        {
            sendUnitCapValueToFirebase(&fbdo, unitNames[2], measuredCap);
        }
        if (msb > UPPER_BOUND) // adjust capdac accordingly
        {
            if (capdac < FDC1004_CAPDAC_MAX)
                capdac++;
        }
        else if (msb < LOWER_BOUND)
        {
            if (capdac > 0)
                capdac--;
        }
    }
}

// Function to read EC sensor value from MCP3021 ADC
void readECSensorValue()
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
}

void updateUnits()
// A function that updates and sends atomizer signals and sends a command to update the water level state when atomizers are off
{
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
                    // readWaterLevel();
                    readECSensorValue();
                } // read the water level state only if the atomizers are off
                previousMillis[i] = currentMillis; // reset the time counter
            }
        }
    }
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
    HTTPClient http;
    String versionUrl = "https://firebasestorage.googleapis.com/v0/b/" + String(FIREBASE_PROJECT_ID) +
                        ".appspot.com/o/Version_" + serialNumber + ".txt?alt=media";

    http.begin(versionUrl);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        String versionString = http.getString(); // Read version from file
        http.end();
        return versionString.toFloat(); // Convert to float and return
    }
    else
    {
        Serial.println("Failed to fetch latest firmware version.");
        Serial.println(http.errorToString(httpCode));
        http.end();
        return firmware_version; // If failed, return current firmware version
    }
}
void updateFirmwareVersionInFirestore(float newVersion)
{
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
            firmwareUrl += ".appspot.com/o/firmware_";
            firmwareUrl += serialNumber;
            firmwareUrl += ".bin?alt=media";

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
}

void sendHeartbeat()
{
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
    }
}

unsigned long lastRestartMillis = 0;  // Track last restart time
const unsigned long DAILY_RESTART_INTERVAL = 24 * 60 * 60 * 1000;  // 24 hours in milliseconds

void setup()
{
    Serial.begin(9600);
    randomSeed(analogRead(0));
    randomDelay = random(100, 10000);
    connectionOffset = 1000 + randomDelay;
    delay(connectionOffset);

    // Attempt to connect to known Wi-Fi networks
    wm.setConnectTimeout(20);
    wm.setConfigPortalTimeout(60);
    if (!wm.autoConnect(setupWifiName.c_str()))
    {
        Serial.println("Failed to configure WiFi. Restarting...");
        delay(3000);
        ESP.restart();
    }
    // Initialize Firebase and other components
    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    // Other initialization code...
    initializeTime();
    while (!Firebase.ready())
    {
        delay(100);
    }
    fetchFirebaseSystemData(&fbdo, &systemName, &lightOnTime, &lightOffTime, &lightMasterSwitch, &timeCycleEnabled, unitNames);
    fetchFirebaseUnitData(&fbdo, unitsEnabled, atomizerOnIntervals, atomizerOffIntervals, unitNames);
    Serial.println(unitsEnabled[0]);
    updateSystemVersion();
    for (int i = 0; i < NUMBER_OF_UNITS; i++)
    {                                                                  // Loop across all of the pins
        pinMode(waterLevelPins[i], INPUT_PULLUP);                      // set the pinmode of the i-th water level pin to input and use an internal pullup resistor (because this is an electrical switch)
        ledcSetup(i, PWM_FREQUENCY_ATOMIZER, PWM_RESOLUTION_ATOMIZER); // Setup the PWM generator with the atomizer frequency and resolution on the i-th channel
        ledcAttachPin(atomizerPins[i], i);                             // Attach the i-th PWM generator channel with the i-th atomizer pin
    }
    pinMode(SYSTEM_12V_POWER_PIN, OUTPUT);    // set the 12V power enabler pin to Output
    digitalWrite(SYSTEM_12V_POWER_PIN, HIGH); // Turn on the 12V power
    pinMode(SYSTEM_LIGHTS_PIN, OUTPUT);       // Set the light pin to output
    digitalWrite(SYSTEM_LIGHTS_PIN, LOW);     // Make sure lights are off for now

    if (systemName != "")
    { // Set the hostname to the system name
        if (!MDNS.begin(systemName.c_str()))
        {
            Serial.println("Error setting up MDNS responder!");
            delay(1000);
        }
    }
    // Initialize OTA
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
    Wire.begin(SDA, SCL);
    mcp3021.init(&Wire);
}

void loop()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        unsigned long currentMillis = millis();

        // Check for daily restart
        if (currentMillis - lastRestartMillis >= DAILY_RESTART_INTERVAL)
        {
            Serial.println("Performing daily restart to clear memory...");
            delay(1000);  // Give time for the message to be sent
            ESP.restart();
        }

        if (currentMillis - previousHeartbeatMillis >= INTERVAL_30_SECONDS + connectionOffset)
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
                readECSensorValue(); // This already sends to Firebase
                readWaterLevel();
                previousHeartbeatMillis = currentMillis;
                ArduinoOTA.handle();
            }
        }

        if (currentMillis - lastConnectionCheckMillis >= connectionOffset)
        {
            if (Firebase.ready())
            {

                updateUnits();
                systemLights();
                lastConnectionCheckMillis = currentMillis;
            }
        }

        // 🔹 **Check for Firmware Update Every Hour**
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
