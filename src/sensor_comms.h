/**
 * Initializes FDC1004 sensor for a unit
 * @param unitIndex Index of the unit
 * @return true if initialization successful
 */
bool initializeFDC1004(int unitIndex) {
    hardwareManager.selectUnitAndSensor(unitIndex, SystemConfig::FDC1004_CHANNEL);
    
    // Clean up existing instance if any
    if (fdc1004[unitIndex] != nullptr) {
        delete fdc1004[unitIndex];
        fdc1004[unitIndex] = nullptr;
    }
    
    // Create new instance with memory check
    fdc1004[unitIndex] = new (std::nothrow) FDC1004(0x50);
    if (fdc1004[unitIndex] == nullptr) {
        Serial.println("Failed to allocate memory for FDC1004");
        return false;
    }
    
    // Configure FDC1004
    fdc1004[unitIndex]->configureMeasurementSingle(1, FDC1004_100HZ, 0);
    fdc1004[unitIndex]->triggerSingleMeasurement(1, 0);
    
    return true;
}

/**
 * Initializes MCP3021 sensor for a unit
 * @param unitIndex Index of the unit
 * @return true if initialization successful
 */
bool initializeMCP3021(int unitIndex) {
    hardwareManager.selectUnitAndSensor(unitIndex, SystemConfig::MCP3021_CHANNEL);
    
    // Clean up existing instance if any
    if (mcp3021[unitIndex] != nullptr) {
        delete mcp3021[unitIndex];
        mcp3021[unitIndex] = nullptr;
    }
    
    // Create new instance with memory check
    mcp3021[unitIndex] = new (std::nothrow) MCP3021();
    if (mcp3021[unitIndex] == nullptr) {
        Serial.println("Failed to allocate memory for MCP3021");
        return false;
    }
    
    return true;
} 