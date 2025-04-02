#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#include "config.h"

// WiFi credentials
namespace SystemConfig {
    static constexpr const char* WIFI_SSID = "Skyboard";
    static constexpr const char* WIFI_PASSWORD = "skyboard123";
    
    // Firebase credentials
    static constexpr const char* FIREBASE_API_KEY = "AIzaSyDfp9KFIxgs9Wb0AiJTENejm1GLjS2MCQI";
    static constexpr const char* FIREBASE_PROJECT_ID = "skyboard-fb32";
    static constexpr const char* FIREBASE_USER_EMAIL = "skyboard@farming.com";
    static constexpr const char* FIREBASE_USER_PASSWORD = "skyboard123";
}

#endif // CREDENTIALS_H