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
const int redLEDPin = 5;
const int greenLEDPin = 4;
const int blueLEDPin = 2;
const int gpio26LEDPin = 26; // GPIO 26 for new LED
const int power12V = 12;
const int redLEDPwmChannel = 1;
const int greenLEDPwmChannel = 0;
const int blueLEDPwmChannel = 2;
const int gpio26LEDPwmChannel = 3; // PWM channel for GPIO 26 LED
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
unsigned long previousMillis[3] = {0, 0, 0};
unsigned long lastConnectionCheckMillis = 0;
unsigned long lastNotificationMillis = 0;
const unsigned long connectionCheckInterval = 10000; // 5 minutes in milliseconds
time_t lightOnTime;
time_t lightOffTime;
bool lightMasterSwitch = true; // Default to true for safety
// Function to initialize NTP
void initializeTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}
// Function to parse time from Firebase timestamp string
time_t parseTime(const char* timestamp) {
    struct tm tm;
    strptime(timestamp, "%Y-%m-%dT%H:%M:%S", &tm);
    // Only extract the time of day (hours, minutes, seconds), ignore the date
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
            Serial.println("systemName not found or not a string");
        }
    } else {
        Serial.println("Failed to fetch system name.");
        Serial.println(fbdo.errorReason());
    }
}
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
            Serial.println("Light Master Switch: " + String(lightMasterSwitch));
        } else {
            Serial.println("Light_Master_Switch not found or not a boolean");
        }

    } else {
        Serial.println("Failed to fetch light intervals.");
        Serial.println(fbdo.errorReason());
    }
}

// Modify controlGpio26LED to consider Light Master Switch
void controlGpio26LED() {
    time_t now;
    struct tm* currentTime;
    time(&now);
    currentTime = localtime(&now);
    
    // If Light Master Switch is off, turn off GPIO 26 and exit
    if (!lightMasterSwitch) {
        if (digitalRead(gpio26LEDPin) == HIGH) {
            digitalWrite(gpio26LEDPin, LOW); // Ensure LED is off
            sendSystemNotification("GPIO 26 LED", "GPIO 26 LED turned OFF due to Light Master Switch");
            Serial.println("GPIO 26 LED turned OFF due to Light Master Switch");
        }
        return; // Exit the function since the switch is off
    }

    // Proceed with normal interval-based control if Light Master Switch is on
    struct tm currentTimeOfDay = *currentTime;
    currentTimeOfDay.tm_year = 70;  // Epoch year
    currentTimeOfDay.tm_mon = 0;    // January
    currentTimeOfDay.tm_mday = 1;   // 1st of the month
    time_t currentTime_t = mktime(&currentTimeOfDay);

    if (lightOffTime < lightOnTime) {
        // Handle the wrap-around case
        if (currentTime_t >= lightOnTime || currentTime_t <= lightOffTime) {
            if (digitalRead(gpio26LEDPin) == LOW) {
                digitalWrite(gpio26LEDPin, HIGH); 
                sendSystemNotification("GPIO 26 LED", "GPIO 26 LED turned ON");
                Serial.println("GPIO 26 LED turned ON");
            }
        } else {
            if (digitalRead(gpio26LEDPin) == HIGH) {
                digitalWrite(gpio26LEDPin, LOW); 
                sendSystemNotification("GPIO 26 LED", "GPIO 26 LED turned OFF");
                Serial.println("GPIO 26 LED turned OFF");
            }
        }
    } else {
        // Handle the regular case
        if (currentTime_t >= lightOnTime && currentTime_t <= lightOffTime) {
            if (digitalRead(gpio26LEDPin) == LOW) {
                digitalWrite(gpio26LEDPin, HIGH);
                sendSystemNotification("GPIO 26 LED", "GPIO 26 LED turned ON");
                Serial.println("GPIO 26 LED turned ON");
            }
        } else {
            if (digitalRead(gpio26LEDPin) == HIGH) {
                digitalWrite(gpio26LEDPin, LOW); 
                sendSystemNotification("GPIO 26 LED", "GPIO 26 LED turned OFF");
                Serial.println("GPIO 26 LED turned OFF");
            }
        }
    }
}
// Function to check Wi-Fi and Firebase connection
void checkConnection() {
    if (WiFi.status() != WL_CONNECTED || !Firebase.ready()) {
        Serial.println("ESP32 disconnected from Wi-Fi or Firebase.");
        sendSystemNotification(systemName, "Device disconnected");
    } else {
        Serial.println("ESP32 is connected.");
        sendSystemNotification(systemName, "Device connected");
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
                        ledcWrite(i, 9); // Turn on the LED (full brightness)
                        sendSystemNotification(units[i], "Unit state changed: ON");
                    } else {
                        Serial.println("Turning off LED for " + String(units[i]));
                        ledcWrite(i, 0);   // Turn off the LED
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
    // Switch on Power 12V
    pinMode(power12V, OUTPUT);
    digitalWrite(power12V, HIGH);
    // Set GPIO 26 as output and initialize LED as OFF
    pinMode(gpio26LEDPin, OUTPUT);
    digitalWrite(gpio26LEDPin, HIGH);  // Start with LED off
    // Initialize other LEDs as PWM outputs
    ledcSetup(redLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcAttachPin(redLEDPin, redLEDPwmChannel);
    ledcSetup(greenLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcAttachPin(greenLEDPin, greenLEDPwmChannel);
    ledcSetup(blueLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcAttachPin(blueLEDPin, blueLEDPwmChannel);
}
void loop() {
    unsigned long currentMillis = millis();
    if (currentMillis - lastConnectionCheckMillis >= connectionCheckInterval) {
        lastConnectionCheckMillis = currentMillis;
        checkConnection();
        fetchLightIntervals();
    }
    readFirestoreConfig();
    controlLEDs();
    controlGpio26LED(); // Add this to control GPIO 26 LED based on the intervals
}