#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <firebaseFormatting.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <time.h>
#include <credentials.h>
#include <config.h>
#include <WiFiManager.h> // WiFiManager by Tzapu
#include <ArduinoOTA.h>  // OTA functionality
#include <ESPmDNS.h>
const String firmware_version = 1.1

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

String systemPath;
String unitNames[NUMBER_OF_UNITS]; // Storing the names of units with suffixes 
String systemName = ""; // Will be fetched from Firestore
time_t lightOnTime;
time_t lightOffTime;
time_t atomizerOffTime;
bool lightMasterSwitch = false; // The master light switch from Firebase
bool timeCycleEnabled = false;
bool lightState = false;

// Generate the random delays
int randomDelay = random(100, 10000);
int connectionOffset = 1000 + randomDelay;

// Function to initialize NTP
void initializeTime()
{
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void fetchSystemName()
{
    String documentPath = "Systems/" + serialNumber;
    Serial.println(documentPath);
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str()))
    {
        FirebaseJson json;
        json.setJsonData(fbdo.payload());
        FirebaseJsonData jsonData;

        if (json.get(jsonData, "fields/systeName/stringValue"))
        {
            systemName = jsonData.stringValue;
            systemPath = "Systems/" + serialNumber;
            unitNames[0] = systemName + "-1";
            unitNames[1] = systemName + "-2";
            unitNames[2] = systemName + "-3";
            Serial.println("System Name: " + systemName);
        }
        else
        {
            Serial.println("System name not found.");
        }
    }
    else
    {
        Serial.println("Failed to fetch serial number or system name.");
        Serial.println(fbdo.errorReason());
    }
}

// Function to send heartbeat signal
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

// Function to fetch light intervals and switches
void fetchAtomizerIntervals()
{
    String documentPath = systemPath;
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str()))
    {
        FirebaseJson json;
        json.setJsonData(fbdo.payload());
        FirebaseJsonData jsonData;
        // Fetch Light_Interval_On_Time
        if (json.get(jsonData, "fields/Light_Interval_On_Time/timestampValue"))
        {
            lightOnTime = parseTime(jsonData.stringValue.c_str());
            Serial.println(jsonData.stringValue.c_str());
        }
        else
        {
            Serial.println("Light_Interval_On_Time not found or not a timestamp");
        }
        // Fetch Light_Interval_Off_Time
        if (json.get(jsonData, "fields/Light_Interval_Off_Time/timestampValue"))
        {
            lightOffTime = parseTime(jsonData.stringValue.c_str());
            Serial.println(jsonData.stringValue.c_str());
        }
        else
        {
            Serial.println("Light_Interval_Off_Time not found or not a timestamp");
        }
        // Fetch Light_Master_Switch
        if (json.get(jsonData, "fields/Light_Master_Switch/booleanValue"))
        {
            lightMasterSwitch = jsonData.boolValue;
        }
        else
        {
            Serial.println("Light_Master_Switch not found or not a boolean");
        }
        // Fetch Light_Time_Cycle_Switch
        if (json.get(jsonData, "fields/Light_Time_Cycle_Switch/booleanValue"))
        {
            timeCycleEnabled = jsonData.boolValue;
        }
        else
        {
            Serial.println("Light_Time_Cycle_Switch not found or not a boolean");
        }
    }
    else
    {
        Serial.println("Failed to fetch light intervals.");
        Serial.println(fbdo.errorReason());
    }
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
                if(!lightState){ // if the lights are off, turn them on
                    digitalWrite(SYSTEM_LIGHTS_PIN, HIGH);
                    lightState = true;
                }
            }
            else{
                // if the current time is before the on time or the current time is after the off time
                if(lightState){
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
                if(!lightState){
                    digitalWrite(SYSTEM_LIGHTS_PIN, HIGH);
                    lightState = true;
                }
            }
            else
            {
                if(lightState){
                    digitalWrite(SYSTEM_LIGHTS_PIN, LOW);
                    lightState = false;
                }
            }
        }
    }
    else{
        if(lightState != lightMasterSwitch){
            if(lightMasterSwitch){
                digitalWrite(SYSTEM_LIGHTS_PIN, HIGH);
                lightState = true;
            }
        else{
            digitalWrite(SYSTEM_LIGHTS_PIN, LOW);
            lightState = false;
        }
    }
    }
}

// Function to read water level sensors and update states
void updateWaterLevelStates(int i)
{
    if (digitalRead(waterLevelPins[i]) == LOW)
    {
        waterLevelStates[i] = false;
    }
    else
    {
        waterLevelStates[i] = true;
    }
    if (waterLevelStates[i] != previousWaterLevelStates[i])
    {
        String documentPath = systemPath + "/units/" + unitNames[i];
        FirebaseJson content;
        content.set("fields/waterLevelState/booleanValue", waterLevelStates[i]);
        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "waterLevelState"))
        {
            Serial.println("Updated water level state for " + unitNames[i]);
            previousWaterLevelStates[i] = waterLevelStates[i];
        }
        else
        {
            Serial.println("Failed to update water level state for " + unitNames[i]);
        }
    }
}

// Function to read system config from Firestore
void readFirestoreConfig()
{
    for (int i = 0; i < 3; i++)
    {
        String documentPath = systemPath + "/units/" + unitNames[i];
        if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str()))
        {
            FirebaseJson json;
            json.setJsonData(fbdo.payload());
            FirebaseJsonData jsonData;
            if (json.get(jsonData, "fields/unitState/booleanValue"))
            {
                bool newState = jsonData.boolValue;
                if (unitsEnabled[i] != newState)
                {
                    unitsEnabled[i] = newState;
                    if (unitsEnabled[i])
                    {
                        Serial.println("Turning on LED for " + String(unitNames[i]));
                        ledcWrite(i, 9);
                    }
                    else
                    {
                        Serial.println("Turning off LED for " + String(unitNames[i]));
                        ledcWrite(i, 0);
                    }
                }
            }
            if (json.get(jsonData, "fields/Interval_On/integerValue"))
            {
                atomizerOnIntervals[i] = jsonData.intValue * 1000;
            }
            if (json.get(jsonData, "fields/Interval_Off/integerValue"))
            {
                atomizerOffIntervals[i] = jsonData.intValue * 1000;
            }
        }
        else
        {
            Serial.println("Failed to get document for " + String(unitNames[i]));
            Serial.println(fbdo.errorReason());
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
                if (atomStates[i] == false) // If the i-th atomizer is off,
                {
                    updateWaterLevelStates(i);
                } // read the water level state only if the atomizers are off
                previousMillis[i] = currentMillis; // reset the time counter
            }
        }
    }
}
bool connectToWiFi()
{
    String hostname = "SA" + serialNumber;
    WiFi.setHostname(hostname.c_str());
    WiFi.mode(WIFI_AP_STA); // Enable both AP and STA modes

    WiFiManager wm;
    //wm.resetSettings(); //---------------------------------------------------------------------- WiFi Credentials Erase
    wm.setConfigPortalTimeout(180); // Keep portal active for 3 minutes
    wm.startWebPortal();           // Start web server for manual configuration

    Serial.println("Starting Wi-Fi connection process...");

    // Try connecting to known networks
    for (int i = 0; i < knownWiFiCount; i++)
    {
        Serial.print("Attempting to connect to ");
        Serial.println(knownWiFi[i].ssid);

        WiFi.begin(knownWiFi[i].ssid, knownWiFi[i].password);

        // Wait for connection
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000)
        {
            wm.process(); // Allow Wi-Fi Manager to handle requests
            delay(100);   // Short delay for responsiveness
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("\nConnected to Wi-Fi: " + String(knownWiFi[i].ssid));
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            wm.stopWebPortal(); // Stop web portal
            return true;
        }
    }

    // If no connection, allow manual configuration via AP
    Serial.println("Switching to AP mode for manual configuration...");
    String wifi_SSID = "SkyAcres WiFi Setup " + serialNumber;
    if (!wm.startConfigPortal(wifi_SSID.c_str(), "password"))
    {
        return false;
    }

    // If manual configuration succeeds
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("Wi-Fi configured via AP mode.");
        return true;
    }
    return false;
}
void updateSystemVersion()
{
    if (systemPath != "")
    {
        String documentPath = systemPath; // Use the system path
        FirebaseJson content;
        content.set("fields/version/stringValue", firmware_version);

        if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "version"))
        {
            Serial.println("System version updated successfully in Firestore.");
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

void setup()
{
    Serial.begin(9600);
    delay(connectionOffset);
    if(!connectToWiFi()){
        Serial.println("Wi-Fi setup failed.");
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
    fetchSystemName();
    // Store version in Firestore
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

    if (systemName != "") {   // Set the hostname to the system name
        if (!MDNS.begin(systemName.c_str())){
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
    ArduinoOTA.begin();
}

void loop()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        // Handle OTA

        unsigned long currentMillis = millis();

        if (currentMillis - previousHeartbeatMillis >= INTERVAL_30_SECONDS + connectionOffset)
        {
            if(Firebase.ready()){
                sendHeartbeat();
                previousHeartbeatMillis = currentMillis;
                ArduinoOTA.handle();
            }
        }

        if (currentMillis - lastConnectionCheckMillis >= connectionOffset)
        {
            if(Firebase.ready()){
                fetchAtomizerIntervals();
                readFirestoreConfig();
                updateUnits();
                systemLights();
                lastConnectionCheckMillis = currentMillis;
            }
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
            connectToWiFi();
            delay(1000); // Retry every second
            if(currentMillisWiFi - wifiTimeoutCheck >= WIFI_RESET_INTERVAL){
                esp_restart();
            }
        }

        Serial.println("Wi-Fi reconnected. Reinitializing Firebase...");
        // Reinitialize Firebase after reconnection
        config.api_key = API_KEY;
        auth.user.email = USER_EMAIL;
        auth.user.password = USER_PASSWORD;
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);

        initializeTime(); // Reinitialize time after reconnection
    }
}

