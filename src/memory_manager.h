#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <Arduino.h>

// Memory thresholds
#define CRITICAL_HEAP_SIZE 20000  // 20KB - restart if below this
#define WARNING_HEAP_SIZE 30000   // 30KB - start cleanup if below this
#define MIN_HEAP_FOR_FIREBASE 25000 // 25KB - skip Firebase ops if below this

// Track heap fragmentation
class MemoryManager {
private:
    static unsigned long lastHeapCheck;
    static uint32_t minHeapEver;
    static int lowHeapCount;
    static const int MAX_LOW_HEAP_COUNT = 5;
    
public:
    static void init() {
        minHeapEver = ESP.getFreeHeap();
        lastHeapCheck = millis();
        lowHeapCount = 0;
    }
    
    static bool checkMemoryHealth() {
        uint32_t currentHeap = ESP.getFreeHeap();
        uint32_t minHeap = ESP.getMinFreeHeap();
        
        // Track minimum heap
        if (minHeap < minHeapEver) {
            minHeapEver = minHeap;
        }
        
        // Critical condition - immediate restart needed
        if (currentHeap < CRITICAL_HEAP_SIZE) {
            Serial.printf("[CRITICAL] Heap too low: %d bytes. Restarting...\n", currentHeap);
            delay(1000);
            ESP.restart();
            return false;
        }
        
        // Warning condition - count occurrences
        if (currentHeap < WARNING_HEAP_SIZE) {
            lowHeapCount++;
            Serial.printf("[WARNING] Low heap detected: %d bytes (count: %d/%d)\n", 
                         currentHeap, lowHeapCount, MAX_LOW_HEAP_COUNT);
            
            // If consistently low, restart
            if (lowHeapCount >= MAX_LOW_HEAP_COUNT) {
                Serial.println("[WARNING] Consistent low heap. Restarting for safety...");
                delay(1000);
                ESP.restart();
                return false;
            }
            
            // Try to free memory
            cleanupMemory();
            return true;
        }
        
        // Reset counter if heap recovers
        if (currentHeap > WARNING_HEAP_SIZE) {
            lowHeapCount = 0;
        }
        
        return true;
    }
    
    static bool canPerformFirebaseOperation() {
        uint32_t currentHeap = ESP.getFreeHeap();
        if (currentHeap < MIN_HEAP_FOR_FIREBASE) {
            Serial.printf("[MEMORY] Skipping Firebase operation - heap too low: %d bytes\n", currentHeap);
            return false;
        }
        return true;
    }
    
    static void cleanupMemory() {
        Serial.println("[MEMORY] Attempting memory cleanup...");
        
        // Force garbage collection on String pool
        String dummy;
        dummy.reserve(0);
        
        // Clear any temporary buffers
        yield();
        delay(10);
        
        uint32_t afterCleanup = ESP.getFreeHeap();
        Serial.printf("[MEMORY] Heap after cleanup: %d bytes\n", afterCleanup);
    }
    
    static void logMemoryStatus() {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minHeap = ESP.getMinFreeHeap();
        uint32_t heapSize = ESP.getHeapSize();
        uint32_t psramSize = ESP.getPsramSize();
        uint32_t freePsram = ESP.getFreePsram();
        
        Serial.println("===== Memory Status =====");
        Serial.printf("Heap Size: %d bytes\n", heapSize);
        Serial.printf("Free Heap: %d bytes (%.1f%%)\n", freeHeap, (float)freeHeap/heapSize*100);
        Serial.printf("Min Free Heap: %d bytes\n", minHeap);
        Serial.printf("Min Ever: %d bytes\n", minHeapEver);
        
        if (psramSize > 0) {
            Serial.printf("PSRAM Size: %d bytes\n", psramSize);
            Serial.printf("Free PSRAM: %d bytes\n", freePsram);
        }
        
        // Heap fragmentation estimate
        float fragmentation = 1.0 - ((float)minHeap / (float)freeHeap);
        Serial.printf("Fragmentation: %.1f%%\n", fragmentation * 100);
        Serial.println("========================");
    }
};

// Static member initialization
unsigned long MemoryManager::lastHeapCheck = 0;
uint32_t MemoryManager::minHeapEver = 0;
int MemoryManager::lowHeapCount = 0;

#endif // MEMORY_MANAGER_H