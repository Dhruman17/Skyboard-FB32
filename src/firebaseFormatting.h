#ifndef FIREBASEFORMATTING_H
#define FIREBASEFORMATTING_H

// Function to parse time from Firebase timestamp string
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

#endif // FIREBASEFORMATTING