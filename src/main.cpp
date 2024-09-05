#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h> // Provide the token generation process info and error info
#include <addons/RTDBHelper.h>  // Provide the RTDB payload printing info and other helper functions

// Define your Wi-Fi credentials
#define WIFI_SSID "POTANU VAPAR MAFTYA"
#define WIFI_PASSWORD "Rogers@2433"

// Define your Firebase project details
#define API_KEY "AIzaSyDfp9KFIxgs9Wb0AiJTENejm1GLjS2MCQI"
#define FIREBASE_PROJECT_ID "skyacres-marketplace"
#define USER_EMAIL "test@gmail.com"   // Replace with your Firebase user email
#define USER_PASSWORD "test123"         // Replace with your Firebase user password

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// LED pins and PWM channels
const int redLEDPin = 5;
const int greenLEDPin = 4;
const int blueLEDPin = 2;

const int redLEDPwmChannel = 1;
const int greenLEDPwmChannel = 0;
const int blueLEDPwmChannel = 2;

const int pwmFrequency = 5000;
const int pwmResolution = 8; // 8-bit resolution

// Document paths for system and units
String systemPath = "Systems/H1-1";
String units[] = {"H1-1-1", "H1-1-2", "H1-1-3"}; // Array of units

// Variables to store configuration for each unit
bool unitStates[3] = {false, false, false}; // Initial states of units
long intervalOn[3] = {5000, 5000, 5000}; // Default on intervals
long intervalOff[3] = {5000, 5000, 5000}; // Default off intervals
bool ledStates[3] = {false, false, false}; // Initial LED states
unsigned long previousMillis[3] = {0, 0, 0}; // Track previous time for each unit

// Function to send system offline notification
void sendSystemOfflineNotification() {
    String documentPath = "SystemNotification/" + systemPath; // Path to store notification
    FirebaseJson content;
    content.set("fields/systemnotificationId/stringValue", auth.token.uid); // UID of the system
    content.set("fields/timestamp/timestampValue", Firebase.getCurrentTime());
    content.set("fields/message/stringValue", "System went offline");

    if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw())) {
        Serial.println("System offline notification sent.");
    } else {
        Serial.println("Failed to send system offline notification.");
        Serial.println(fbdo.errorReason());
    }
}

// Function to send water level low notification
void sendWaterLevelLowNotification() {
    String documentPath = "WaterlevelNotification/" + systemPath; // Path to store notification
    FirebaseJson content;
    content.set("fields/systemnotificationId/stringValue", auth.token.uid); // UID of the system
    content.set("fields/timestamp/timestampValue", Firebase.getCurrentTime());
    content.set("fields/message/stringValue", "Water level low");

    if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw())) {
        Serial.println("Water level low notification sent.");
    } else {
        Serial.println("Failed to send water level low notification.");
        Serial.println(fbdo.errorReason());
    }
}

// Function to read system config from Firestore
void readFirestoreConfig() {
    for (int i = 0; i < 3; i++) {
        String documentPath = systemPath + "/units/" + units[i];

        if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str())) {
            Serial.println(fbdo.payload());

            FirebaseJson json;
            json.setJsonData(fbdo.payload());

            FirebaseJsonData jsonData;

            if (json.get(jsonData, "fields/unitState/booleanValue")) {
                unitStates[i] = jsonData.boolValue;
                Serial.println("Unit " + String(units[i]) + " state: " + String(unitStates[i]));
            } else {
                Serial.println("UnitState for " + String(units[i]) + " not found or not a boolean");
            }

            if (json.get(jsonData, "fields/Interval_On/integerValue")) {
                intervalOn[i] = jsonData.intValue * 1000; // Convert seconds to milliseconds
                Serial.println("On_Duration for " + String(units[i]) + ": " + String(intervalOn[i]));
            } else {
                Serial.println("Interval_On not found or not an integer for " + String(units[i]));
            }

            if (json.get(jsonData, "fields/Interval_Off/integerValue")) {
                intervalOff[i] = jsonData.intValue * 1000; // Convert seconds to milliseconds
                Serial.println("Off_Duration for " + String(units[i]) + ": " + String(intervalOff[i]));
            } else {
                Serial.println("Interval_Off not found or not an integer for " + String(units[i]));
            }
        } else {
            Serial.println("Failed to get document for " + String(units[i]));
            Serial.println(fbdo.errorReason());
        }
    }
}

void setup() {
    Serial.begin(9600);

    // Connect to Wi-Fi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
    }
    Serial.println("Connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Initialize Firebase
    config.api_key = API_KEY;
    config.database_url = "";  // Firestore doesn't use this
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    if (Firebase.ready()) {
        Serial.println("Firebase initialized successfully.");
    } else {
        Serial.print("Firebase initialization failed: ");
        Serial.println(fbdo.errorReason());
    }

    // Initialize PWM channels
    ledcSetup(redLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcSetup(greenLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcSetup(blueLEDPwmChannel, pwmFrequency, pwmResolution);

    ledcAttachPin(redLEDPin, redLEDPwmChannel);
    ledcAttachPin(greenLEDPin, greenLEDPwmChannel);
    ledcAttachPin(blueLEDPin, blueLEDPwmChannel);

    // Fetch initial configuration from Firestore
    readFirestoreConfig();
}

void loop() {
    unsigned long currentMillis = millis();

    // Fetch configuration from Firestore periodically
    static unsigned long lastReadMillis = 0;
    if (currentMillis - lastReadMillis >= 5000) { // Fetch every 5 seconds
        lastReadMillis = currentMillis;
        readFirestoreConfig();
    }

    // Control LEDs based on fetched configuration
    for (int i = 0; i < 3; i++) {
        if (unitStates[i]) {
            if (currentMillis - previousMillis[i] >= (ledStates[i] ? intervalOn[i] : intervalOff[i])) {
                ledStates[i] = !ledStates[i];
                previousMillis[i] = currentMillis;

                int dutyCycle = ledStates[i] ? 255 : 0;
                if (i == 0) {
                    ledcWrite(redLEDPwmChannel, dutyCycle);
                } else if (i == 1) {
                    ledcWrite(greenLEDPwmChannel, dutyCycle);
                } else if (i == 2) {
                    ledcWrite(blueLEDPwmChannel, dutyCycle);
                }
            }
        } else {
            // Turn off the corresponding LED if unit is off
            if (i == 0) {
                ledcWrite(redLEDPwmChannel, 0);
            } else if (i == 1) {
                ledcWrite(greenLEDPwmChannel, 0);
            } else if (i == 2) {
                ledcWrite(blueLEDPwmChannel, 0);
            }
        }
    }

    // Simulate notifications based on conditions (for example purposes)
    bool systemOffline = false; // Simulate system offline condition
    bool waterLevelLow = false; // Simulate water level low condition

    if (systemOffline) {
        sendSystemOfflineNotification();
    }

    if (waterLevelLow) {
        sendWaterLevelLowNotification();
    }
}
