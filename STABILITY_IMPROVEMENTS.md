# ESP32 System Stability Improvements

## Overview
This document outlines the stability improvements implemented to address system failures after 2-3 weeks of continuous operation.

## Root Causes Identified

### 1. Memory Fragmentation
- **Issue**: Excessive String concatenation without memory reservation
- **Impact**: Heap fragmentation leading to allocation failures
- **Solution**: Added memory pre-reservation and cleanup

### 2. Memory Leaks
- **Issue**: HTTPClient connections not properly closed on error paths
- **Impact**: Gradual memory exhaustion
- **Solution**: Ensured http.end() called in all code paths

### 3. Firebase Operation Overload
- **Issue**: Firebase operations attempted even during low memory conditions
- **Impact**: System crashes when heap < 20KB
- **Solution**: Added memory checks before Firebase operations

### 4. Incorrect Restart Interval
- **Issue**: DAILY_RESTART_INTERVAL set to 2 hours instead of 24 hours
- **Impact**: Too frequent restarts or no daily cleanup
- **Solution**: Fixed to 24-hour interval

## Implemented Solutions

### 1. Memory Manager (memory_manager.h)
- **Critical Heap Threshold**: 20KB - triggers immediate restart
- **Warning Heap Threshold**: 30KB - triggers cleanup attempts
- **Firebase Operation Minimum**: 25KB - skips operations if below
- **Monitoring**: Tracks heap fragmentation and minimum heap ever recorded
- **Auto-recovery**: Restarts system if consistently low heap detected

### 2. Enhanced Memory Monitoring
- Memory health checks every 30 seconds
- Detailed memory status logs every 5 minutes
- Heap fragmentation calculation and logging
- PSRAM usage tracking (if available)

### 3. Firebase Operation Protection
- Memory checks before all Firebase operations
- Operations skipped when heap < 25KB
- Proper cleanup of FirebaseJson objects
- String memory optimization with reserve() and clear()

### 4. HTTP Client Improvements
- Added timeout settings (10s for version check, 30s for OTA)
- Proper cleanup in all error paths
- Memory check before OTA updates (requires 50KB minimum)
- String memory freed after use

### 5. Daily Restart Strategy
- Corrected to 24-hour interval
- Sends final heartbeat before restart
- Logs memory status before restart
- Graceful shutdown with 2-second delay

## Testing Recommendations

### 1. Long-term Testing
- Run system for 4+ weeks continuously
- Monitor heap levels via serial output
- Check for memory fragmentation patterns
- Verify daily restart occurs at 24-hour mark

### 2. Stress Testing
- Simulate low memory conditions
- Test Firebase operation skipping
- Verify automatic recovery mechanisms
- Check OTA update behavior under low memory

### 3. Monitoring Points
- Free Heap: Should stay above 30KB normally
- Min Heap: Should not drop below 20KB
- Fragmentation: Should stay below 50%
- Daily Restart: Should occur every 24 hours

## Additional Recommendations

### 1. Future Improvements
- Consider using static buffers instead of String class
- Implement connection pooling for Firebase
- Add exponential backoff for failed operations
- Consider external watchdog timer

### 2. Configuration Tuning
Adjust these values in memory_manager.h based on your needs:
- `CRITICAL_HEAP_SIZE`: Currently 20KB
- `WARNING_HEAP_SIZE`: Currently 30KB
- `MIN_HEAP_FOR_FIREBASE`: Currently 25KB
- `DAILY_RESTART_INTERVAL`: Currently 24 hours

### 3. Monitoring
- Set up remote logging for heap statistics
- Track restart reasons and frequencies
- Monitor Firebase operation success rates
- Log memory status to SD card if available

## Expected Outcomes
- System should run continuously for 4+ weeks without crashes
- Memory usage should stabilize after initial operation
- Daily restarts should prevent long-term degradation
- Firebase operations should gracefully degrade under low memory
- System should auto-recover from temporary memory issues

## Files Modified
1. `src/main.cpp` - Added memory management, fixed restart interval
2. `src/firebase_coms.h` - Added memory checks to Firebase operations
3. `src/memory_manager.h` - New file with memory management utilities
4. Various Firebase functions - Added proper cleanup and memory checks

## Commit Information
All changes have been committed with detailed descriptions of the improvements made for system stability.