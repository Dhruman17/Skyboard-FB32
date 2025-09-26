# Bulk OTA Deployment System

This system allows you to deploy firmware updates to multiple ESP32 devices simultaneously using Firebase Storage and Firestore.

## ⚠️ **IMPORTANT UPDATE** ⚠️

**For Local Network Deployments**, use the new enhanced system documented in [`DEPLOYMENT_GUIDE.md`](./DEPLOYMENT_GUIDE.md). This new system:

✅ **Compiles device-specific firmware** to preserve unique serial numbers
✅ **Prevents identity conflicts** that cause devices to go offline
✅ **Maintains Firebase tracking** for all devices
✅ **Uses local network OTA** via PlatformIO

**Command for local deployments:**
```bash
python local_bulk_ota_deploy.py --csv deployment_list.csv --network 192.168.68.0/24 --yes
```

**This document below describes the Firebase-based remote deployment system.**

## Prerequisites

1. **Python 3.7+** with pip installed
2. **Firebase Service Account Key** with admin privileges
3. **Compiled firmware binaries** for each board type

## Setup

### 1. Install Python Dependencies
```bash
pip install -r requirements.txt
```

### 2. Firebase Service Account Setup

1. Go to [Firebase Console](https://console.firebase.google.com/)
2. Select your project (skyacres-marketplace)
3. Go to Project Settings > Service Accounts
4. Click "Generate new private key"
5. Save the JSON file securely (e.g., `firebase-service-account-key.json`)

⚠️ **Never commit this key to version control!**

### 3. Prepare Firmware Binaries

Build firmware for your target version and board types:

```bash
# Build ESP32_THREE_PORT firmware
pio run -e esp32dev_3port
cp .pio/build/esp32dev_3port/firmware.bin firmware/1.570/ESP32_THREE_PORT/

# Build ESP32_S3 firmware
pio run -e esps3_board
cp .pio/build/esps3_board/firmware.bin firmware/1.570/ESP32_S3/
```

## Usage

### 1. Create Deployment CSV

Create a CSV file with your deployment targets:

```csv
serial_number,board_type,target_version,deployment_group,notes
123456789123456789,ESP32_THREE_PORT,1.570,testing,Test system for validation
SME103231417,ESP32_THREE_PORT,1.570,production,Field unit A1
SME103231418,ESP32_S3,1.570,production,Lab unit B2
```

### 2. Test Deployment (Dry Run)

```bash
python bulk_ota_deploy.py \
  --csv test_deployment.csv \
  --service-account firebase-service-account-key.json \
  --dry-run
```

### 3. Deploy to Single System (Testing)

```bash
python bulk_ota_deploy.py \
  --csv test_deployment.csv \
  --service-account firebase-service-account-key.json
```

### 4. Deploy to Specific Group

```bash
python bulk_ota_deploy.py \
  --csv deployment_list.csv \
  --service-account firebase-service-account-key.json \
  --group testing
```

### 5. Monitor Deployment Status

```bash
python bulk_ota_deploy.py \
  --csv test_deployment.csv \
  --service-account firebase-service-account-key.json \
  --monitor
```

## How It Works

### Device Side (No Changes Needed)
Your existing OTA implementation (`main.cpp:641-697`) automatically:
1. Checks for updates every 6 hours
2. Compares Firestore version vs Storage version
3. Downloads and installs if Storage version is newer
4. Updates Firestore version after successful installation

### Deployment Script Process
1. **Reads CSV** and validates device data
2. **Uploads firmware** to Firebase Storage at `{serial_number}/firmware.bin`
3. **Creates Version.txt** at `{serial_number}/Version.txt` with target version
4. **Updates Firestore** with deployment metadata (without changing version field)
5. **Device detects** version mismatch and initiates OTA update

### Timeline Example
```
T+0min: Script uploads firmware and version files
T+0min: Device still running v1.569 (Firestore version: 1.569)
T+Xmin: Device checks for updates (within 6 hours)
T+Xmin: Device finds Storage version 1.570 > Firestore version 1.569
T+Xmin: Device downloads and installs firmware
T+Xmin: Device updates Firestore version to 1.570
T+Xmin: Device restarts with new firmware
```

## Monitoring and Troubleshooting

### Check Deployment Status
Monitor device `lastSeen` timestamps to verify successful updates:

```bash
python bulk_ota_deploy.py --csv test_deployment.csv --service-account firebase-service-account-key.json --monitor
```

### Common Issues

1. **"Firmware not found"** - Ensure firmware.bin exists in correct directory structure
2. **"Service account permission denied"** - Verify service account has Firestore/Storage admin roles
3. **"Device not updating"** - Check device WiFi connection and Firebase connectivity

### Verification Steps

1. **Before deployment**: Device shows old version in Firestore
2. **After deployment**: Storage files uploaded, Firestore shows `ota_status: pending`
3. **After device update**: Device shows new version, `lastSeen` updated recently

## Security Notes

- Store service account key securely
- Use deployment groups for staged rollouts
- Test with single device before bulk deployment
- Monitor device heartbeats for successful updates

## Test System Details

**Serial Number**: `123456789123456789`
**Board Type**: `ESP32_THREE_PORT`
**Current Version**: Check Firestore document at `Systems/123456789123456789`

This system is designed for the test deployment to validate the bulk OTA process before rolling out to production devices.