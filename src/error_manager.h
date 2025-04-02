#ifndef ERROR_MANAGER_H
#define ERROR_MANAGER_H

#include "config.h"
#include <Arduino.h>

namespace ErrorManager {
    // Error Categories
    enum class ErrorCategory {
        SYSTEM,     // System-level errors
        NETWORK,    // Network/connectivity errors
        FIREBASE,   // Firebase operation errors
        SENSOR,     // Sensor reading/validation errors
        HARDWARE,   // Hardware initialization/operation errors
        MUTEX      // Thread safety errors
    };

    // Error Codes
    enum class ErrorCode {
        // System Errors (1000-1999)
        SYSTEM_INIT_FAILED = 1000,
        SYSTEM_UPDATE_FAILED = 1001,
        SYSTEM_INVALID_STATE = 1002,
        HEAP_WARNING = 1003,  // Low heap memory warning

        // Network Errors (2000-2999)
        NETWORK_CONNECTION_FAILED = 2000,
        NETWORK_AUTH_FAILED = 2001,
        NETWORK_TIMEOUT = 2002,

        // Firebase Errors (3000-3999)
        FIREBASE_OPERATION_FAILED = 3000,
        FIREBASE_BATCH_FAILED = 3001,
        FIREBASE_PATH_INVALID = 3002,

        // Sensor Errors (4000-4999)
        SENSOR_READ_FAILED = 4000,
        SENSOR_VALIDATION_FAILED = 4001,
        SENSOR_NOT_INITIALIZED = 4002,

        // Hardware Errors (5000-5999)
        HARDWARE_INIT_FAILED = 5000,
        HARDWARE_I2C_ERROR = 5001,
        HARDWARE_PWM_ERROR = 5002,

        // Mutex Errors (6000-6999)
        MUTEX_TIMEOUT = 6000,
        MUTEX_CREATION_FAILED = 6001
    };

    // Error Severity Levels
    enum class Severity {
        DEBUG,    // Debug information
        INFO,     // Informational messages
        WARNING,  // Warning conditions
        ERROR,    // Error conditions
        CRITICAL  // Critical conditions
    };

    /**
     * Error structure to track error details
     */
    struct Error {
        ErrorCategory category;
        ErrorCode code;
        Severity severity;
        String message;
        String location;  // Function or component where error occurred
        unsigned long timestamp;

        Error(ErrorCategory cat, ErrorCode cd, Severity sev, const String& msg, const String& loc)
            : category(cat), code(cd), severity(sev), message(msg), location(loc), 
              timestamp(millis()) {}
    };

    /**
     * Logs an error with full context
     * Thread-safe: Yes
     * @param error Error structure containing error details
     */
    void logError(const Error& error) {
        String severityStr;
        switch (error.severity) {
            case Severity::DEBUG: severityStr = "DEBUG"; break;
            case Severity::INFO: severityStr = "INFO"; break;
            case Severity::WARNING: severityStr = "WARNING"; break;
            case Severity::ERROR: severityStr = "ERROR"; break;
            case Severity::CRITICAL: severityStr = "CRITICAL"; break;
        }

        Serial.printf("[%lu] %s - %s: %s (Code: %d)\n",
            error.timestamp,
            severityStr.c_str(),
            error.location.c_str(),
            error.message.c_str(),
            static_cast<int>(error.code));
    }

    /**
     * Creates and logs a system error
     * Thread-safe: Yes
     * @param code Error code
     * @param message Error message
     * @param location Error location
     * @param severity Error severity
     */
    void systemError(ErrorCode code, const String& message, const String& location, 
                    Severity severity = Severity::ERROR) {
        Error error(ErrorCategory::SYSTEM, code, severity, message, location);
        logError(error);
    }

    /**
     * Creates and logs a network error
     * Thread-safe: Yes
     * @param code Error code
     * @param message Error message
     * @param location Error location
     * @param severity Error severity
     */
    void networkError(ErrorCode code, const String& message, const String& location, 
                     Severity severity = Severity::ERROR) {
        Error error(ErrorCategory::NETWORK, code, severity, message, location);
        logError(error);
    }

    /**
     * Creates and logs a Firebase error
     * Thread-safe: Yes
     * @param code Error code
     * @param message Error message
     * @param location Error location
     * @param severity Error severity
     */
    void firebaseError(ErrorCode code, const String& message, const String& location, 
                      Severity severity = Severity::ERROR) {
        Error error(ErrorCategory::FIREBASE, code, severity, message, location);
        logError(error);
    }

    /**
     * Creates and logs a sensor error
     * Thread-safe: Yes
     * @param code Error code
     * @param message Error message
     * @param location Error location
     * @param severity Error severity
     */
    void sensorError(ErrorCode code, const String& message, const String& location, 
                    Severity severity = Severity::ERROR) {
        Error error(ErrorCategory::SENSOR, code, severity, message, location);
        logError(error);
    }

    /**
     * Creates and logs a hardware error
     * Thread-safe: Yes
     * @param code Error code
     * @param message Error message
     * @param location Error location
     * @param severity Error severity
     */
    void hardwareError(ErrorCode code, const String& message, const String& location, 
                      Severity severity = Severity::ERROR) {
        Error error(ErrorCategory::HARDWARE, code, severity, message, location);
        logError(error);
    }

    /**
     * Creates and logs a mutex error
     * Thread-safe: Yes
     * @param code Error code
     * @param message Error message
     * @param location Error location
     * @param severity Error severity
     */
    void mutexError(ErrorCode code, const String& message, const String& location, 
                   Severity severity = Severity::ERROR) {
        Error error(ErrorCategory::MUTEX, code, severity, message, location);
        logError(error);
    }
}

#endif // ERROR_MANAGER_H 