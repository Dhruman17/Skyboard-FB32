void fetchFirebaseSystemData(FirebaseData *pFBDO, String *pSystemName, time_t *plightOnTime, time_t *plightOffTime, bool *plightMasterSwitch,
                            bool *ptimeCycleEnabled, bool *punitsEnabled, unsigned long *patomizerOnIntervals,
                            unsigned long *patomizerOffIntervals) {
    FirebaseJson json;
    FirebaseJsonData jsonData;
    
    // Get system document
    char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
    snprintf(pathBuffer, sizeof(pathBuffer), SystemConfig::UNIT_PATH_FORMAT, SystemConfig::SERIAL_NUMBER);
    
    if (Firebase.Firestore.getDocument(pFBDO, SystemConfig::FIREBASE_PROJECT_ID, "", pathBuffer)) {
        json.setJsonData(pFBDO->payload());
        
        // Get system name
        if (json.get(jsonData, "fields/systeName/stringValue")) {  // Fixed typo in path
            String systemName = jsonData.stringValue;
            *pSystemName = systemName;
            
            // Get unit names
            for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
                String unitName = systemName + "-" + String(i + 1);
                // Use unitName as needed
            }
        }
        
        Serial.println("The System name from Fetch System Name is: " + *pSystemName);
        
        // Get other system data
        // ... rest of the function ...
    }
} 