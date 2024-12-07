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
    {"Verizon_B3JPTT", "update-fop6-cab"}
    
};
const int knownWiFiCount = sizeof(knownWiFi) / sizeof(knownWiFi[0]);

// Firebase Credentials
#define API_KEY "AIzaSyDfp9KFIxgs9Wb0AiJTENejm1GLjS2MCQI"
#define FIREBASE_PROJECT_ID "skyacres-marketplace"
#define USER_EMAIL "test@gmail.com"
#define USER_PASSWORD "test123"

#endif // CREDENTIALS_H