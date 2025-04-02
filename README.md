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

### Cloud Integration
- Firebase Realtime Database integration
- Remote system monitoring and control
- OTA (Over-The-Air) firmware updates
- Real-time sensor data logging
- System heartbeat monitoring

### Smart Features
- Automated irrigation scheduling
- Water level monitoring and alerts
- EC value tracking for nutrient management
- Time-based lighting control
- Multi-unit independent operation
- Automatic reconnection handling

## Hardware Requirements

### Core Components
- ESP32 development board
- FDC1004 capacitive sensor (water level)
- MCP3021 ADC (EC measurement)
- TCA9548APWR I2C multiplexer
- PCA9546A I2C multiplexers (one per unit)
- Atomizers
- System lighting

### Pin Configuration
- I2C: SDA (21), SCL (22)
- System Power: GPIO 32
- System Lights: GPIO 33
- Atomizer Pins: GPIO 25, 26, 27
- Water Level Pins: GPIO 14, 12, 13

## Software Setup

### Prerequisites
- Arduino IDE
- ESP32 board support
- Required libraries:
  - Firebase_ESP_Client
  - WiFiManager
  - MCP3X21
  - Protocentral_FDC1004

### Installation
1. Clone this repository
2. Install required libraries through Arduino IDE Library Manager
3. Configure Firebase credentials in `credentials.h`
4. Upload the firmware to your ESP32

### Configuration
1. Update WiFi credentials in `credentials.h`
2. Configure Firebase project settings
3. Set system parameters in `config.h`

## Usage

### Initial Setup
1. Power on the system
2. Connect to the WiFi network
3. System will automatically connect to Firebase
4. Configure unit settings through Firebase console

### Operation
- System automatically manages:
  - Irrigation cycles
  - Water level monitoring
  - EC measurements
  - Lighting schedule
  - System health monitoring

### Monitoring
- Access real-time data through Firebase console
- Monitor system status and sensor readings
- Configure unit parameters remotely
- View system logs and alerts

## Firebase Data Structure

### System Document
```
systems/{systemId}/
├── lastSeen: timestamp
├── systemName: string
├── firmwareVersion: string
├── lightOnTime: string (HH:MM)
├── lightOffTime: string (HH:MM)
├── lightMasterSwitch: boolean
├── timeCycleEnabled: boolean
└── units/
    └── {unitId}/
        ├── unitName: string
        ├── unitState: boolean
        ├── Interval_On: integer
        ├── Interval_Off: integer
        ├── waterLevel: double
        ├── waterLevelState: boolean
        └── ecValue: double
```

## Troubleshooting

### Common Issues
1. WiFi Connection
   - Check WiFi credentials
   - Verify network availability
   - Check signal strength

2. Sensor Readings
   - Verify I2C connections
   - Check multiplexer addresses
   - Calibrate sensors if needed

3. Firebase Connection
   - Verify API credentials
   - Check internet connectivity
   - Monitor Firebase quotas

### Error Recovery
- System automatically attempts to reconnect
- Watchdog timer for system stability
- Automatic firmware rollback on update failure

## Contributing
1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Push to the branch
5. Create a Pull Request

## License
This project is licensed under the MIT License - see the LICENSE file for details.

## Support
For support, please contact:
- Email: info@skyacres.ca
- Website: www.skyacres.ca