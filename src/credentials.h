#ifndef CREDENTIALS_H
#define CREDENTIALS_H

//Serial number and delays for system
String serialNumber = "1234567890"; // Unique serial number for each system

// Known Wi-Fi Networks
struct WiFiCredentials {
    const char* ssid;
    const char* password;
};

const WiFiCredentials knownWiFi[] = {
    {"Devil_ 4G", "Rathod@2442"},
    {"gladwin22", "Farmer24"},
};
const int knownWiFiCount = sizeof(knownWiFi) / sizeof(knownWiFi[0]);

// Firebase Project ID
#define FIREBASE_PROJECT_ID "skyacres-marketplace"

#endif // CREDENTIALS_H