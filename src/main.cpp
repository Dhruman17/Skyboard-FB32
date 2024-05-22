#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// Insert your network credentials
#define WIFI_SSID "POTANU VAPAR MAFTYA"
#define WIFI_PASSWORD "Rogers@2433"

// Insert Firebase project API Key
#define API_KEY "AIzaSyCMIvMWv4p6rVaOvvu8RKfP-4Plg1RhBlE"

// Insert RTDB URL
#define DATABASE_URL "https://skydashboard-506ed-default-rtdb.firebaseio.com/"

// Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

unsigned long previousMillis = 0;
const long intervalOn = 5000;  // 5 seconds on
const long intervalOff = 1000; // 1 second off
bool ledState = false;

const int greenLEDPin = 5;
const int redLEDPin = 2;
const int blueLEDPin = 4;

bool powerButtonState = false;

void setup() {
  Serial.begin(9600); // Set baud rate to 9600 to match the Serial Monitor
  
  // Set LED pins as output
  pinMode(greenLEDPin, OUTPUT);
  pinMode(redLEDPin, OUTPUT);
  pinMode(blueLEDPin, OUTPUT);
  
  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // Assign the API key (required)
  config.api_key = API_KEY;

  // Assign the RTDB URL (required)
  config.database_url = DATABASE_URL;

  // Sign up
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("ok");
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  // Assign the callback function for the long running token generation task
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h
  
  // Initialize Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void updateFirebase(const char* path, bool state) {
  if (Firebase.ready()) {
    if (Firebase.RTDB.setBool(&fbdo, path, state)) {
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    } else {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }
  }
}

void updatePowerButtonState(bool state) {
  updateFirebase("Power_Button", state);
}

bool readPowerButtonState() {
  if (Firebase.ready()) {
    if (Firebase.RTDB.getBool(&fbdo, "Power_Button")) {
      return fbdo.boolData();
    } else {
      Serial.println("FAILED to read power button state");
      Serial.println("REASON: " + fbdo.errorReason());
      return false; // Default to false if reading fails
    }
  }
  return false;
}

void loop() {
  unsigned long currentMillis = millis();

  // Check the power button state from Firebase
  powerButtonState = readPowerButtonState();

  if (powerButtonState) {
    if (ledState) {
      if (currentMillis - previousMillis >= intervalOn) {
        // Turn off the LEDs
        digitalWrite(greenLEDPin, LOW);
        digitalWrite(redLEDPin, LOW);
        digitalWrite(blueLEDPin, LOW);
        
        // Update Firebase with the LED statuses
        updateFirebase("LED_Green", false);
        updateFirebase("LED_Red", false);
        updateFirebase("LED_Blue", false);

        previousMillis = currentMillis;
        ledState = false;
      }
    } else {
      if (currentMillis - previousMillis >= intervalOff) {
        // Turn on the LEDs
        digitalWrite(greenLEDPin, HIGH);
        digitalWrite(redLEDPin, HIGH);
        digitalWrite(blueLEDPin, HIGH);

        // Update Firebase with the LED statuses
        updateFirebase("LED_Green", true);
        updateFirebase("LED_Red", true);
        updateFirebase("LED_Blue", true);

        previousMillis = currentMillis;
        ledState = true;
      }
    }
  } else {
    // Ensure LEDs are turned off when power button is off
    digitalWrite(greenLEDPin, LOW);
    digitalWrite(redLEDPin, LOW);
    digitalWrite(blueLEDPin, LOW);

    // Update Firebase with the LED statuses if not already off
    if (ledState) {
      updateFirebase("LED_Green", false);
      updateFirebase("LED_Red", false);
      updateFirebase("LED_Blue", false);
      ledState = false;
    }
  }
}
