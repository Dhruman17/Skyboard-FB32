#ifndef FIREBASE_COMS
#define FIREBASE_COMS
#include <Firebase_ESP_Client.h>
#include <credentials.h>
#include <config.h>
#define FIREBASEJSON_USE_PSRAM

const String systemPath = "Systems/" + serialNumber;

time_t parseTime(const char *timestamp)
{
    struct tm tm;
    strptime(timestamp, "%Y-%m-%dT%H:%M:%S", &tm);
    tm.tm_year = 70; // Epoch year
    tm.tm_mon = 0;   // January
    tm.tm_mday = 1;  // 1st of the month
    return mktime(&tm);
}
// Function to format timestamp correctly for Firebase
String formatTimestamp()
{
    time_t now;
    struct tm *tm_info;
    char buffer[30];
    time(&now);
    tm_info = localtime(&now);
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
    return String(buffer) + "Z";
}

void fetchFirebaseSystemData(FirebaseData *pFBDO, String *pSystemName, time_t *plightOnTime, time_t *plightOffTime, bool *plightMasterSwitch,
                             bool *ptimeCycleEnabled, String unitNames[NUMBER_OF_UNITS])
{
    if (Firebase.Firestore.getDocument(pFBDO, FIREBASE_PROJECT_ID, "", systemPath.c_str()))
    {
        FirebaseJson json;
        json.setJsonData(pFBDO->payload());
        FirebaseJsonData jsonData;
        Serial.println("Got payload");

        if (json.get(jsonData, "fields/systemName/stringValue"))
        {
            String systemName = jsonData.stringValue;
            *pSystemName = systemName;
            for (int i = 0; i < NUMBER_OF_UNITS; i++)
            {
                String unitName = String(i + 1);
                unitNames[i] = unitName;
            }
            Serial.println("The System name from Fetch System Name is: " + *pSystemName);
        }
        else
        {
            Serial.println("System name not found.");
        }
        // Fetch Light_Interval_On_Time
        if (json.get(jsonData, "fields/Light_Interval_On_Time/timestampValue"))
        {
            *plightOnTime = parseTime(jsonData.stringValue.c_str());
            Serial.println("The On Time is: " + jsonData.stringValue);
        }
        else
        {
            Serial.println("Light_Interval_On_Time not found or not a timestamp");
        }
        // Fetch Light_Interval_Off_Time
        if (json.get(jsonData, "fields/Light_Interval_Off_Time/timestampValue"))
        {
            *plightOffTime = parseTime(jsonData.stringValue.c_str());
            Serial.println("The Off Time is: " + jsonData.stringValue);
        }
        else
        {
            Serial.println("Light_Interval_Off_Time not found or not a timestamp");
        }
        // Fetch Light_Master_Switch
        if (json.get(jsonData, "fields/Light_Master_Switch/booleanValue"))
        {
            *plightMasterSwitch = jsonData.boolValue;
            Serial.println("Master Switch is: " + jsonData.stringValue);
        }
        else
        {
            Serial.println("Light_Master_Switch not found or not a boolean");
        }
        // Fetch Light_Time_Cycle_Switch
        if (json.get(jsonData, "fields/Light_Time_Cycle_Switch/booleanValue"))
        {
            *ptimeCycleEnabled = jsonData.boolValue;
            Serial.println("Time Cycle Switch is: " + jsonData.stringValue);
        }
        else
        {
            Serial.println("Light_Time_Cycle_Switch not found or not a boolean");
        }
        // Fetch system-level useCapacitive
        // Check for system-level useCapacitive
        if (json.get(jsonData, "fields/useCapacitive/booleanValue"))
        {
            useCapacitiveSensor = jsonData.boolValue;
            Serial.print("System-wide sensor mode: ");
            Serial.println(useCapacitiveSensor ? "Capacitive" : "Float");
        }
        else
        {
            useCapacitiveSensor = true;
            Serial.println("useCapacitive field missing. Setting default = true");

            // 1. Parse existing Firestore document
            FirebaseJson currentData;
            currentData.setJsonData(pFBDO->payload());

            FirebaseJsonData fieldsData;
            if (currentData.get(fieldsData, "fields"))
            {
                FirebaseJson fields;
                fields.setJsonData(fieldsData.stringValue); // all current fields

                // 2. Merge new field
                fields.set("useCapacitive/booleanValue", true);

                FirebaseJson mergedContent;
                mergedContent.set("fields", fields);

                // 3. Send full merged content back to Firestore
                bool success = Firebase.Firestore.patchDocument(
                    pFBDO,
                    FIREBASE_PROJECT_ID,
                    "",
                    systemPath.c_str(),
                    mergedContent.raw(),
                    "", "", "");

                if (success)
                {
                    Serial.println("useCapacitive added to system doc without overwriting.");
                }
                else
                {
                    Serial.println("Failed to merge useCapacitive: " + pFBDO->errorReason());
                }
            }
        }

        json.clear();
        jsonData.clear();
    }
    else
    {
        Serial.println("Failed to fetch data.");
        Serial.println(pFBDO->errorReason());
    }
}

void fetchFirebaseUnitData(FirebaseData *pFBDO, bool runitsEnabled[NUMBER_OF_UNITS], long rAtomizerOnIntervals[NUMBER_OF_UNITS], long rAtomizerOffIntervals[NUMBER_OF_UNITS], String unitNames[NUMBER_OF_UNITS])
{
    for (int i = 0; i < NUMBER_OF_UNITS; i++)
    {
        String documentPath;
        documentPath.reserve(100); // Prevent fragmentation
        documentPath = systemPath + "/units/";
        documentPath += unitNames[i];

        if (Firebase.Firestore.getDocument(pFBDO, FIREBASE_PROJECT_ID, "", documentPath.c_str()))
        {
            FirebaseJson json;
            json.setJsonData(pFBDO->payload());
            FirebaseJsonData jsonData;
            if (json.get(jsonData, "fields/unitState/booleanValue"))
            {
                bool newState = jsonData.boolValue;
                if (unitsEnabled[i] != newState)
                {
                    unitsEnabled[i] = newState;
                    if (!unitsEnabled[i])
                    {
                        Serial.println("Turning off atomizer for " + String(unitNames[i]));
                        ledcWrite(i, 0);
                    }
                }
            }
            if (json.get(jsonData, "fields/Interval_On/integerValue"))
            {
                atomizerOnIntervals[i] = jsonData.intValue * 1000;
            }
            if (json.get(jsonData, "fields/Interval_Off/integerValue"))
            {
                atomizerOffIntervals[i] = jsonData.intValue * 1000;
            }

            // 🧼 Free memory to prevent leaks
            json.clear();
            jsonData.clear();
        }
        else
        {
            Serial.println("Failed to get document for " + String(unitNames[i]));
            Serial.println(pFBDO->errorReason());
        }
    }
}

void sendUnitECValueToFirebase(FirebaseData *pFBDO, const String &unitName, float ecValue)
{
    String documentPath;
    documentPath.reserve(100);
    documentPath = systemPath + "/units/";
    documentPath += unitName;

    FirebaseJson content;
    content.set("fields/EC_Sensor_Value/doubleValue", ecValue);
    content.set("fields/EC_Updated/timestampValue", formatTimestamp());

    if (Firebase.Firestore.patchDocument(pFBDO, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "EC_Sensor_Value, EC_Updated"))
    {
        Serial.println("EC value updated for " + unitName);
    }
    else
    {
        Serial.println("Failed to update EC value for " + unitName + ": " + pFBDO->errorReason());
    }
    content.clear();
}
void sendUnitCapValueToFirebase(FirebaseData *pFBDO, const String &unitName, float waterLevel)
{
    String documentPath;
    documentPath.reserve(100);
    documentPath = systemPath + "/units/";
    documentPath += unitName;

    FirebaseJson content;
    content.set("fields/Cap_Water_Level/doubleValue", waterLevel);
    content.set("fields/Cap_Water_Updated/timestampValue", formatTimestamp());

    if (Firebase.Firestore.patchDocument(pFBDO, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "Cap_Water_Level,Cap_Water_Updated"))
    {
        Serial.println("Water level updated for " + unitName);
    }
    else
    {
        Serial.println("Failed to update water level for " + unitName + ": " + pFBDO->errorReason());
    }
    content.clear();
}
void sendFloatSensorState(FirebaseData *pFBDO, const String &unitName, bool isWet)
{
    String documentPath = systemPath + "/units/" + unitName;

    FirebaseJson content;
    content.set("fields/Float_Water_State/booleanValue", isWet);
    content.set("fields/Float_Updated/timestampValue", formatTimestamp());

    if (Firebase.Firestore.patchDocument(pFBDO, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "Float_Water_State,Float_Updated"))
    {
        Serial.println("Float state updated for " + unitName);
    }
    else
    {
        Serial.println("Failed to update Float state for " + unitName);
    }
}

#endif //