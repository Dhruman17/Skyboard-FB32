void fetchFirebaseSystemData(FirebaseData *pFBDO, String *pSystemName, time_t *plightOnTime, time_t *plightOffTime, bool *plightMasterSwitch,
                            bool *ptimeCycleEnabled, bool *punitsEnabled, unsigned long *patomizerOnIntervals,
                            unsigned long *patomizerOffIntervals) {
    FirebaseJson json;
    FirebaseJsonData jsonData;
    
    // Get system document
    char pathBuffer[SystemConfig::FIREBASE_PATH_BUFFER_SIZE];
    snprintf(pathBuffer, sizeof(pathBuffer), SystemConfig::SYSTEM_PATH_FORMAT, SystemConfig::SERIAL_NUMBER);
    
    if (Firebase.Firestore.getDocument(pFBDO, SystemConfig::FIREBASE_PROJECT_ID, "", pathBuffer)) {
        json.setJsonData(pFBDO->payload());
        
        // Get system name
        if (json.get(jsonData, "fields/systemName/stringValue")) {
            *pSystemName = jsonData.stringValue;
            Serial.println("System name from Firebase: " + *pSystemName);
        }
        
        // Get light settings
        if (json.get(jsonData, "fields/lightOnTime/stringValue")) {
            String timeStr = jsonData.stringValue;
            struct tm tm = {};
            strptime(timeStr.c_str(), "%H:%M", &tm);
            *plightOnTime = mktime(&tm);
            Serial.println("Light on time from Firebase: " + timeStr);
        }
        
        if (json.get(jsonData, "fields/lightOffTime/stringValue")) {
            String timeStr = jsonData.stringValue;
            struct tm tm = {};
            strptime(timeStr.c_str(), "%H:%M", &tm);
            *plightOffTime = mktime(&tm);
            Serial.println("Light off time from Firebase: " + timeStr);
        }
        
        if (json.get(jsonData, "fields/lightMasterSwitch/booleanValue")) {
            *plightMasterSwitch = jsonData.boolValue;
            Serial.println("Light master switch from Firebase: " + String(*plightMasterSwitch));
        }
        
        if (json.get(jsonData, "fields/timeCycleEnabled/booleanValue")) {
            *ptimeCycleEnabled = jsonData.boolValue;
            Serial.println("Time cycle enabled from Firebase: " + String(*ptimeCycleEnabled));
        }
        
        // Get unit settings
        for (int i = 0; i < SystemConfig::NUMBER_OF_UNITS; i++) {
            // Get unit document path
            snprintf(pathBuffer, sizeof(pathBuffer), SystemConfig::UNIT_PATH_FORMAT, 
                    SystemConfig::SERIAL_NUMBER, i);
            
            if (Firebase.Firestore.getDocument(pFBDO, SystemConfig::FIREBASE_PROJECT_ID, "", pathBuffer)) {
                json.setJsonData(pFBDO->payload());
                
                // Get unit enabled state
                if (json.get(jsonData, "fields/unitState/booleanValue")) {
                    punitsEnabled[i] = jsonData.boolValue;
                    Serial.printf("Unit %d enabled state from Firebase: %d\n", i, punitsEnabled[i]);
                }
                
                // Get atomizer intervals
                if (json.get(jsonData, "fields/Interval_On/integerValue")) {
                    patomizerOnIntervals[i] = jsonData.intValue;
                    Serial.printf("Unit %d on interval from Firebase: %lu\n", i, patomizerOnIntervals[i]);
                }
                
                if (json.get(jsonData, "fields/Interval_Off/integerValue")) {
                    patomizerOffIntervals[i] = jsonData.intValue;
                    Serial.printf("Unit %d off interval from Firebase: %lu\n", i, patomizerOffIntervals[i]);
                }
            }
        }
    } else {
        Serial.println("Failed to fetch system data from Firebase");
        ErrorManager::firebaseError(
            ErrorManager::ErrorCode::FIREBASE_OPERATION_FAILED,
            "Failed to fetch system data",
            "fetchFirebaseSystemData"
        );
    }
} 