#ifndef CREDENTIALS_H
#define CREDENTIALS_H

// Serial number and delays for system
String serialNumber = ""; // Unique serial number for each system

// Known Wi-Fi Networks
struct WiFiCredentials
{
    const char *ssid;
    const char *password;
};

String setupWifiName = "SkyAcres Setup " + serialNumber;

// Firebase Credentials
#define API_KEY "AIzaSyDfp9KFIxgs9Wb0AiJTENejm1GLjS2MCQI"
#define FIREBASE_PROJECT_ID "skyacres-marketplace"
#define USER_EMAIL "info@skyacres.ca"
#define USER_PASSWORD "SkyacresBC"

#endif // CREDENTIALS_H