#ifndef MUTEX_MANAGER_H
#define MUTEX_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "error_manager.h"

/**
 * MutexManager Class
 * 
 * Provides thread-safe mutex management for critical sections
 */
class MutexManager {
private:
    SemaphoreHandle_t mutex;
    static constexpr TickType_t MUTEX_TIMEOUT = pdMS_TO_TICKS(5000);  // 5 second timeout
    
public:
    /**
     * ScopedLock class for RAII-style mutex locking
     */
    class ScopedLock {
    private:
        MutexManager& manager;
        bool locked;
        
    public:
        explicit ScopedLock(MutexManager& mgr) : manager(mgr), locked(false) {
            locked = manager.takeMutex();
        }
        
        explicit ScopedLock(const MutexManager& mgr) : manager(const_cast<MutexManager&>(mgr)), locked(false) {
            locked = manager.takeMutex();
        }
        
        ~ScopedLock() {
            if (locked) {
                manager.giveMutex();
            }
        }
        
        bool isLocked() const {
            return locked;
        }
    };
    
    MutexManager() : mutex(nullptr) {
        mutex = xSemaphoreCreateMutex();
        if (mutex == nullptr) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_CREATE_FAILED,
                "Failed to create mutex",
                "MutexManager::MutexManager"
            );
        }
    }
    
    virtual ~MutexManager() {
        if (mutex != nullptr) {
            vSemaphoreDelete(mutex);
        }
    }

    /**
     * Takes the mutex
     * @return true if mutex was taken successfully
     */
    bool takeMutex() const {
        if (mutex == nullptr) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_NOT_INITIALIZED,
                "Mutex not initialized",
                "MutexManager::takeMutex"
            );
            return false;
        }
        
        if (xSemaphoreTake(mutex, MUTEX_TIMEOUT) != pdTRUE) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_TIMEOUT,
                "Failed to take mutex",
                "MutexManager::takeMutex"
            );
            return false;
        }
        return true;
    }
    
    /**
     * Gives the mutex
     * @return true if mutex was given successfully
     */
    bool giveMutex() const {
        if (mutex == nullptr) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_NOT_INITIALIZED,
                "Mutex not initialized",
                "MutexManager::giveMutex"
            );
            return false;
        }
        
        if (xSemaphoreGive(mutex) != pdTRUE) {
            ErrorManager::mutexError(
                ErrorManager::ErrorCode::MUTEX_NOT_TAKEN,
                "Failed to give mutex",
                "MutexManager::giveMutex"
            );
            return false;
        }
        return true;
    }

    /**
     * Gets the mutex handle
     * @return The mutex handle
     */
    SemaphoreHandle_t getMutex() const {
        return mutex;
    }
};

#endif // MUTEX_MANAGER_H 