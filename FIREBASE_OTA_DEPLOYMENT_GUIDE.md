# Firebase OTA Deployment Guide

Complete guide for deploying firmware to ESP32 devices using local OTA and Firebase Storage.

## Overview

The `local_bulk_ota_deploy.py` script now supports three deployment modes:

1. **Local OTA Only** - Deploy firmware directly to devices on your network (original functionality)
2. **Local OTA + Firebase Upload** - Deploy locally AND upload to Firebase for remote devices
3. **Firebase Upload Only** - Compile and upload to Firebase without local deployment

## Prerequisites

### 1. Install Dependencies

```bash
pip install firebase-admin google-cloud-storage google-auth
```

### 2. Authenticate with Firebase

```bash
gcloud auth application-default login
```

This will authenticate you with Google Cloud and allow the script to upload to Firebase Storage.

See `FIREBASE_SETUP.md` for detailed authentication setup.

## Usage Examples

### Mode 1: Local OTA Deployment Only (Default)

Deploy firmware directly to devices on your local network:

```bash
python3 local_bulk_ota_deploy.py --csv test_local_deployment.csv --yes
```

This will:
- ✅ Discover devices on your network
- ✅ Compile device-specific firmware for each device
- ✅ Deploy via local OTA to each device
- ❌ NOT upload to Firebase

### Mode 2: Local OTA + Firebase Upload

Deploy locally AND upload to Firebase Storage for remote OTA:

```bash
python3 local_bulk_ota_deploy.py --csv test_local_deployment.csv --upload-to-firebase --yes
```

This will:
- ✅ Discover devices on your network
- ✅ Compile device-specific firmware for each device
- ✅ Deploy via local OTA to each device
- ✅ Upload firmware.bin and version.txt to Firebase Storage

**Use this when:**
- You want to update devices on your network immediately
- You also want to make firmware available in Firebase for remote devices

### Mode 3: Firebase Upload Only

Compile and upload to Firebase without local deployment:

```bash
python3 local_bulk_ota_deploy.py --csv test_local_deployment.csv --firebase-only --yes
```

This will:
- ✅ Compile device-specific firmware for each device
- ✅ Upload firmware.bin and version.txt to Firebase Storage
- ❌ NOT deploy via local OTA

**Use this when:**
- Devices are not on your network
- You want to prepare firmware for remote OTA updates
- You want to stage firmware updates before devices check for updates

## Firebase Storage Structure

Firmware files are organized by serial number in Firebase Storage:

```
skyacres-marketplace.appspot.com/
├── 123456789123456789/
│   ├── firmware.bin
│   └── version.txt
├── 987654321987654321/
│   ├── firmware.bin
│   └── version.txt
└── ...
```

Each device will:
1. Check its own folder (based on serial number)
2. Download `version.txt` to check if update is available
3. If newer version available, download `firmware.bin`
4. Apply the update and reboot

## Command-Line Options

### Basic Options

| Option | Description |
|--------|-------------|
| `--csv CSV` | Path to deployment CSV file (required) |
| `--firmware-dir DIR` | Directory for firmware files (default: ./firmware) |
| `--network RANGE` | Network range to scan (default: 192.168.1.0/24) |
| `--ota-port PORT` | ArduinoOTA port (default: 3232) |
| `--group GROUP` | Deploy only to specific deployment group |
| `--max-concurrent N` | Maximum concurrent deployments (default: 5) |
| `--yes, -y` | Skip confirmation prompts |

### Firebase Options

| Option | Description |
|--------|-------------|
| `--upload-to-firebase` | Upload to Firebase after local OTA deployment |
| `--firebase-only` | Only upload to Firebase, skip local OTA |
| `--firebase-project PROJECT` | Firebase project ID (default: skyacres-marketplace) |

### Utility Options

| Option | Description |
|--------|-------------|
| `--discover-only` | Only discover devices, don't deploy |
| `--dry-run` | Show what would be deployed without deploying |

## CSV File Format

Your CSV file should contain:

```csv
serial_number,board_type,target_version,system_name,ip_address,deployment_group
123456789123456789,ESP32_S3,2.1.0,skyacres-001,192.168.1.100,production
987654321987654321,ESP32_THREE_PORT,2.1.0,skyacres-002,192.168.1.101,production
```

**Required fields:**
- `serial_number` - Unique device serial number
- `board_type` - Either `ESP32_S3` or `ESP32_THREE_PORT`
- `target_version` - Firmware version to deploy

**Optional fields:**
- `system_name` - mDNS hostname for device discovery
- `ip_address` - Direct IP address (faster than mDNS)
- `deployment_group` - Group name for selective deployment

## Workflow Examples

### Scenario 1: Update All Local Devices

```bash
# Deploy to all devices on your network
python3 local_bulk_ota_deploy.py --csv deployment.csv --yes
```

### Scenario 2: Update Local + Prepare for Remote Devices

```bash
# Deploy locally AND upload to Firebase
python3 local_bulk_ota_deploy.py --csv deployment.csv --upload-to-firebase --yes
```

### Scenario 3: Prepare Firmware for Remote Devices

```bash
# Only upload to Firebase (no local deployment)
python3 local_bulk_ota_deploy.py --csv deployment.csv --firebase-only --yes
```

### Scenario 4: Deploy to Specific Group

```bash
# Deploy only to devices in "test" group
python3 local_bulk_ota_deploy.py --csv deployment.csv --group test --upload-to-firebase --yes
```

### Scenario 5: Dry Run Before Deployment

```bash
# See what would be deployed without actually deploying
python3 local_bulk_ota_deploy.py --csv deployment.csv --dry-run
```

## Output and Logging

### Local OTA Deployment Output

```
[OK] Local Bulk OTA Deployer initialized
[NET] Network range: 192.168.1.0/24
[PORT] OTA port: 3232
[FIREBASE] Firebase Storage client initialized for project: skyacres-marketplace

[DISCOVER] Discovering 2 devices on network...
[FIND] Testing direct IP 192.168.1.100 for 123456789123456789...
[OK] Device reachable at direct IP 192.168.1.100

[DEPLOY] Starting local bulk deployment...
[DEVICE] Compiling firmware for 123456789123456789...
[COMPILE] Compiling device-specific firmware for 123456789123456789...
[OK] Device-specific firmware compiled: firmware_compiled/123456789123456789/firmware.bin
[VERSION] Created version.txt: 2.1.0
[OTA] Uploading device-specific firmware to 192.168.1.100...
[OK] Successfully deployed device-specific firmware to skyacres-001.local (192.168.1.100)
[FIREBASE] Uploading firmware for 123456789123456789 to Firebase Storage...
✓ Uploaded firmware.bin for 123456789123456789
✓ Uploaded version.txt for 123456789123456789
[FIREBASE] Successfully uploaded firmware for 123456789123456789

==================================================
[SUMMARY] LOCAL DEPLOYMENT SUMMARY
==================================================
Devices in CSV: 2
Devices discovered: 2
[OK] Successful OTA deployments: 2
[ERROR] Failed OTA deployments: 0

[FIREBASE] Firebase Storage Upload Summary:
[OK] Successful uploads: 2
[ERROR] Failed uploads: 0
[TIME] Completed at: 2025-10-07 14:32:15

[SUCCESS] 2 device(s) updated successfully!
```

### Firebase-Only Upload Output

```
[FIREBASE] Firebase-only mode: Compiling and uploading firmware to Firebase Storage

[DEVICE] Processing 123456789123456789...
[COMPILE] Compiling device-specific firmware for 123456789123456789...
[OK] Device-specific firmware compiled: firmware_compiled/123456789123456789/firmware.bin
[VERSION] Created version.txt: 2.1.0
[FIREBASE] Uploading firmware for 123456789123456789 to Firebase Storage...
✓ Uploaded firmware.bin for 123456789123456789
✓ Uploaded version.txt for 123456789123456789

==================================================
[SUMMARY] FIREBASE UPLOAD SUMMARY
==================================================
Total devices: 2
[OK] Successful uploads: 2
[ERROR] Failed uploads: 0
[TIME] Completed at: 2025-10-07 14:35:22
```

## Troubleshooting

### Error: "Firebase Storage uploader not available"

**Problem:** Required Python packages not installed

**Solution:**
```bash
pip install firebase-admin google-cloud-storage
```

### Error: "Could not automatically determine credentials"

**Problem:** Not authenticated with Google Cloud

**Solution:**
```bash
gcloud auth application-default login
```

### Error: "Permission denied" when uploading

**Problem:** Service account lacks Storage permissions

**Solution:** Grant Storage Object Admin role in IAM Console

See `FIREBASE_SETUP.md` for detailed troubleshooting.

### Device Not Discovered

**Problem:** Device not found on network

**Solutions:**
- Add direct IP address to CSV file (`ip_address` column)
- Check device is powered on and connected to WiFi
- Verify device is on same network
- Check firewall settings

### Compilation Failed

**Problem:** PlatformIO compilation error

**Solutions:**
- Verify PlatformIO is installed: `pio --version`
- Check `board_type` in CSV is correct
- Review compilation errors in output
- Ensure `src/credentials.h` exists

## Device-Side Implementation

For devices to check and download firmware from Firebase:

1. Device periodically checks Firebase Storage for its serial number folder
2. Downloads `version.txt` and compares with current version
3. If newer version available, downloads `firmware.bin`
4. Uses ESP32 OTA update mechanism to apply firmware
5. Reboots with new firmware

See your ESP32 firmware code in `src/firebase_coms.h` for the device-side OTA implementation.

## Security Best Practices

✅ **No service account keys** - Uses Application Default Credentials
✅ **Short-lived tokens** - Auto-rotate every hour
✅ **Device-specific firmware** - Each device gets firmware compiled with its unique serial number
✅ **Version validation** - Devices check version before updating
✅ **Secure storage** - Firebase Storage with proper IAM permissions

## Advanced Usage

### Parallel Processing

Adjust concurrent deployments for faster processing:

```bash
# Deploy to 10 devices simultaneously
python3 local_bulk_ota_deploy.py --csv deployment.csv --max-concurrent 10 --yes
```

### Selective Deployment by Group

Deploy only to specific device groups:

```bash
# Deploy only to "production" group
python3 local_bulk_ota_deploy.py --csv deployment.csv --group production --yes

# Deploy only to "testing" group
python3 local_bulk_ota_deploy.py --csv deployment.csv --group testing --upload-to-firebase --yes
```

### Staged Rollout

1. **Stage 1:** Test with small group
```bash
python3 local_bulk_ota_deploy.py --csv deployment.csv --group test --upload-to-firebase --yes
```

2. **Stage 2:** Deploy to production
```bash
python3 local_bulk_ota_deploy.py --csv deployment.csv --group production --upload-to-firebase --yes
```

## Summary

| Command | Local OTA | Firebase Upload | Use Case |
|---------|-----------|-----------------|----------|
| `--csv file.csv` | ✅ | ❌ | Update local devices only |
| `--csv file.csv --upload-to-firebase` | ✅ | ✅ | Update local + prepare remote |
| `--csv file.csv --firebase-only` | ❌ | ✅ | Prepare for remote OTA only |

Choose the mode that best fits your deployment scenario!
