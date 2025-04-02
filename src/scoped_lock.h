#ifndef SCOPED_LOCK_H
#define SCOPED_LOCK_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

/**
 * RAII wrapper for FreeRTOS mutex
 */
class ScopedLock {
private:
    SemaphoreHandle_t mutex;
    bool locked;

public:
    /**
     * Constructor
     * @param mutex Mutex to lock
     */
    explicit ScopedLock(SemaphoreHandle_t mutex) : mutex(mutex), locked(false) {
        if (mutex != NULL) {
            locked = xSemaphoreTake(mutex, SystemConfig::MUTEX_TIMEOUT_MS) == pdTRUE;
        }
    }

    /**
     * Destructor
     */
    ~ScopedLock() {
        if (locked && mutex != NULL) {
            xSemaphoreGive(mutex);
        }
    }

    /**
     * Check if mutex was successfully locked
     * @return true if mutex is locked
     */
    bool isLocked() const {
        return locked;
    }

    // Delete copy constructor and assignment operator
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
};

#endif // SCOPED_LOCK_H 