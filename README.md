# Skyboard-FB32

A sophisticated vertical farming control system built for ESP32, featuring multi-unit management, sensor monitoring, and cloud integration.

## Features

### Hardware Integration
- Multi-unit vertical farming system (up to 3 units)
- Water level monitoring using FDC1004 capacitive sensors
- EC (Electrical Conductivity) measurement using MCP3021 ADC
- PWM-controlled atomizer system for irrigation
- Two-level I2C multiplexing for sensor management
  - TCA9548APWR (system level)
  - PCA9546A (unit level)
- LED lighting control with time-based scheduling
- Automatic sensor calibration and error detection
- Power management and monitoring
- Watchdog timer for system stability

### Cloud Integration
- Firebase Realtime Database integration
- Remote system monitoring and control
- Dual OTA (Over-The-Air) firmware update system:
  - Firebase OTA (Primary):
    - Automatic version checking every hour
    - Chunked firmware downloads
    - Firmware verification before applying
    - Version tracking in Firebase
    - Automatic system restart after update
  - ArduinoOTA (Development):
    - Direct firmware uploads via Arduino IDE
    - Development and testing support
    - Backup update mechanism
- Real-time sensor data logging
- System heartbeat monitoring
- Automatic reconnection handling
- Data buffering for offline operation
- Error reporting and diagnostics

### Smart Features
- Automated irrigation scheduling
- Water level monitoring and alerts
- EC value tracking for nutrient management
- Time-based lighting control
- Multi-unit independent operation
- Automatic reconnection handling
- Sensor calibration and validation
- Error recovery and system health monitoring
- Power optimization
- Data validation and error correction

## Hardware Requirements

### Core Components
- ESP32 development board
- FDC1004 capacitive sensor (water level)
- MCP3021 ADC (EC measurement)
- TCA9548APWR I2C multiplexer
- PCA9546A I2C multiplexers (one per unit)
- Atomizers
- System lighting
- Power supply (5V/3.3V)
- Optional: External RTC for time synchronization

### Pin Configuration
- I2C: SDA (21), SCL (22)
- System Power: GPIO 32
- System Lights: GPIO 33
- Atomizer Pins: GPIO 25, 26, 27
- Water Level Pins: GPIO 14, 12, 13
- Optional: External RTC SDA (4), SCL (5)

## Software Setup

### Prerequisites
- Arduino IDE or PlatformIO
- ESP32 board support
- Required libraries:
  - Firebase_ESP_Client
  - WiFiManager
  - MCP3X21
  - Protocentral_FDC1004
  - ArduinoOTA (built-in)
  - ArduinoJson
  - ESPAsyncWebServer

### Installation
1. Clone this repository
2. Install required libraries through Arduino IDE Library Manager or PlatformIO
3. Configure Firebase credentials in `credentials.h`
4. Set up your development environment:
   - For Arduino IDE: Configure board settings
   - For PlatformIO: Configure `platformio.ini`
5. Upload the firmware to your ESP32

### Configuration
1. Update WiFi credentials in `credentials.h`
2. Configure Firebase project settings
3. Set system parameters in `config.h`:
   - System configuration
   - Update intervals
   - Sensor thresholds
   - Time settings
4. Configure OTA update settings:
   - Firebase OTA: Set firmware URL and path in `config.h`
   - ArduinoOTA: Set hostname and password in `NetworkManager`
5. Optional: Configure external RTC settings

## Usage

### Initial Setup
1. Power on the system
2. Connect to the WiFi network
3. System will automatically connect to Firebase
4. Configure unit settings through Firebase console
5. Calibrate sensors if needed
6. Set up lighting schedule

### Operation
- System automatically manages:
  - Irrigation cycles
  - Water level monitoring
  - EC measurements
  - Lighting schedule
  - System health monitoring
  - Firmware updates
  - Sensor calibration
  - Error recovery
  - Power management
  - Data validation

### Monitoring
- Access real-time data through Firebase console
- Monitor system status and sensor readings
- Configure unit parameters remotely
- View system logs and alerts
- Track firmware versions and update status
- Monitor system health and diagnostics
- View sensor calibration status
- Track power consumption

### Firmware Updates
1. Firebase OTA (Production):
   - System automatically checks for updates every hour
   - Updates are downloaded in chunks to handle large firmware files
   - Firmware is verified before applying
   - System version is updated in Firebase
   - System automatically restarts after successful update
   - Automatic rollback on update failure

2. ArduinoOTA (Development):
   - Connect to the system's WiFi network
   - Use Arduino IDE to upload firmware
   - System automatically handles the update process
   - Development and testing support

## Firebase Data Structure

### System Document
```
systems/{systemId}/
├── lastSeen: timestamp
├── systemName: string
├── firmwareVersion: string
├── lastUpdated: timestamp
├── lightOnTime: string (HH:MM)
├── lightOffTime: string (HH:MM)
├── lightMasterSwitch: boolean
├── timeCycleEnabled: boolean
├── systemHealth: object
│   ├── lastCalibration: timestamp
│   ├── sensorStatus: object
│   ├── powerStatus: object
│   └── errorLog: array
└── units/
    └── {unitId}/
        ├── unitName: string
        ├── unitState: boolean
        ├── Interval_On: integer
        ├── Interval_Off: integer
        ├── waterLevel: double
        ├── waterLevelState: boolean
        ├── ecValue: double
        ├── calibrationData: object
        └── errorHistory: array
```

## Development

### Building and Uploading
1. Use PlatformIO for development
2. Configure build settings in `platformio.ini`
3. Build and upload using PlatformIO commands
4. Monitor build output for warnings and errors

### Testing
1. Use ArduinoOTA for quick development testing
2. Use Firebase OTA for production testing
3. Monitor update process through serial output
4. Test sensor calibration
5. Verify error handling
6. Check power management
7. Validate data integrity

### Troubleshooting
1. Check serial output for update status
2. Verify Firebase connectivity
3. Ensure proper firmware version format
4. Monitor system logs for update errors
5. Check sensor calibration status
6. Verify power supply stability
7. Monitor system health metrics
8. Review error logs

## Contributing
1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Create a Pull Request
6. Follow coding standards
7. Include documentation updates
8. Add test cases

## License
This project is licensed under the MIT License - see the LICENSE file for details.

## Support
For support, please contact:
- Email: info@skyacres.ca
- Website: www.skyacres.ca
- Documentation: docs.skyacres.ca
- Issue Tracker: GitHub Issues