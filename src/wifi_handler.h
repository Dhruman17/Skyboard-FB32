#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <WiFi.h>
#include <WiFiManager.h>
#include <Firebase_ESP_Client.h>

// ===== CONFIGURABLE PARAMETERS =====
const unsigned long WIFI_RECONNECT_INTERVAL = 30000;     // Try to reconnect every 30 seconds
const unsigned long WIFI_OFFLINE_TIMEOUT    = 5 * 60 * 1000; // Reboot if offline for 5 minutes

// ===== STATE VARIABLES =====
unsigned long lastReconnectAttempt = 0;
unsigned long wifiLostSince = 0;
bool firebaseWasConnected = false;

// ===== FUNCTION: Try WiFi connection on startup =====
bool tryInitialWiFiConnect(const String& setupWifiName)
{
    WiFi.begin();
   Serial.println("[WiFi] Attempting to connect to saved WiFi...");

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n\u2705 Connected to WiFi.");
        return true;
    }

    Serial.println("\n\u26A0\uFE0F Failed to connect. Launching WiFiManager portal...");
    WiFiManager wm;
    wm.setConfigPortalTimeout(60); // Portal timeout in seconds
    return wm.autoConnect(setupWifiName.c_str());
}

// ===== FUNCTION: Handle reconnect attempts in loop() =====
void handleWiFiReconnect()
{
    if (WiFi.status() != WL_CONNECTED && millis() - lastReconnectAttempt > WIFI_RECONNECT_INTERVAL) {
        Serial.println("\u26A0\uFE0F WiFi disconnected. Attempting reconnect...");
        WiFi.disconnect(true);
        WiFi.begin();
        lastReconnectAttempt = millis();
    }
}

// ===== FUNCTION: Reboot device if WiFi is down too long =====
void checkWiFiFailsafe()
{
    static unsigned long wifiLostSince = 0;
    const unsigned long MAX_DISCONNECTED_TIME = 5 * 60 * 1000;  // 5 min

    if (WiFi.status() != WL_CONNECTED) {
        if (wifiLostSince == 0) {
            wifiLostSince = millis();
            Serial.println("📡 WiFi lost. Starting failsafe timer.");
        }

        if (millis() - wifiLostSince > MAX_DISCONNECTED_TIME) {
            Serial.println("❌ WiFi offline too long. Rebooting...");
            ESP.restart();
        }

        // Try reconnecting
        Serial.println("🔁 Attempting WiFi reconnect...");
        WiFi.disconnect();
        WiFi.begin();
        delay(1000);
    } else {
        // Reset timer and enable I2C sensors again
        if (wifiLostSince != 0) {
            Serial.println("✅ WiFi reconnected. Re-enabling I2C sensors.");
            ENABLE_I2C_SENSORS = true;
        }
        wifiLostSince = 0;
    }
}

// ===== FUNCTION: Check and reconnect Firebase if needed =====
void checkFirebaseReconnect(FirebaseConfig* config, FirebaseAuth* auth)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        if (!Firebase.ready())
        {
            if (firebaseWasConnected)
            {
                Serial.println("\u26A1 Firebase lost. Reinitializing...");
            }

            Firebase.begin(config, auth);
            Firebase.reconnectWiFi(true);
        }

        firebaseWasConnected = Firebase.ready();
    }
    else
    {
        firebaseWasConnected = false;
    }
}

// ===== FUNCTION: (Optional) Register WiFi Event Handler =====
void registerWiFiEventHandler()
{
    WiFi.onEvent([](WiFiEvent_t event) {
        if (event == SYSTEM_EVENT_STA_DISCONNECTED) {
Serial.println("[WiFi] Disconnected (event handler).");

        }
    });
}
IPAddress deriveStaticIP(const String& serial) {
    String suffix = serial.substring(serial.length() - 3);  // Last 3 digits
    int host = suffix.toInt();

    // Safety check: avoid 0, 1 (reserved), and >254
    if (host <= 1 || host > 254) host = 200;

    return IPAddress(192, 168, 1, host);
}

#endif // WIFI_HANDLER_H
