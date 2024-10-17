#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <time.h>

#define WIFI_SSID "POTANU VAPAR MAFTYA"
#define WIFI_PASSWORD "Rogers@2433"
#define API_KEY "AIzaSyDfp9KFIxgs9Wb0AiJTENejm1GLjS2MCQI"
#define FIREBASE_PROJECT_ID "skyacres-marketplace"
#define USER_EMAIL "test@gmail.com"
#define USER_PASSWORD "test123"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;


const unsigned long heartbeatInterval = 30000; // 30 seconds for heartbeat
const unsigned long connectionCheckInterval = 10000; // 10 seconds in milliseconds
unsigned long previousHeartbeatMillis = 0;
unsigned long lastConnectionCheckMillis = 0;
unsigned long previousMillis[3] = {0, 0, 0};
unsigned long lastNotificationMillis = 0;

const int redLEDPin = 5;
const int greenLEDPin = 4;
const int blueLEDPin = 2;
const int waterLevelPin25 = 25; // Float sensor for Unit1
const int waterLevelPin23 = 23; // Float sensor for Unit2
const int waterLevelPin13 = 13; // Float sensor for Unit3
const int gpio26LEDPin = 26; // GPIO 26 for new LED
const int power12V = 12;
const int redLEDPwmChannel = 1;
const int greenLEDPwmChannel = 0;
const int blueLEDPwmChannel = 2;
const int gpio26LEDPwmChannel = 3; // PWM channel for Light System
const int pwmFrequency = 108000;
const int pwmResolution = 4; // 8-bit resolution

String systemPath = "Systems/THQ";
String units[] = {"THQ-1", "THQ-2", "THQ-3"};
String systemName = "THQ";

bool unitStates[3] = {false, false, false}; 
bool previousUnitStates[3] = {false, false, false};
bool waterLevelStates[3] = {false, false, false};
bool previousWaterLevelStates[3] = {false, false, false};
long intervalOn[3] = {5000, 5000, 5000};
long intervalOff[3] = {5000, 5000, 5000};
bool ledStates[3] = {false, false, false};

time_t lightOnTime;
time_t lightOffTime;
bool lightMasterSwitch = true; // Default to true for safety
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
        if (json.get(jsonData, "fields/systemName/stringValue")) {
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
void fetchLightIntervals() {
    String documentPath = systemPath;
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())) {
        FirebaseJson json;
        json.setJsonData(fbdo.payload());
        FirebaseJsonData jsonData;
        // Fetch Light_Interval_On_Time
        if (json.get(jsonData, "fields/Light_Interval_On_Time/timestampValue")) {
            lightOnTime = parseTime(jsonData.stringValue.c_str());
        } else {
            Serial.println("Light_Interval_On_Time not found or not a timestamp");
        }
        // Fetch Light_Interval_Off_Time
        if (json.get(jsonData, "fields/Light_Interval_Off_Time/timestampValue")) {
            lightOffTime = parseTime(jsonData.stringValue.c_str());
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
void controlGpio26LED() {
    time_t now;
    struct tm* currentTime;
    time(&now);
    currentTime = localtime(&now);
    if (!lightMasterSwitch) {
        if (digitalRead(gpio26LEDPin) == HIGH) {
            digitalWrite(gpio26LEDPin, LOW);
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
        if (lightOffTime < lightOnTime) {
            if (currentTime_t >= lightOnTime || currentTime_t <= lightOffTime) {
                if (digitalRead(gpio26LEDPin) == LOW) {
                    digitalWrite(gpio26LEDPin, HIGH); 
                    sendSystemNotification("Light System", "Light System turned ON");
                }
            } else {
                if (digitalRead(gpio26LEDPin) == HIGH) {
                    digitalWrite(gpio26LEDPin, LOW); 
                    sendSystemNotification("Light System", "Light System turned OFF");
                }
            }
        } else {
            if (currentTime_t >= lightOnTime && currentTime_t <= lightOffTime) {
                if (digitalRead(gpio26LEDPin) == LOW) {
                    digitalWrite(gpio26LEDPin, HIGH);
                    sendSystemNotification("Light System", "Light System turned ON");
                }
            } else {
                if (digitalRead(gpio26LEDPin) == HIGH) {
                    digitalWrite(gpio26LEDPin, LOW); 
                    sendSystemNotification("Light System", "Light System turned OFF");
                }
            }
        }
    } else {
        if (digitalRead(gpio26LEDPin) == LOW) {
            digitalWrite(gpio26LEDPin, HIGH);
            sendSystemNotification("Light System", "Light System turned ON");
        }
    }
}

// Function to read water level sensors and update states
void updateWaterLevelStates() {
    if (digitalRead(waterLevelPin25) == LOW) {
        waterLevelStates[0] = false;
    } else {
        waterLevelStates[0] = true;
    }
    if (digitalRead(waterLevelPin23) == LOW) {
        waterLevelStates[1] = false;
    } else {
        waterLevelStates[1] = true;
    }
    if (digitalRead(waterLevelPin13) == LOW) {
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
            if (json.get(jsonData, "fields/waterLevelState/booleanValue")) {
                if (waterLevelStates[i] != jsonData.boolValue) {
                    waterLevelStates[i] = jsonData.boolValue;
                    if (waterLevelStates[i]) {
                        sendSystemNotification(units[i], "Water level is LOW");
                    } else {
                        sendSystemNotification(units[i], "Water level is NORMAL");
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
void controlLEDs() {
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
    fetchSystemName();
    fetchLightIntervals();

    pinMode(waterLevelPin25, INPUT);
    pinMode(waterLevelPin23, INPUT);
    pinMode(waterLevelPin13, INPUT);
    pinMode(power12V, OUTPUT);
    digitalWrite(power12V, HIGH);
    pinMode(gpio26LEDPin, OUTPUT);
    digitalWrite(gpio26LEDPin, LOW); 

    ledcSetup(redLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcAttachPin(redLEDPin, redLEDPwmChannel);
    ledcSetup(greenLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcAttachPin(greenLEDPin, greenLEDPwmChannel);
    ledcSetup(blueLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcAttachPin(blueLEDPin, blueLEDPwmChannel);
}

void loop() {
    unsigned long currentMillis = millis();
     if (currentMillis - previousHeartbeatMillis >= heartbeatInterval) {
        sendHeartbeat();
        previousHeartbeatMillis = currentMillis;
    }
    updateWaterLevelStates();
    fetchLightIntervals();
    controlGpio26LED();
    readFirestoreConfig();
    controlLEDs(); 
}
