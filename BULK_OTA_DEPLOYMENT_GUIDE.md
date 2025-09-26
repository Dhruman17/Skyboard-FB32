# Bulk OTA Deployment Guide

## Overview
This guide explains how to perform bulk Over-The-Air (OTA) firmware updates on ESP32 devices in the SkyAcres system using the local deployment scripts.

## Prerequisites
- Python 3.7 or higher
- All devices must be on the same local network
- PlatformIO or Arduino IDE with ESP32 board support
- Compiled firmware binaries from PlatformIO builds

## Setup

### 1. Install Dependencies
```bash
pip install -r requirements_local.txt
```

### 2. Prepare Your Environment
Create a `.env` file (use `.env.example` as template) with your configuration:
```bash
cp .env.example .env
```

### 3. Prepare Firmware Files

The deployment script expects firmware files to be organized in this structure:
```
firmware/
├── 1.57/                    # Target version
│   ├── ESP32_S3/
│   │   └── firmware.bin
│   └── ESP32_THREE_PORT/
│       └── firmware.bin
└── 1.58/                    # Another version
    ├── ESP32_S3/
    │   └── firmware.bin
    └── ESP32_THREE_PORT/
        └── firmware.bin
```

#### From PlatformIO Builds

Your compiled firmware is located in:
- **ESP32_S3 boards**: `.pio/build/esps3_board/firmware.bin`
- **ESP32_THREE_PORT boards**: `.pio/build/esp32dev_3port/firmware.bin`

**Create the firmware directory structure:**

```bash
# Create firmware directory structure
mkdir -p firmware/1.57/ESP32_S3
mkdir -p firmware/1.57/ESP32_THREE_PORT

# Copy firmware files from PlatformIO builds
copy .pio\build\esps3_board\firmware.bin firmware\1.57\ESP32_S3\firmware.bin
copy .pio\build\esp32dev_3port\firmware.bin firmware\1.57\ESP32_THREE_PORT\firmware.bin
```

**For different versions, create additional directories:**
```bash
mkdir -p firmware/1.58/ESP32_S3
mkdir -p firmware/1.58/ESP32_THREE_PORT
# Copy new firmware versions...
```

#### Automated Firmware Setup Script

You can create a batch script to automate this process. Create `setup_firmware.bat`:

```batch
@echo off
set VERSION=%1
if "%VERSION%"=="" (
    echo Usage: setup_firmware.bat [version]
    echo Example: setup_firmware.bat 1.57
    exit /b 1
)

echo Setting up firmware structure for version %VERSION%...

mkdir firmware\%VERSION%\ESP32_S3 2>nul
mkdir firmware\%VERSION%\ESP32_THREE_PORT 2>nul

if exist .pio\build\esps3_board\firmware.bin (
    copy .pio\build\esps3_board\firmware.bin firmware\%VERSION%\ESP32_S3\firmware.bin
    echo ✅ Copied ESP32_S3 firmware
) else (
    echo ❌ ESP32_S3 firmware not found in .pio\build\esps3_board\
)

if exist .pio\build\esp32dev_3port\firmware.bin (
    copy .pio\build\esp32dev_3port\firmware.bin firmware\%VERSION%\ESP32_THREE_PORT\firmware.bin
    echo ✅ Copied ESP32_THREE_PORT firmware
) else (
    echo ❌ ESP32_THREE_PORT firmware not found in .pio\build\esp32dev_3port\
)

echo Done! Firmware structure ready for version %VERSION%
```

**Usage:**
```bash
setup_firmware.bat 1.57
```

## Deployment Process

### Step 1: Prepare the Deployment CSV File

The deployment is controlled by a CSV file (`test_local_deployment.csv`) with the following columns:

| Column | Description | Required | Example |
|--------|-------------|----------|---------|
| `serial_number` | Unique device identifier | Yes | `TL1079927967` |
| `board_type` | ESP32 board type | Yes | `ESP32_S3` or `ESP32_THREE_PORT` |
| `target_version` | Firmware version to deploy | Yes | `1.57` |
| `deployment_group` | Group name for batch deployment | Yes | `testing` |
| `notes` | Additional notes about the device | No | `New board system` |
| `system_name` | Human-readable system identifier | Yes | `TL01` |
| `ip_address` | Device IP (auto-discovered if empty) | No | `192.168.1.100` |

#### Adding a New Device to Deployment

1. **Open the CSV file**: `test_local_deployment.csv`
2. **Add a new row** with the device information:
   ```csv
   serial_number,board_type,target_version,deployment_group,notes,system_name,ip_address
   TL1079927967,ESP32_THREE_PORT,1.57,testing,New board system,TL01,
   ```

3. **Save the file**

### Step 2: Device Discovery

Before deployment, discover all devices on your network:

```bash
python find_device.py
```

Or use the simple version:
```bash
python simple_find_device.py
```

This will scan your network and identify ESP32 devices that are ready for OTA updates.

### Step 3: Test Connection

Test connection to specific devices:

```bash
python test_connection.py
```

### Step 4: Run Bulk Deployment

Execute the bulk deployment script:

```bash
python local_bulk_ota_deploy.py --csv test_local_deployment.csv --firmware-dir ./firmware
```

#### Command Options:
- `--csv`: Path to your deployment CSV file
- `--firmware-dir`: Directory containing firmware files (default: ./firmware)
- `--group`: Deploy only to specific deployment group (optional)
- `--network`: Network range to scan (default: 192.168.1.0/24)
- `--ota-port`: ArduinoOTA port (default: 3232)
- `--max-concurrent`: Maximum concurrent deployments (default: 5)
- `--discover-only`: Only discover devices, don't deploy
- `--dry-run`: Show what would be deployed without actually deploying

#### Example Commands:

Deploy to all devices in CSV:
```bash
python local_bulk_ota_deploy.py --csv test_local_deployment.csv --firmware-dir ./firmware
```

Deploy only to testing group:
```bash
python local_bulk_ota_deploy.py --csv test_local_deployment.csv --firmware-dir ./firmware --group testing
```

Discover devices only (no deployment):
```bash
python local_bulk_ota_deploy.py --csv test_local_deployment.csv --discover-only
```

Dry run to see what would be deployed:
```bash
python local_bulk_ota_deploy.py --csv test_local_deployment.csv --firmware-dir ./firmware --dry-run
```

### Step 5: Monitor Deployment

The script will:
1. 🔍 Scan the network for devices
2. 📋 Read the deployment CSV file
3. ✅ Validate device entries
4. 🚀 Deploy firmware to each device
5. 📊 Report deployment results

## Adding New Systems

### Quick Steps to Add a New Device:

1. **Get the device information:**
   - Serial number (from device or system)
   - System name (your choice)
   - Board type (ESP32_S3 or ESP32_THREE_PORT)

2. **Add to CSV file:**
   ```csv
   NEW_SERIAL_NUMBER,ESP32_S3,1.57,production,Description,SYSTEM_NAME,
   ```

3. **Run deployment:**
   ```bash
   python local_bulk_ota_deploy.py --csv test_local_deployment.csv --firmware-dir ./firmware
   ```

### Example: Adding Systems

```csv
TL1079927967,ESP32_THREE_PORT,1.57,testing,New board system,TL01,
MR6384439627,ESP32_S3,1.57,testing,S3 board system,MR1,
```

## Troubleshooting

### Common Issues:

1. **Device not found**:
   - Ensure device is on same network
   - Check if device is in OTA mode
   - Verify network range in script

2. **Connection timeout**:
   - Check firewall settings
   - Verify OTA port (default: 3232) is open
   - Ensure device is not busy

3. **CSV validation errors**:
   - Check all required fields are filled
   - Verify board_type matches allowed values
   - Ensure no extra spaces in entries

### Log Files

Deployment logs are automatically saved with timestamps for troubleshooting and audit purposes.

## Advanced Usage

### Single Device Deployment

For single device updates, use:
```bash
python simple_ota_deploy.py
```

### Custom Network Range

If your devices are on a different subnet:
```bash
python local_bulk_ota_deploy.py --network 10.0.1.0/24 --csv test_local_deployment.csv --firmware-dir ./firmware
```

## Security Notes

- Ensure you're on a secure network when performing OTA updates
- Verify firmware binary integrity before deployment
- Keep deployment logs for audit trail
- Use deployment groups to stage updates safely

## File Structure

```
├── local_bulk_ota_deploy.py    # Main bulk deployment script
├── find_device.py              # Network device discovery
├── simple_find_device.py       # Simple device discovery
├── simple_ota_deploy.py        # Single device deployment
├── test_connection.py          # Connection testing
├── test_local_deployment.csv   # Deployment configuration
├── requirements_local.txt      # Python dependencies
└── .env.example               # Environment configuration template
```