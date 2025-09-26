# SkyAcres Bulk OTA Deployment System

## Overview

The SkyAcres Bulk OTA Deployment System allows you to deploy firmware updates to multiple ESP32 devices simultaneously while preserving each device's unique identity. This system was developed to solve serial number conflicts that occurred in previous bulk deployments, ensuring devices remain trackable in Firebase after updates.

## System Architecture

### Components
- **local_bulk_ota_deploy.py**: Main deployment script for local network OTA
- **bulk_ota_deploy.py**: Firebase-based deployment script (for remote deployments)
- **Device CSV Files**: Define which devices to update and their configurations
- **PlatformIO**: Handles firmware compilation and OTA uploads

### How It Works

1. **Device Discovery**: Script scans the network to find devices using their hostnames and IP addresses
2. **Device-Specific Compilation**: For each device, the script:
   - Temporarily modifies `src/credentials.h` with the device's serial number
   - Compiles firmware specifically for that device
   - Restores original source files
3. **OTA Deployment**: Uses PlatformIO to upload the device-specific firmware via network
4. **Identity Preservation**: Each device receives firmware with its correct serial number, maintaining Firebase tracking

## Prerequisites

### Software Requirements
- **Python 3.7+** with required packages
- **PlatformIO** installed and accessible
- **Git** for version control

### Network Requirements
- Devices and deployment computer on same network
- Devices must be online and OTA-enabled
- Network connectivity to device hostnames (e.g., `device.local`)

### Hardware Requirements
- ESP32 devices with ArduinoOTA enabled
- Sufficient flash memory for firmware updates

## File Structure

```
Skyboard-FB32/
├── local_bulk_ota_deploy.py          # Local network deployment script
├── bulk_ota_deploy.py                # Firebase-based deployment script
├── DEPLOYMENT_GUIDE.md               # This guide
├── test_local_deployment.csv         # Example device list
├── test_thq_only.csv                 # Single device test file
├── src/
│   ├── credentials.h                 # Device credentials (modified during compilation)
│   ├── main.cpp                      # Main firmware code
│   └── Version.txt                   # Current firmware version
├── firmware_compiled/                # Generated device-specific firmware
│   ├── {serial_number1}/firmware.bin
│   ├── {serial_number2}/firmware.bin
│   └── ...
└── platformio.ini                    # PlatformIO configuration
```

## Quick Start Guide

### 1. Prepare Device List

Create a CSV file with your devices:

```csv
serial_number,board_type,target_version,deployment_group,notes,system_name,ip_address
TL1079927967,ESP32_THREE_PORT,1.58,production,TL01 device,TL01,192.168.68.150
MR6384439627,ESP32_S3,1.58,production,MR1 device,MR1,192.168.68.139
123456789123456789,ESP32_S3,1.58,production,THQ device,THQ,10.0.0.78
```

### 2. Update Firmware Version

```bash
echo "1.58" > src/Version.txt
```

### 3. Deploy

```bash
python local_bulk_ota_deploy.py --csv deployment_list.csv --network 192.168.68.0/24 --yes
```

## Command Reference

### Basic Commands

| Purpose | Command |
|---------|---------|
| **Deploy All Devices** | `python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --yes` |
| **Test Discovery** | `python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --discover-only` |
| **Dry Run** | `python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --dry-run` |
| **Deploy Specific Group** | `python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --group testing --yes` |

### Command Line Options

| Option | Description | Example |
|--------|-------------|---------|
| `--csv FILE` | Path to device CSV file | `--csv deployment_list.csv` |
| `--network RANGE` | Network range to scan | `--network 192.168.68.0/24` |
| `--yes` | Skip confirmation prompts | `--yes` |
| `--dry-run` | Show what would happen without deploying | `--dry-run` |
| `--discover-only` | Find devices without deploying | `--discover-only` |
| `--group NAME` | Deploy only to specific group | `--group testing` |
| `--max-concurrent N` | Max parallel deployments (default: 5) | `--max-concurrent 3` |

### Network Ranges

| Network | Usage |
|---------|--------|
| `192.168.68.0/24` | Current SkyAcres network |
| `192.168.1.0/24` | Standard home/office networks |
| `10.0.0.0/24` | Corporate networks |

## CSV File Format

### Required Fields

| Field | Description | Example |
|-------|-------------|---------|
| `serial_number` | Unique device identifier (matches Firebase document ID) | `TL1079927967` |
| `board_type` | Hardware type: `ESP32_THREE_PORT` or `ESP32_S3` | `ESP32_S3` |
| `target_version` | Firmware version to deploy | `1.58` |
| `system_name` | Device hostname for network discovery | `TL01` |

### Optional Fields

| Field | Description | Example |
|-------|-------------|---------|
| `deployment_group` | Group for selective deployment | `testing`, `production` |
| `notes` | Human-readable description | `Main lab device` |
| `ip_address` | Known IP address (helps discovery) | `192.168.68.150` |

## Deployment Process Details

### Phase 1: Discovery
- Script scans specified network range
- Attempts to find devices using:
  - Direct IP addresses (if provided)
  - Hostname resolution (e.g., `device.local`)
  - Common naming patterns

### Phase 2: Compilation
For each discovered device:
1. **Backup Original**: Save current `src/credentials.h`
2. **Generate Device-Specific**: Create credentials with device's serial number
3. **Compile**: Use PlatformIO to build firmware for specific board type
4. **Store Firmware**: Save compiled binary in `firmware_compiled/{serial_number}/`
5. **Restore Original**: Restore original source files

### Phase 3: Deployment
- Use PlatformIO OTA to upload firmware to each device
- Deploy to multiple devices in parallel (configurable concurrency)
- Report success/failure for each device

### Phase 4: Verification
- Check deployment success
- Generate summary report
- Clean up temporary files

## Understanding the Output

### Successful Deployment
```
[OK] Local Bulk OTA Deployer initialized
[NET] Network range: 192.168.68.0/24
[OK] Added device: TL1079927967 (ESP32_THREE_PORT) -> v1.58
[OK] Device TL1079927967 discovered at 192.168.68.150
[COMPILE] Compiling device-specific firmware for TL1079927967...
[OK] Device-specific firmware compiled: firmware_compiled\TL1079927967\firmware.bin
[OTA] Starting PlatformIO OTA deployment to TL01.local (192.168.68.150)...
[OK] Successfully deployed to TL01.local (192.168.68.150)
[SUCCESS] 1 device(s) updated successfully!
```

### Common Issues

| Message | Meaning | Action |
|---------|---------|--------|
| `[ERROR] Could not discover device` | Device not found on network | Check device is online, verify network range |
| `[ERROR] PlatformIO not found` | PlatformIO not installed | Install: `pip install platformio` |
| `[ERROR] Compilation failed` | Source code issues | Check code compiles: `pio run -e esps3_board` |
| `[WARN] Cannot ping direct IP` | Device unreachable at listed IP | Device may have different IP or be offline |

## Troubleshooting

### Device Discovery Issues

1. **Check Device Status**
   ```bash
   ping device.local
   ping 192.168.68.150
   ```

2. **Scan Network**
   ```bash
   python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --discover-only
   ```

3. **Try Different Network Ranges**
   - Check your router's DHCP range
   - Try `/24` networks: `192.168.1.0/24`, `10.0.0.0/24`

### Compilation Issues

1. **Test PlatformIO**
   ```bash
   pio --version
   pio run -e esps3_board
   ```

2. **Check Source Code**
   - Ensure `src/credentials.h` exists
   - Verify no syntax errors in main code

### Upload Issues

1. **Manual OTA Test**
   ```bash
   pio run -e esps3_board -t upload --upload-port device.local
   ```

2. **Device Requirements**
   - Device must be awake (not in deep sleep)
   - ArduinoOTA must be enabled
   - Sufficient free memory for OTA

## Security Considerations

### Credentials Management
- Firebase credentials are embedded in compiled firmware
- Each device gets its unique serial number
- Original source files are always restored after compilation

### Network Security
- OTA updates occur over local network only
- No external internet access required for local deployments
- Devices must be on trusted network

### Access Control
- Physical access to deployment computer required
- Device-specific firmware prevents identity theft
- Firebase documents remain segregated by serial number

## Maintenance

### Regular Tasks
1. **Update CSV files** when adding/removing devices
2. **Increment version numbers** in `src/Version.txt` for new releases
3. **Clean up compiled firmware** in `firmware_compiled/` periodically
4. **Test discovery** before major deployments

### Backup Procedures
- Source code is automatically backed up during compilation
- Device CSV files should be version controlled
- Compiled firmware binaries can be archived for rollback

## Integration with Firebase

### Device Tracking
- Each device maintains its original serial number
- Firebase documents remain accessible under existing IDs
- Device status and telemetry continue to work normally

### Version Management
- Devices report their firmware version to Firebase
- OTA status tracking remains functional
- Remote monitoring of deployment success possible

## Performance

### Deployment Speed
- **Single device**: ~30-60 seconds (compilation + upload)
- **Multiple devices**: Parallel deployment (configurable concurrency)
- **Network dependency**: Local network speed affects upload times

### Resource Usage
- **Disk space**: ~2MB per compiled firmware binary
- **Memory**: Minimal during deployment
- **CPU**: High during compilation phase

## Best Practices

### Pre-Deployment
1. **Test with single device** before bulk deployment
2. **Use dry-run mode** to verify device discovery
3. **Check firmware version** in `src/Version.txt`
4. **Verify CSV file format** and device information

### During Deployment
1. **Monitor output** for errors or warnings
2. **Don't interrupt** compilation or upload processes
3. **Ensure stable network** connection
4. **Keep devices powered** during update

### Post-Deployment
1. **Verify device status** in Firebase
2. **Check device functionality** remotely
3. **Clean up** compiled firmware files if desired
4. **Document** deployment results

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025-09-26 | Initial device-specific compilation system |
| | | Fixed serial number conflicts in bulk deployments |
| | | Added local network OTA deployment |
| | | Integrated PlatformIO compilation pipeline |

## Support

### Common Commands for Support
```bash
# Check system status
pio --version
python --version

# Test device discovery
python local_bulk_ota_deploy.py --csv devices.csv --discover-only

# Test single device
python local_bulk_ota_deploy.py --csv single_device.csv --dry-run

# Manual firmware compilation
pio run -e esps3_board
pio run -e esp32dev_3port
```

### Log Files
- PlatformIO compilation logs in terminal output
- Device discovery results in script output
- Firebase connectivity logs in device serial output

---

**Last Updated**: September 26, 2025
**Author**: SkyAcres Development Team
**Version**: 1.0