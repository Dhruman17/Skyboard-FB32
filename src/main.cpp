#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <time.h>
#include <credentials.h>
#include <config.h>
#include <WiFiManager.h>  // WiFiManager by Tzapu
#include <ArduinoOTA.h>   // OTA functionality


FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;


String systemPath;
String units[3];
String systemName = ""; // Will be fetched from Firestore
time_t atomizerOnTime;
time_t atomizerOffTime;
bool systemLightSwitch = true; // Default to true for safety
bool systemLightTimeCycleSwitch = false;

// Function to initialize NTP
void initializeTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

// Function to parse time from Firebase timestamp string
time_t parseTime(const char* timestamp) {
    struct tm tm;
    strptime(timestamp, "%Y-%m-%dT%H:%M:%S", &tm);
    tm.tm_year = 70; // Epoch year
    tm.tm_mon = 0;   // January
    tm.tm_mday = 1;  // 1st of the month
    return mktime(&tm);
}
// Function to format timestamp correctly for Firebase
String formatTimestamp() {
    time_t now;
    struct tm* tm_info;
    char buffer[30];
    time(&now);
    tm_info = localtime(&now);
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
    return String(buffer) + "Z";
}

// Function to fetch serial number and system name
void fetchSystemName() {
    String documentPath = "Systems/" + serialNumber;
    Serial.println(documentPath);
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())) {
        FirebaseJson json;
        json.setJsonData(fbdo.payload());
        FirebaseJsonData jsonData;
        
        if (json.get(jsonData, "fields/systeName/stringValue")) {
            systemName = jsonData.stringValue;
            systemPath = "Systems/" + serialNumber;
            units[0] = systemName + "-1";
            units[1] = systemName + "-2";
            units[2] = systemName + "-3";
            Serial.println("System Name: " + systemName);
        } else {
            Serial.println("System name not found.");
        }
    } else {
        Serial.println("Failed to fetch serial number or system name.");
        Serial.println(fbdo.errorReason());
    }
}

// Function to send heartbeat signal
void sendHeartbeat() {
    String documentPath = systemPath;
    FirebaseJson content;
    content.set("fields/lastSeen/timestampValue", formatTimestamp());
    if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "lastSeen")) {
        Serial.println("Heartbeat sent.");
    } else {
        Serial.println("Failed to send heartbeat.");
        Serial.println(fbdo.errorReason());
    }
}

// Function to send system notification
void sendSystemNotification(String unitName, String message) {
    String documentPath = "Notifications";
    FirebaseJson content;
    content.set("fields/unitName/stringValue", unitName);
    content.set("fields/systemName/stringValue", systemName);
    content.set("fields/message/stringValue", message);
    content.set("fields/uid/stringValue", auth.token.uid);
    content.set("fields/timestamp/timestampValue", formatTimestamp());
    if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw())) {
        Serial.println("Notification sent: " + message);
    } else {
        Serial.println("Failed to send notification.");
        Serial.println(fbdo.errorReason());
    }
}
// Function to fetch system name from Firestore
void fetchSystemName() {
    String documentPath = systemPath;
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())) {
        FirebaseJson json;                                                                          
        json.setJsonData(fbdo.payload());
        FirebaseJsonData jsonData;
        if (json.get(jsonData, "fields/systeName/stringValue")) {
            systemName = jsonData.stringValue;
            Serial.println("System Name: " + systemName);
        } else {
            Serial.println("SystemName not found or not a string");
        }
    } else {
        Serial.println("Failed to fetch system name.");
        Serial.println(fbdo.errorReason());
    }
}

// Function to fetch light intervals and switches
void fetchAtomizerIntervals() {
    String documentPath = systemPath;
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())) {
        FirebaseJson json;
        json.setJsonData(fbdo.payload());
        FirebaseJsonData jsonData;
        // Fetch Light_Interval_On_Time
        if (json.get(jsonData, "fields/Light_Interval_On_Time/timestampValue")) {
            atomizerOnTime = parseTime(jsonData.stringValue.c_str());
        } else {
            Serial.println("Light_Interval_On_Time not found or not a timestamp");
        }
        // Fetch Light_Interval_Off_Time
        if (json.get(jsonData, "fields/Light_Interval_Off_Time/timestampValue")) {
            atomizerOffTime = parseTime(jsonData.stringValue.c_str());
        } else {
            Serial.println("Light_Interval_Off_Time not found or not a timestamp");
        }
        // Fetch Light_Master_Switch
        if (json.get(jsonData, "fields/Light_Master_Switch/booleanValue")) {
            systemLightSwitch = jsonData.boolValue;
        } else {
            Serial.println("Light_Master_Switch not found or not a boolean");
        }
        // Fetch Light_Time_Cycle_Switch
        if (json.get(jsonData, "fields/Light_Time_Cycle_Switch/booleanValue")) {
            systemLightTimeCycleSwitch = jsonData.boolValue;
        } else {
            Serial.println("Light_Time_Cycle_Switch not found or not a boolean");
        }
    } else {
        Serial.println("Failed to fetch light intervals.");
        Serial.println(fbdo.errorReason());
    }
}

// Modify systemLights to consider Light Master Switch and Time Cycle Switch
void systemLights() {
    time_t now;
    struct tm* currentTime;
    time(&now);
    currentTime = localtime(&now);
    if (!systemLightSwitch) {
        if (digitalRead(SYSTEM_LIGHTS_PIN_26) == HIGH) {
            digitalWrite(SYSTEM_LIGHTS_PIN_26, LOW);
            sendSystemNotification("Light System", "Light System turned OFF due to Light Master Switch");
        }
        return;
    }

    struct tm currentTimeOfDay = *currentTime;
    currentTimeOfDay.tm_year = 70;  // Epoch year
    currentTimeOfDay.tm_mon = 0;    // January
    currentTimeOfDay.tm_mday = 1;   // 1st of the month
    time_t currentTime_t = mktime(&currentTimeOfDay);
    if (systemLightTimeCycleSwitch) {
        if (atomizerOffTime < atomizerOnTime) {
            if (currentTime_t >= atomizerOnTime || currentTime_t <= atomizerOffTime) {
                if (digitalRead(SYSTEM_LIGHTS_PIN_26) == LOW) {
                    digitalWrite(SYSTEM_LIGHTS_PIN_26, HIGH); 
                    sendSystemNotification("Light System", "Light System turned ON");
                }
            } else {
                if (digitalRead(SYSTEM_LIGHTS_PIN_26) == HIGH) {
                    digitalWrite(SYSTEM_LIGHTS_PIN_26, LOW); 
                    sendSystemNotification("Light System", "Light System turned OFF");
                }
            }
        } else {
            if (currentTime_t >= atomizerOnTime && currentTime_t <= atomizerOffTime) {
                if (digitalRead(SYSTEM_LIGHTS_PIN_26) == LOW) {
                    digitalWrite(SYSTEM_LIGHTS_PIN_26, HIGH);
                    sendSystemNotification("Light System", "Light System turned ON");
                }
            } else {
                if (digitalRead(SYSTEM_LIGHTS_PIN_26) == HIGH) {
                    digitalWrite(SYSTEM_LIGHTS_PIN_26, LOW); 
                    sendSystemNotification("Light System", "Light System turned OFF");
                }
            }
        }
    } else {
        if (digitalRead(SYSTEM_LIGHTS_PIN_26) == LOW) {
            digitalWrite(SYSTEM_LIGHTS_PIN_26, HIGH);
            sendSystemNotification("Light System", "Light System turned ON");
        }
    }
}

// Function to read water level sensors and update states
void updateWaterLevelStates() {
    if (digitalRead(WATER_LEVEL_PIN_25) == LOW) {
        waterLevelStates[0] = false;
    } else {
        waterLevelStates[0] = true;
    }
    if (digitalRead(WATER_LEVEL_PIN_23) == LOW) {
        waterLevelStates[1] = false;
    } else {
        waterLevelStates[1] = true;
    }
    if (digitalRead(WATER_LEVEL_PIN_13) == LOW) {
        waterLevelStates[2] = false;
    } else {
        waterLevelStates[2] = true;
    }
    for (int i = 0; i < 3; i++) {
        if (waterLevelStates[i] != previousUnitStates[i]) {
            String documentPath = systemPath + "/units/" + units[i];
            FirebaseJson content;
            content.set("fields/waterLevelState/booleanValue", waterLevelStates[i]);
            if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(),"waterLevelState")) {
                Serial.println("Updated water level state for " + units[i]);
                previousUnitStates[i] = waterLevelStates[i];
            } else {
                Serial.println("Failed to update water level state for " + units[i]);
            }
        }
    }
}

// Function to read system config from Firestore
void readFirestoreConfig() {
    for (int i = 0; i < 3; i++) {
        String documentPath = systemPath + "/units/" + units[i];
        if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())) {
            FirebaseJson json;
            json.setJsonData(fbdo.payload());
            FirebaseJsonData jsonData;
            if (json.get(jsonData, "fields/unitState/booleanValue")) {
                bool newState = jsonData.boolValue;
                if (unitStates[i] != newState) {
                    unitStates[i] = newState;
                    if (unitStates[i]) {
                        Serial.println("Turning on LED for " + String(units[i]));
                        ledcWrite(i, 9);
                        sendSystemNotification(units[i], "Unit state changed: ON");
                    } else {
                        Serial.println("Turning off LED for " + String(units[i]));
                        ledcWrite(i, 0);
                        sendSystemNotification(units[i], "Unit state changed: OFF");
                    }
                }
            }
            if (json.get(jsonData, "fields/Interval_On/integerValue")) {
                intervalOn[i] = jsonData.intValue * 1000;
            }
            if (json.get(jsonData, "fields/Interval_Off/integerValue")) {
                intervalOff[i] = jsonData.intValue * 1000;
            }
        } else {
            Serial.println("Failed to get document for " + String(units[i]));
            Serial.println(fbdo.errorReason());
        }
    }
}

// Function to control the LEDs based on unit states
void controlatomizers() {
    unsigned long currentMillis = millis();
    for (int i = 0; i < 3; i++) {
        if (unitStates[i]) {
            if (currentMillis - previousMillis[i] >= (ledStates[i] ? intervalOn[i] : intervalOff[i])) {
                ledStates[i] = !ledStates[i];
                ledcWrite(i, ledStates[i] ? 9 : 0);
                previousMillis[i] = currentMillis;
            }
        }
    }
}

void setup() {
    Serial.begin(9600);
    // Seed the random generator using analog pin noise or millis()
    randomSeed(analogRead(0) + millis());

    // Introduce a random delay (up to 10 seconds)
    randomDelay = random(1000, 10000);
    Serial.print("Random delay in setup: ");
    Serial.println(randomDelay);
    delay(randomDelay);

    // Initialize WiFiManager
    WiFiManager wm;

    // Start the configuration portal
    bool configPortalStarted = wm.startConfigPortal("ESP32-Config", "password");

    if (configPortalStarted) {
        Serial.println("WiFi configuration successful!");
        Serial.print("Connected to: ");
        Serial.println(WiFi.SSID());
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Failed to configure WiFi. Restarting...");
        delay(3000);
        ESP.restart();
    }

    // Initialize OTA
    ArduinoOTA.onStart([]() {
        String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nUpdate Complete!");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();

    // Initialize Firebase and other components
    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
       Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    // Initialize time
    initializeTime();
    
    // Fetch serial number and system details
    fetchSystemName();

    // Setup pin modes and initialize system components as before
    pinMode(WATER_LEVEL_PIN_25, INPUT);
    pinMode(WATER_LEVEL_PIN_23, INPUT);
    pinMode(WATER_LEVEL_PIN_13, INPUT);
    pinMode(SYSTEM_POWER_PIN_12, OUTPUT);
    digitalWrite(SYSTEM_POWER_PIN_12, HIGH);
    pinMode(SYSTEM_LIGHTS_PIN_26, OUTPUT);
    digitalWrite(SYSTEM_LIGHTS_PIN_26, LOW); 

    ledcSetup(ATOMIZER_PWM_CHANNEL_1, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(ATOMIZER_PIN_5, ATOMIZER_PWM_CHANNEL_1);
    ledcSetup(ATOMIZER_PWM_CHANNEL_0, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(ATOMIZER_PIN_4, ATOMIZER_PWM_CHANNEL_0);
    ledcSetup(ATOMIZER_PWM_CHANNEL_2, PWM_FREQUENCY, PWM_RESOLUTION);
    ledcAttachPin(ATOMIZER_PIN_2, ATOMIZER_PWM_CHANNEL_2);
}


void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();  // OTA update handling
      // Introduce a random delay between operations to avoid network overload
        if (millis() - lastConnectionCheckMillis > random(5000, 15000)) {
            Serial.println("Adding random delay to prevent overload.");
            delay(random(1000, 5000));
            lastConnectionCheckMillis = millis();
        }

        // Rest of your existing logic
        unsigned long currentMillis = millis();
        if (currentMillis - previousHeartbeatMillis >= HEARTBEAT_INTERVAL) {
            sendHeartbeat();
            previousHeartbeatMillis = currentMillis;
        }

        systemLights();
        updateWaterLevelStates();
        fetchAtomizerIntervals();
        readFirestoreConfig();
        controlatomizers();
    } else {
        Serial.println("Wi-Fi disconnected, trying to reconnect...");
        WiFiManager wm;
        wm.autoConnect("ESP32-Config", "password");  // Reconnect using WiFiManager
    }
}
