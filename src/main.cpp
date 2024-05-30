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
unsigned long firebaseReadMillis = 0;
const unsigned long firebaseReadInterval = 10000; // Read from Firebase every 10 seconds

long intervalOn = 0;
long intervalOff = 0;
bool ledState = false;
int pwmFrequency = 5000; // Change to global variable
const int greenLEDPin = 5;
const int redLEDPin = 2;
const int blueLEDPin = 4;

const int pwmResolution = 8;
const int greenLEDPwmChannel = 0;
const int redLEDPwmChannel = 1;
const int blueLEDPwmChannel = 2;

bool powerButtonState = false;

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

long readTimerValue(const char* path) {
  if (Firebase.ready()) {
    if (Firebase.RTDB.getInt(&fbdo, path)) {
      return fbdo.intData();
    } else {
      Serial.println("FAILED to read timer value");
      Serial.println("REASON: " + fbdo.errorReason());
    }
  }
  return -1; // Indicate failure
}

void enableLEDs(bool state) {
  int dutyCycle = state ? 255 : 0; // Max duty cycle for 8-bit resolution is 255

  ledcWrite(greenLEDPwmChannel, dutyCycle);
  ledcWrite(redLEDPwmChannel, dutyCycle);
  ledcWrite(blueLEDPwmChannel, dutyCycle);
  
  updateFirebase("LED_Green", state);
  updateFirebase("LED_Red", state);
  updateFirebase("LED_Blue", state);
}

void disableLEDs() {
  // Detach PWM channels to "break the circuit"
  ledcDetachPin(greenLEDPin);
  ledcDetachPin(redLEDPin);
  ledcDetachPin(blueLEDPin);

  updateFirebase("LED_Green", false);
  updateFirebase("LED_Red", false);
  updateFirebase("LED_Blue", false);
}


void readFirebaseConfig() {
  // Check the power button state from Firebase
  powerButtonState = readPowerButtonState();

  // Read on and off timer values from Firebase
  long newIntervalOn = readTimerValue("On_Duration");
  long newIntervalOff = readTimerValue("Off_Duration");

// Read LED frequency from Firebase
  long newLedFrequency = readTimerValue("LED_Frequency");

  // Only update the intervals if valid values are retrieved
  if (newIntervalOn >= 0 && newIntervalOff >= 0) {
    intervalOn = newIntervalOn * 1000; // Convert to milliseconds
    intervalOff = newIntervalOff * 1000; // Convert to milliseconds

    // Update LED frequency
    pwmFrequency = newLedFrequency;
    ledcSetup(greenLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcSetup(redLEDPwmChannel, pwmFrequency, pwmResolution);
    ledcSetup(blueLEDPwmChannel, pwmFrequency, pwmResolution);

    // Print the current timer values
    Serial.print("Current On Timer: ");
    Serial.println(intervalOn);
    Serial.print("Current Off Timer: ");
    Serial.println(intervalOff);
    Serial.print("Current LED Frequency: ");
    Serial.println(pwmFrequency);
  }
}

void setup() {
  Serial.begin(9600); // Set baud rate to 9600 to match the Serial Monitor
  
  // Setup PWM channels
  ledcSetup(greenLEDPwmChannel, pwmFrequency, pwmResolution);
  ledcSetup(redLEDPwmChannel, pwmFrequency, pwmResolution);
  ledcSetup(blueLEDPwmChannel, pwmFrequency, pwmResolution);

  // Attach PWM channels to LED pins
  ledcAttachPin(greenLEDPin, greenLEDPwmChannel);
  ledcAttachPin(redLEDPin, redLEDPwmChannel);
  ledcAttachPin(blueLEDPin, blueLEDPwmChannel);
  
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

  // Initial read from Firebase
  readFirebaseConfig();
}


void loop() {
  unsigned long currentMillis = millis();

  // Only read from Firebase at specified intervals
  if (currentMillis - firebaseReadMillis >= firebaseReadInterval) {
    firebaseReadMillis = currentMillis;
    readFirebaseConfig();
  }

  if (powerButtonState) {
    // Reattach the PWM channels if the power button is on
    ledcAttachPin(greenLEDPin, greenLEDPwmChannel);
    ledcAttachPin(redLEDPin, redLEDPwmChannel);
    ledcAttachPin(blueLEDPin, blueLEDPwmChannel);

    if (ledState) {
      if (currentMillis - previousMillis >= intervalOn) {
        // Turn off the LEDs
        enableLEDs(false);
        previousMillis = currentMillis;
        ledState = false;
      }
    } else {
      if (currentMillis - previousMillis >= intervalOff) {
        // Turn on the LEDs
        enableLEDs(true);
        previousMillis = currentMillis;
        ledState = true;
      }
    }
  } else {
    // Disable the LEDs if the power button is off
    if (ledState || powerButtonState) {
      disableLEDs();
      ledState = false;
    }
  }
}
