#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "config.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoOTA.h>

class NetworkManager {
private:
    FirebaseData& fbdo;
    FirebaseAuth& auth;
    FirebaseConfig& config;
    String setupWifiName;
    bool wifiConnected = false;
    bool configPortalRunning = false;
    unsigned long lastReconnectAttempt = 0;
    unsigned long lastWiFiCheck = 0;
    const unsigned long WIFI_CHECK_INTERVAL = 30000; // Check WiFi every 30 seconds
    const unsigned long RECONNECT_INTERVAL = 5000;   // Try to reconnect every 5 seconds
    
    void initializeTime() {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        // Wait for time to be set
        int retries = 0;
        while (time(nullptr) < 1000 && retries < 10) {
            delay(100);
            retries++;
        }
    }
    
    bool connectToWiFi() {
        String hostname = "SA" + serialNumber;
        WiFi.setHostname(hostname.c_str());
        WiFi.mode(WIFI_AP_STA);
        WiFi.setAutoReconnect(true);  // Enable auto reconnect
        WiFi.persistent(true);        // Save WiFi settings to flash
        
        WiFiManager wm;
        wm.setConfigPortalTimeout(180);
        wm.startWebPortal();
        
        Serial.println("Starting Wi-Fi connection process...");
        
        // Try connecting to known networks
        for (int i = 0; i < knownWiFiCount; i++) {
            Serial.print("Attempting to connect to ");
            Serial.println(knownWiFi[i].ssid);
            
            WiFi.begin(knownWiFi[i].ssid, knownWiFi[i].password);
            
            unsigned long startAttemptTime = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
                wm.process();
                delay(100);
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\nConnected to Wi-Fi: " + String(knownWiFi[i].ssid));
                Serial.print("IP Address: ");
                Serial.println(WiFi.localIP());
                wm.stopWebPortal();
                return true;
            }
        }
        
        // If no connection, allow manual configuration via AP
        Serial.println("Switching to AP mode for manual configuration...");
        String wifi_SSID = "SkyAcres WiFi Setup " + serialNumber;
        if (!wm.startConfigPortal(wifi_SSID.c_str(), "password")) {
            return false;
        }
        
        return WiFi.status() == WL_CONNECTED;
    }
    
    void initializeFirebase() {
        config.api_key = API_KEY;
        auth.user.email = USER_EMAIL;
        auth.user.password = USER_PASSWORD;
        Firebase.begin(&config, &auth);
        Firebase.reconnectWiFi(true);
        
        // Wait for Firebase to be ready
        unsigned long startTime = millis();
        while (!Firebase.ready() && millis() - startTime < 10000) {
            delay(100);
        }
        
        if (!Firebase.ready()) {
            Serial.println("Failed to initialize Firebase");
        }
    }

public:
    NetworkManager(FirebaseData& fbdo, FirebaseAuth& auth, FirebaseConfig& config, String setupWifiName)
        : fbdo(fbdo), auth(auth), config(config), setupWifiName(setupWifiName) {}
    
    bool begin() {
        if (!connectToWiFi()) {
            Serial.println("Wi-Fi setup failed.");
            return false;
        }
        
        initializeTime();
        initializeFirebase();
        return true;
    }
    
    bool isConnected() {
        unsigned long currentMillis = millis();
        
        // Check WiFi status periodically
        if (currentMillis - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
            lastWiFiCheck = currentMillis;
            wifiConnected = WiFi.status() == WL_CONNECTED;
            
            if (!wifiConnected) {
                Serial.println("WiFi disconnected, attempting to reconnect...");
                if (!reconnect()) {
                    Serial.println("Failed to reconnect to WiFi");
                    return false;
                }
            }
        }
        
        return wifiConnected && Firebase.ready();
    }
    
    bool reconnect() {
        unsigned long currentMillis = millis();
        
        // Only attempt reconnection after the reconnect interval
        if (currentMillis - lastReconnectAttempt < RECONNECT_INTERVAL) {
            return false;
        }
        
        lastReconnectAttempt = currentMillis;
        Serial.println("Wi-Fi disconnected. Retrying...");
        
        // Disconnect WiFi to force a clean reconnection
        WiFi.disconnect();
        delay(1000);
        
        // Try to reconnect
        if (!connectToWiFi()) {
            return false;
        }
        
        // Reinitialize Firebase
        initializeFirebase();
        initializeTime();
        
        return true;
    }
    
    void handleOTA(const String& hostname) {
        ArduinoOTA.setHostname(hostname.c_str());
        ArduinoOTA.begin();
    }
};

#endif // NETWORK_MANAGER_H 