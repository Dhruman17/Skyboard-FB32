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
const int greenLEDPin = 4;
const int redLEDPin = 5;
const int blueLEDPin = 2;

const int greenLEDPwmChannel = 0;
const int redLEDPwmChannel = 1;
const int blueLEDPwmChannel = 2;

const int pwmFrequency = 5000;
const int pwmResolution = 8; // 8-bit resolution

// Define Firestore document path
const char* documentPath = "Systems/H1-1";

// Variables to store configuration
bool powerButtonState = false;
long intervalOn = 5000; // Default to 5 seconds
long intervalOff = 5000; // Default to 5 seconds
bool ledState = false;
unsigned long previousMillis = 0;

void readFirestoreConfig() {
    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath)) {
        Serial.println(fbdo.payload());

        FirebaseJsonData jsonData;

        FirebaseJson json;
        json.setJsonData(fbdo.payload());

        if (json.get(jsonData, "fields/Power_Button/booleanValue") && jsonData.typeNum == FirebaseJson::JSON_BOOL) {
            powerButtonState = jsonData.boolValue;
            Serial.println("Power_Button state: " + String(powerButtonState));
        } else {
            Serial.println("Power_Button not found or not a boolean");
        }

        if (json.get(jsonData, "fields/Interval_On/integerValue") ){
            intervalOn = jsonData.intValue * 1000; // Convert seconds to milliseconds
            Serial.println("On_Duration: " + String(intervalOn));
        } else {
            Serial.println("Interval_On not found or not an integer");
        }

        if (json.get(jsonData, "fields/Interval_Off/integerValue") ){
            intervalOff = jsonData.intValue * 1000; // Convert seconds to milliseconds
            Serial.println("Off_Duration: " + String(intervalOff));
        } else {
            Serial.println("Interval_Off not found or not an integer");
        }
    } else {
        Serial.println("Failed to get document");
        Serial.println(fbdo.errorReason());
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
    config.database_url = "https://your-project-id.firebaseio.com"; // Set it to any valid URL or an empty string
    config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    if (Firebase.ready()) {
        Serial.println("Firebase initialized successfully.");
    } else {
        Serial.print("Firebase initialization failed: ");
        Serial.println(fbdo.errorReason());
    }

    // Initialize PWM channels
    ledcSetup(greenLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcSetup(redLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcSetup(blueLEDPwmChannel, pwmFrequency, pwmResolution);

    ledcAttachPin(greenLEDPin, greenLEDPwmChannel);
    ledcAttachPin(redLEDPin, redLEDPwmChannel);
    ledcAttachPin(blueLEDPin, blueLEDPwmChannel);

    // Fetch initial configuration from Firestore
    readFirestoreConfig();
}

void loop() {
    unsigned long currentMillis = millis();

    // Fetch configuration from Firestore periodically
    static unsigned long lastReadMillis = 0;
    if (currentMillis - lastReadMillis >= 5000) { // Fetch every 60 seconds
        lastReadMillis = currentMillis;
        readFirestoreConfig();
    }

    // Control LEDs based on fetched configuration
    if (powerButtonState) {
        if (currentMillis - previousMillis >= (ledState ? intervalOn : intervalOff)) {
            ledState = !ledState;
            previousMillis = currentMillis;

            int dutyCycle = ledState ? 255 : 0;
            ledcWrite(greenLEDPwmChannel, dutyCycle);
            ledcWrite(redLEDPwmChannel, dutyCycle);
            ledcWrite(blueLEDPwmChannel, dutyCycle);
        }
    } else {
        // Turn off all LEDs if power button is off
        ledcWrite(greenLEDPwmChannel, 0);
        ledcWrite(redLEDPwmChannel, 0);
        ledcWrite(blueLEDPwmChannel, 0);
    }
}
