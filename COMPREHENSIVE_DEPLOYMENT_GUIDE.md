# SkyAcres ESP32 Comprehensive Deployment Guide

## Table of Contents
- [Overview](#overview)
- [System Architecture](#system-architecture)
- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Local Network Deployment](#local-network-deployment)
- [Remote Firebase Deployment](#remote-firebase-deployment)
- [Stability Improvements](#stability-improvements)
- [Troubleshooting](#troubleshooting)
- [Best Practices](#best-practices)
- [Advanced Configuration](#advanced-configuration)

## Overview

The SkyAcres deployment system provides comprehensive firmware update capabilities for ESP32 devices with two main deployment methods:

1. **Local Network Deployment** - Device-specific compilation with local OTA updates
2. **Remote Firebase Deployment** - Cloud-based firmware distribution

### Key Features
- ✅ **Device-specific firmware compilation** preserves unique serial numbers
- ✅ **Prevents identity conflicts** that cause devices to go offline
- ✅ **Maintains Firebase tracking** for all devices
- ✅ **Parallel deployment** with configurable concurrency
- ✅ **Comprehensive stability improvements** for long-term operation
- ✅ **Automated discovery** and validation
- ✅ **Rollback capabilities** and deployment groups

## System Architecture

### Components
- **local_bulk_ota_deploy.py** - Local network OTA deployment (recommended)
- **bulk_ota_deploy.py** - Firebase-based remote deployment
- **Device CSV files** - Define deployment targets and configurations
- **PlatformIO** - Handles firmware compilation and OTA uploads
- **Memory Manager** - Stability monitoring and auto-recovery
- **Discovery tools** - Network scanning and device detection

### Deployment Flow
```
Device List (CSV) → Discovery → Compilation → Validation → Deployment → Verification
```

## Prerequisites

### Software Requirements
- **Python 3.7+** with pip
- **PlatformIO** installed and accessible (`pip install platformio`)
- **Git** for version control

### Hardware Requirements
- ESP32 devices with ArduinoOTA enabled
- Devices on same network as deployment computer
- Minimum 50KB free heap for OTA updates

### Network Requirements
- Stable network connection
- Devices accessible via hostname (e.g., `device.local`) or IP
- Network scanning permissions

## Quick Start

### 1. Environment Setup
```bash
# Install dependencies
pip install -r requirements_local.txt

# Create environment file (if needed)
cp .env.example .env
```

### 2. Prepare Device List
Create `deployment_list.csv`:
```csv
serial_number,board_type,target_version,deployment_group,notes,system_name,ip_address
TL1079927967,ESP32_THREE_PORT,1.58,production,TL01 device,TL01,192.168.68.150
MR6384439627,ESP32_S3,1.58,production,MR1 device,MR1,192.168.68.139
```

### 3. Set Target Version
```bash
echo "1.58" > src/Version.txt
```

### 4. Deploy
**Main deployment command (tested and verified):**
```bash
python local_bulk_ota_deploy.py --csv deployment_list.csv --network 192.168.68.0/24 --yes
```

**Test discovery first (recommended):**
```bash
python local_bulk_ota_deploy.py --csv deployment_list.csv --network 192.168.68.0/24 --discover-only
```

## Local Network Deployment

### Command Reference

| Purpose | Command |
|---------|---------|
| **Deploy All Devices** | `python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --yes` |
| **Test Discovery First** | `python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --discover-only` |
| **Dry Run Preview** | `python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --dry-run` |
| **Deploy Specific Group** | `python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --group testing --yes` |
| **Custom Network Range** | `python local_bulk_ota_deploy.py --csv devices.csv --network 10.0.0.0/24 --yes` |

### Command Options (All Verified)

| Option | Description | Example |
|--------|-------------|---------|
| `--csv FILE` | **Required** - Device CSV file path | `--csv test_local_deployment.csv` |
| `--network RANGE` | Network range to scan | `--network 192.168.68.0/24` |
| `--yes` | Skip confirmation prompts | `--yes` |
| `--dry-run` | Preview without deploying | `--dry-run` |
| `--discover-only` | Find devices only (no deployment) | `--discover-only` |
| `--group NAME` | Deploy to specific group | `--group testing` |
| `--max-concurrent N` | Parallel deployments (default: 5) | `--max-concurrent 3` |
| `--firmware-dir DIR` | Firmware directory path | `--firmware-dir ./firmware` |
| `--ota-port PORT` | ArduinoOTA port (default: 3232) | `--ota-port 3232` |

### CSV File Format

#### Required Fields
| Field | Description | Example |
|-------|-------------|---------|
| `serial_number` | Unique device identifier | `TL1079927967` |
| `board_type` | `ESP32_THREE_PORT` or `ESP32_S3` | `ESP32_S3` |
| `target_version` | Target firmware version | `1.58` |
| `system_name` | Device hostname | `TL01` |

#### Optional Fields
| Field | Description | Example |
|-------|-------------|---------|
| `deployment_group` | Deployment grouping | `testing`, `production` |
| `notes` | Device description | `Main lab device` |
| `ip_address` | Known IP address | `192.168.68.150` |

### Automated Firmware Setup

Create `setup_firmware.bat` for automated firmware preparation:
```batch
@echo off
set VERSION=%1
if "%VERSION%"=="" (
    echo Usage: setup_firmware.bat [version]
    exit /b 1
)

mkdir firmware\%VERSION%\ESP32_S3 2>nul
mkdir firmware\%VERSION%\ESP32_THREE_PORT 2>nul

if exist .pio\build\esps3_board\firmware.bin (
    copy .pio\build\esps3_board\firmware.bin firmware\%VERSION%\ESP32_S3\firmware.bin
    echo ✅ Copied ESP32_S3 firmware
)

if exist .pio\build\esp32dev_3port\firmware.bin (
    copy .pio\build\esp32dev_3port\firmware.bin firmware\%VERSION%\ESP32_THREE_PORT\firmware.bin
    echo ✅ Copied ESP32_THREE_PORT firmware
)
```

Usage: `setup_firmware.bat 1.58`

## Remote Firebase Deployment

### Setup

1. **Firebase Service Account**
   - Download service account key from Firebase Console
   - Save as `firebase-service-account-key.json`
   - **Never commit to version control**

2. **Prepare Firmware Structure**
   ```bash
   # Build and organize firmware
   pio run -e esp32dev_3port
   pio run -e esps3_board

   mkdir -p firmware/1.58/ESP32_S3
   mkdir -p firmware/1.58/ESP32_THREE_PORT

   cp .pio/build/esps3_board/firmware.bin firmware/1.58/ESP32_S3/
   cp .pio/build/esp32dev_3port/firmware.bin firmware/1.58/ESP32_THREE_PORT/
   ```

### Firebase Deployment Commands

| Purpose | Command |
|---------|---------|
| **Dry Run** | `python bulk_ota_deploy.py --csv test.csv --service-account key.json --dry-run` |
| **Deploy All** | `python bulk_ota_deploy.py --csv devices.csv --service-account key.json` |
| **Deploy Group** | `python bulk_ota_deploy.py --csv devices.csv --service-account key.json --group testing` |
| **Monitor Status** | `python bulk_ota_deploy.py --csv devices.csv --service-account key.json --monitor` |

## Stability Improvements

### Memory Management System

The deployment includes comprehensive stability improvements addressing system failures after 2-3 weeks of operation:

#### Key Improvements
- **Memory Manager** - Monitors heap, triggers cleanup and restarts
- **Critical Heap Threshold** - 20KB triggers immediate restart
- **Firebase Protection** - Operations skipped when heap < 25KB
- **Daily Restart** - Fixed to proper 24-hour interval
- **Memory Leak Prevention** - Proper HTTPClient cleanup

#### Memory Thresholds
| Threshold | Value | Action |
|-----------|-------|--------|
| Critical Heap | 20KB | Immediate restart |
| Warning Heap | 30KB | Cleanup attempts |
| Firebase Minimum | 25KB | Skip operations |
| OTA Minimum | 50KB | Skip OTA updates |

#### Monitoring Features
- Memory health checks every 30 seconds
- Detailed status logs every 5 minutes
- Heap fragmentation tracking
- PSRAM usage monitoring
- Automatic recovery mechanisms

### Long-term Stability Testing

#### Recommended Tests
1. **4+ week continuous operation** without crashes
2. **Memory stress testing** under low heap conditions
3. **Firebase operation validation** during memory constraints
4. **Daily restart verification** at 24-hour intervals

#### Monitoring Points
- **Free Heap**: Should stay above 30KB normally
- **Min Heap**: Should not drop below 20KB
- **Fragmentation**: Should stay below 50%
- **Daily Restarts**: Every 24 hours exactly

## Troubleshooting

### Device Discovery Issues

#### Network Connectivity
```bash
# Test device reachability
ping device.local
ping 192.168.68.150

# Scan for devices
python local_bulk_ota_deploy.py --csv devices.csv --discover-only
```

#### Common Network Ranges
| Network | Usage |
|---------|-------|
| `192.168.68.0/24` | Current SkyAcres network |
| `192.168.1.0/24` | Standard home/office |
| `10.0.0.0/24` | Corporate networks |

### Compilation Issues

#### PlatformIO Validation
```bash
# Check PlatformIO installation
pio --version

# Test compilation
pio run -e esps3_board
pio run -e esp32dev_3port
```

#### Source Code Verification
- Ensure `src/credentials.h` exists
- Check for syntax errors
- Verify board definitions in `platformio.ini`

### Deployment Failures

#### Manual OTA Test
```bash
pio run -e esps3_board -t upload --upload-port device.local
```

#### Device Requirements
- Device must be awake (not in deep sleep)
- ArduinoOTA enabled in firmware
- Sufficient free memory (50KB+ for OTA)
- Stable network connection

### Error Message Reference

| Error | Cause | Solution |
|-------|-------|----------|
| `Device not found` | Network/connectivity | Check network, device status |
| `PlatformIO not found` | Missing installation | `pip install platformio` |
| `Compilation failed` | Source code issues | Fix syntax errors, check includes |
| `Cannot ping direct IP` | Device unreachable | Verify IP, check device status |
| `OTA timeout` | Network/device busy | Check connection, retry |

## Best Practices

### Pre-Deployment
1. **Test with single device** before bulk operations
2. **Use dry-run mode** to validate discovery
3. **Verify firmware version** in `src/Version.txt`
4. **Validate CSV format** and device information
5. **Check network connectivity** to all devices

### During Deployment
1. **Monitor output** for errors and warnings
2. **Don't interrupt** compilation or upload processes
3. **Ensure stable network** connection
4. **Keep devices powered** during updates
5. **Use deployment groups** for staged rollouts

### Post-Deployment
1. **Verify device status** in Firebase/logs
2. **Check device functionality** remotely
3. **Monitor memory usage** via serial output
4. **Clean up** compiled firmware if needed
5. **Document results** for future reference

### Security Considerations
- Use trusted networks only for OTA updates
- Keep Firebase service accounts secure
- Verify firmware integrity before deployment
- Use deployment groups for risk mitigation
- Monitor device authentication status

## Advanced Configuration

### Custom Network Scanning
```bash
# Multiple network ranges
python local_bulk_ota_deploy.py --network 192.168.1.0/24,10.0.0.0/24 --csv devices.csv

# Custom OTA port
python local_bulk_ota_deploy.py --ota-port 8266 --csv devices.csv
```

### Memory Manager Tuning

Adjust in `src/memory_manager.h`:
```cpp
#define CRITICAL_HEAP_SIZE  20480   // 20KB
#define WARNING_HEAP_SIZE   30720   // 30KB
#define MIN_HEAP_FOR_FIREBASE 25600 // 25KB
#define DAILY_RESTART_INTERVAL (24 * 60 * 60 * 1000) // 24 hours
```

### Performance Optimization

#### Deployment Speed
- **Single device**: 30-60 seconds (compilation + upload)
- **Parallel deployment**: Configurable concurrency (default: 5)
- **Network dependency**: Local network speed affects upload

#### Resource Usage
- **Disk space**: ~2MB per compiled firmware
- **Memory**: Minimal during deployment
- **CPU**: High during compilation phase

### Directory Structure
```
Skyboard-FB32/
├── local_bulk_ota_deploy.py          # Local deployment script
├── bulk_ota_deploy.py                # Firebase deployment script
├── COMPREHENSIVE_DEPLOYMENT_GUIDE.md # This guide
├── test_local_deployment.csv         # Example device list
├── src/
│   ├── credentials.h                 # Device credentials
│   ├── main.cpp                      # Main firmware
│   ├── memory_manager.h              # Stability improvements
│   └── Version.txt                   # Current version
├── firmware_compiled/                # Compiled device-specific firmware
└── firmware/                         # Organized firmware binaries
    ├── 1.57/
    │   ├── ESP32_S3/firmware.bin
    │   └── ESP32_THREE_PORT/firmware.bin
    └── 1.58/
        ├── ESP32_S3/firmware.bin
        └── ESP32_THREE_PORT/firmware.bin
```

## Support Commands

```bash
# System status check
pio --version && python --version

# Device discovery
python local_bulk_ota_deploy.py --csv devices.csv --discover-only

# Single device test
python local_bulk_ota_deploy.py --csv single_device.csv --dry-run

# Manual firmware build
pio run -e esps3_board && pio run -e esp32dev_3port

# Network diagnostics
python find_device.py
python simple_find_device.py
python test_connection.py
```

---

**Document Version**: 2.0
**Last Updated**: September 29, 2025
**Author**: SkyAcres Development Team
**Combines**: BULK_OTA_DEPLOYMENT_GUIDE.md, DEPLOYMENT_GUIDE.md, BULK_OTA_DEPLOYMENT.md, STABILITY_IMPROVEMENTS.md