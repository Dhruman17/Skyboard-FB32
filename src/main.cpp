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

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

String systemPath;
String unitNames[3];
String systemName = ""; // Will be fetched from Firestore
time_t atomizerOnTime;
time_t atomizerOffTime;
bool systemLightSwitch = true; // Default to true for safety
bool systemLightTimeCycleSwitch = false;
unsigned long connectionOffset = 0; // Random delay for connection

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
    }
    else
    {
        Serial.println("Failed to send heartbeat.");
        Serial.println(fbdo.errorReason());
    }
}

// Function to send system notification
void sendSystemNotification(String unitName, String message)
{
    String documentPath = "Notifications";
    FirebaseJson content;
    content.set("fields/unitName/stringValue", unitName);
    content.set("fields/systemName/stringValue", systemName);
    content.set("fields/message/stringValue", message);
    content.set("fields/uid/stringValue", auth.token.uid);
    content.set("fields/timestamp/timestampValue", formatTimestamp());
    if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw()))
    {
        Serial.println("Notification sent: " + message);
    }
    else
    {
        Serial.println("Failed to send notification.");
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
            atomizerOnTime = parseTime(jsonData.stringValue.c_str());
        }
        else
        {
            Serial.println("Light_Interval_On_Time not found or not a timestamp");
        }
        // Fetch Light_Interval_Off_Time
        if (json.get(jsonData, "fields/Light_Interval_Off_Time/timestampValue"))
        {
            atomizerOffTime = parseTime(jsonData.stringValue.c_str());
        }
        else
        {
            Serial.println("Light_Interval_Off_Time not found or not a timestamp");
        }
        // Fetch Light_Master_Switch
        if (json.get(jsonData, "fields/Light_Master_Switch/booleanValue"))
        {
            systemLightSwitch = jsonData.boolValue;
        }
        else
        {
            Serial.println("Light_Master_Switch not found or not a boolean");
        }
        // Fetch Light_Time_Cycle_Switch
        if (json.get(jsonData, "fields/Light_Time_Cycle_Switch/booleanValue"))
        {
            systemLightTimeCycleSwitch = jsonData.boolValue;
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
    if (!systemLightSwitch)
    {
        if (digitalRead(SYSTEM_LIGHTS_PIN) == HIGH)
        {
            digitalWrite(SYSTEM_LIGHTS_PIN, LOW);
            sendSystemNotification("Light System", "Light System turned OFF due to Light Master Switch");
        }
        return;
    }

    struct tm currentTimeOfDay = *currentTime;
    currentTimeOfDay.tm_year = 70; // Epoch year
    currentTimeOfDay.tm_mon = 0;   // January
    currentTimeOfDay.tm_mday = 1;  // 1st of the month
    time_t currentTime_t = mktime(&currentTimeOfDay);
    if (systemLightTimeCycleSwitch)
    {
        if (atomizerOffTime < atomizerOnTime)
        {
            if (currentTime_t >= atomizerOnTime || currentTime_t <= atomizerOffTime)
            {
                if (digitalRead(SYSTEM_LIGHTS_PIN) == LOW)
                {
                    digitalWrite(SYSTEM_LIGHTS_PIN, HIGH);
                    sendSystemNotification("Light System", "Light System turned ON");
                }
            }
            else
            {
                if (digitalRead(SYSTEM_LIGHTS_PIN) == HIGH)
                {
                    digitalWrite(SYSTEM_LIGHTS_PIN, LOW);
                    sendSystemNotification("Light System", "Light System turned OFF");
                }
            }
        }
        else
        {
            if (currentTime_t >= atomizerOnTime && currentTime_t <= atomizerOffTime)
            {
                if (digitalRead(SYSTEM_LIGHTS_PIN) == LOW)
                {
                    digitalWrite(SYSTEM_LIGHTS_PIN, HIGH);
                    sendSystemNotification("Light System", "Light System turned ON");
                }
            }
            else
            {
                if (digitalRead(SYSTEM_LIGHTS_PIN) == HIGH)
                {
                    digitalWrite(SYSTEM_LIGHTS_PIN, LOW);
                    sendSystemNotification("Light System", "Light System turned OFF");
                }
            }
        }
    }
    else
    {
        if (digitalRead(SYSTEM_LIGHTS_PIN) == LOW)
        {
            digitalWrite(SYSTEM_LIGHTS_PIN, HIGH);
            sendSystemNotification("Light System", "Light System turned ON");
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
                        sendSystemNotification(unitNames[i], "Unit state changed: ON");
                    }
                    else
                    {
                        Serial.println("Turning off LED for " + String(unitNames[i]));
                        ledcWrite(i, 0);
                        sendSystemNotification(unitNames[i], "Unit state changed: OFF");
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
                if (!atomStates[i])
                {
                    updateWaterLevelStates(i);
                }                                  // read the water level state only if the atomizers are off
                if (atomStates[i] == false) // If the i-th atomizer is off,
                {
                    updateWaterLevelStates(i);
                } // read the water level state only if the atomizers are off
                previousMillis[i] = currentMillis; // reset the time counter
            }
        }
    }
}
bool connectToKnownWiFi()
{
    for (int i = 0; i < knownWiFiCount; i++)
    {
        Serial.print("Attempting to connect to ");
        Serial.println(knownWiFi[i].ssid);
        WiFi.begin(knownWiFi[i].ssid, knownWiFi[i].password);

        // Wait for connection (up to 10 seconds)
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20)
        {
            delay(500); // 500ms delay per attempt
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("\nConnected to " + String(knownWiFi[i].ssid));
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            return true;
        }

        Serial.println("\nFailed to connect to " + String(knownWiFi[i].ssid));
    }
    return false; // Return false if no network connects
}
void setup()
{
    Serial.begin(9600);

    // Generate the random delays
    unsigned long randomDelay = random(100, 1000);
    connectionOffset = 500 + randomDelay;
    delay(connectionOffset);

    // Attempt to connect to known Wi-Fi networks
    if (!connectToKnownWiFi())
    {
        Serial.println("No known networks available. Starting WiFiManager...");

        // Initialize WiFiManager
        WiFiManager wm;

        // Start the configuration portal
        bool configPortalStarted = wm.startConfigPortal("ESP32-Config", "password");

        if (configPortalStarted)
        {
            Serial.println("WiFi configuration successful!");
            Serial.print("Connected to: ");
            Serial.println(WiFi.SSID());
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
        }
        else
        {
            Serial.println("Failed to configure WiFi. Restarting...");
            delay(3000);
            ESP.restart();
        }
    }

    // Initialize OTA
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
    // Initialize Firebase and other components
    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    // Other initialization code...
    initializeTime();
    fetchSystemName();

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
}

void loop()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        unsigned long currentMillis = millis();

        // Handle OTA
        ArduinoOTA.handle();

        if (currentMillis - previousHeartbeatMillis >= INTERVAL_30_SECONDS)
        {
            sendHeartbeat();

            previousHeartbeatMillis = currentMillis;
        }

        // Use the connection offset for other functions, including systemLights
        if (currentMillis - lastConnectionCheckMillis >= connectionOffset)
        {
            fetchAtomizerIntervals();
            readFirestoreConfig();
            updateUnits();
            systemLights();
            lastConnectionCheckMillis = currentMillis;
        }
    }
    else
    {
        Serial.println("Wi-Fi disconnected, trying to reconnect...");
        WiFiManager wm;
        wm.autoConnect("ESP32-Config", "password"); // Reconnect using WiFiManager
    }
}