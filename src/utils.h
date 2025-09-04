#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

// Safe millis() comparison that handles overflow
bool isTimeElapsed(unsigned long startTime, unsigned long interval) {
    unsigned long currentTime = millis();
    
    // Handle millis() overflow (happens every ~49 days)
    if (currentTime < startTime) {
        // Overflow occurred, calculate time since overflow
        unsigned long timeSinceOverflow = currentTime;
        unsigned long timeBeforeOverflow = (0xFFFFFFFF - startTime);
        return (timeSinceOverflow + timeBeforeOverflow) >= interval;
    } else {
        // Normal case - no overflow
        return (currentTime - startTime) >= interval;
    }
}

// Get safe time difference handling overflow
unsigned long getTimeDifference(unsigned long startTime) {
    unsigned long currentTime = millis();
    
    if (currentTime < startTime) {
        // Overflow occurred
        return (0xFFFFFFFF - startTime) + currentTime;
    } else {
        return currentTime - startTime;
    }
}

#endif // UTILS_H