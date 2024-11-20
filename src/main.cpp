#include <config.h>
#include <credentials.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <time.h>

// Firebase and system global variables
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// System Variables
String serialNumber = "1234567890";  // Unique serial number
String systemPath;
String units[3];
String systemName = "";

// Light control variables
time_t atomizerOnTime;  // Initialize to 0 or a default value
time_t atomizerOffTime; // Initialize to 0 or a default value
bool lightMasterSwitch = true;
bool lightTimeCycleSwitch = false;

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
void fetchSerialNumberAndSystemName() {
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
void fetchsystemLightIntervals() {
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
            lightMasterSwitch = jsonData.boolValue;
        } else {
            Serial.println("Light_Master_Switch not found or not a boolean");
        }
        // Fetch Light_Time_Cycle_Switch
        if (json.get(jsonData, "fields/Light_Time_Cycle_Switch/booleanValue")) {
            lightTimeCycleSwitch = jsonData.boolValue;
        } else {
            Serial.println("Light_Time_Cycle_Switch not found or not a boolean");
        }
    } else {
        Serial.println("Failed to fetch light intervals.");
        Serial.println(fbdo.errorReason());
    }
}

// Modify controlGpio26LED to consider Light Master Switch and Time Cycle Switch
void systemLights() {
    time_t now;
    struct tm* currentTime;
    time(&now);
    currentTime = localtime(&now);
    if (!lightMasterSwitch) {
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
    if (lightTimeCycleSwitch) {
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
void controlAtomizers() {
    unsigned long previousMillis[3] = {0, 0, 0};
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
    // Random delay between 50 and 10,000 milliseconds
    randomSeed(analogRead(0)); // Use an analog pin to generate randomness
    unsigned long randomDelay = random(50, 10001); // Generate a random number between 50 and 10,000
    Serial.print("Random delay before Wi-Fi initialization: ");
    Serial.println(randomDelay);
    delay(randomDelay); // remove the delay function
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("Connected to Wi-Fi");

    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    initializeTime();
    
    // Fetch serial number and system details
    fetchSerialNumberAndSystemName();

    pinMode(WATER_LEVEL_PIN_25, INPUT);
    pinMode(WATER_LEVEL_PIN_23, INPUT);
    pinMode(WATER_LEVEL_PIN_13, INPUT);
    pinMode(POWER_12V_PIN_12, OUTPUT);
    digitalWrite(POWER_12V_PIN_12, HIGH);
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
    static unsigned long previousHeartbeatMillis = 0;
    static unsigned long lastConnectionCheckMillis = 0;
    unsigned long currentMillis = millis();
     // Check Wi-Fi and Firebase connection
    if (WiFi.status() != WL_CONNECTED || !Firebase.ready()) {
        if (currentMillis - lastConnectionCheckMillis >= 21600000) { // 6 hours in milliseconds
            Serial.println("System disconnected for 6 hours. Restarting...");
            ESP.restart(); // Restart the system
        }
    } else {
        // Reset the connection check timer if the system is connected
        lastConnectionCheckMillis = currentMillis;
    }
     if (currentMillis - previousHeartbeatMillis >= HEARTBEAT_INTERVAL) {
        sendHeartbeat();
        systemLights();
        updateWaterLevelStates();
        previousHeartbeatMillis = currentMillis;
    }
   
    fetchsystemLightIntervals();
    readFirestoreConfig();
    controlAtomizers(); 
        // Check if it's time to restart

}