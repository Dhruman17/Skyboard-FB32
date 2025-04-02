#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "config.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoOTA.h>

/**
 * NetworkManager Class
 * 
 * Handles all network-related operations:
 * 1. WiFi Connection:
 *    - Automatic connection to known networks
 *    - Fallback to AP mode for manual configuration
 *    - Connection state monitoring and recovery
 * 
 * 2. Firebase Integration:
 *    - Authentication and configuration
 *    - Connection state management
 *    - Error handling and recovery
 * 
 * 3. OTA Updates:
 *    - Hostname configuration
 *    - Update server setup
 * 
 * Connection Management:
 * - Auto-reconnect enabled
 * - Persistent WiFi settings
 * - Connection check every 30 seconds
 * - Reconnection attempts every 5 seconds
 */
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
    
    /**
     * Initializes system time using NTP
     * Required for Firebase timestamps and scheduling
     */
    void initializeTime() {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        // Wait for time to be set
        int retries = 0;
        while (time(nullptr) < 1000 && retries < 10) {
            delay(100);
            retries++;
        }
    }
    
    /**
     * Attempts to connect to WiFi
     * Tries known networks first, then falls back to AP mode
     * @return true if connection successful
     */
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
    /**
     * Constructor
     * @param fbdo Reference to Firebase data object
     * @param auth Reference to Firebase auth object
     * @param config Reference to Firebase config object
     */
    NetworkManager(FirebaseData& fbdo, FirebaseAuth& auth, FirebaseConfig& config)
        : fbdo(fbdo), auth(auth), config(config) {}
    
    /**
     * Initializes network components
     * Sets up WiFi, Firebase, and time synchronization
     * @return true if initialization successful
     */
    bool begin() {
        if (!connectToWiFi()) {
            Serial.println("Wi-Fi setup failed.");
            return false;
        }
        
        initializeTime();
        initializeFirebase();
        return true;
    }
    
    /**
     * Checks if WiFi is connected
     * @return true if connected
     */
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
    
    /**
     * Attempts to reconnect to WiFi
     * @return true if reconnection successful
     */
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
    
    /**
     * Sets up OTA updates
     * @param hostname System hostname for OTA
     */
    void handleOTA(const String& hostname) {
        ArduinoOTA.setHostname(hostname.c_str());
        ArduinoOTA.begin();
    }
};

#endif // NETWORK_MANAGER_H 