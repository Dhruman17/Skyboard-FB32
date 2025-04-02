#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include "config.h"

class NetworkManager {
private:
    WiFiManager wm;
    FirebaseData& fbdo;
    FirebaseAuth& auth;
    FirebaseConfig& config;
    String setupWifiName;
    bool wifiConnected;
    bool configPortalRunning;
    
    void initializeTime() {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    }

public:
    NetworkManager(FirebaseData& fbdo, FirebaseAuth& auth, FirebaseConfig& config, const String& wifiName)
        : fbdo(fbdo), auth(auth), config(config), setupWifiName(wifiName), 
          wifiConnected(false), configPortalRunning(false) {}
    
    bool begin() {
        Serial.begin(9600);
        randomSeed(analogRead(0));
        
        // Attempt to connect to known Wi-Fi networks
        wm.setConnectTimeout(20);
        wm.setConfigPortalTimeout(60);
        if (!wm.autoConnect(setupWifiName.c_str())) {
            Serial.println("Failed to configure WiFi. Restarting...");
            delay(3000);
            ESP.restart();
            return false;
        }
        
        // Initialize Firebase
        config.api_key = API_KEY;
        auth.user.email = USER_EMAIL;
        auth.user.password = USER_PASSWORD;
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);
        
        initializeTime();
        
        unsigned long startTime = millis();
        while (!Firebase.ready() && millis() - startTime < 10000) {
            delay(100);
        }
        
        if (!Firebase.ready()) {
            Serial.println("Failed to initialize Firebase. Restarting...");
            ESP.restart();
            return false;
        }
        
        return true;
    }
    
    bool reconnect() {
        Serial.println("Wi-Fi disconnected. Retrying...");
        unsigned long wifiTimeoutCheck = millis();
        unsigned long currentMillisWiFi;

        while (WiFi.status() != WL_CONNECTED) {
            currentMillisWiFi = millis();
            delay(1000);
            if (!wm.autoConnect(setupWifiName.c_str())) {
                Serial.println("Failed to configure WiFi. Restarting...");
                delay(3000);
                ESP.restart();
                return false;
            }
        }

        Serial.println("Wi-Fi reconnected. Reinitializing Firebase...");
        config.api_key = API_KEY;
        auth.user.email = USER_EMAIL;
        auth.user.password = USER_PASSWORD;
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);

        initializeTime();
        return true;
    }
    
    bool isConnected() const {
        return WiFi.status() == WL_CONNECTED && Firebase.ready();
    }
    
    void handleOTA(const String& hostname) {
        ArduinoOTA.setHostname(hostname.c_str());
        ArduinoOTA.onStart([]() {
            String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
            Serial.printf("Start updating %s\n", type.c_str());
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
    }
};

#endif // NETWORK_MANAGER_H 