# SkyAcres ESP32 OTA Update System - Complete Guide

## Table of Contents
1. [System Overview](#system-overview)
2. [OTA Architecture](#ota-architecture)
3. [Deployment Methods](#deployment-methods)
4. [Firebase Storage Upload System](#firebase-storage-upload-system)
5. [Device-Side OTA Implementation](#device-side-ota-implementation)
6. [Setup and Configuration](#setup-and-configuration)
7. [Deployment Procedures](#deployment-procedures)
8. [Troubleshooting](#troubleshooting)
9. [Best Practices](#best-practices)
10. [Security Considerations](#security-considerations)

---

## System Overview

The SkyAcres ESP32 OTA (Over-The-Air) update system provides comprehensive firmware management for ESP32 devices with three primary update methods:

### Update Methods
1. **Local Network OTA** - Direct updates via ArduinoOTA on local network
2. **Firebase Storage OTA** - Cloud-based updates via Firebase Storage
3. **Serial Upload** - Traditional USB cable upload (fallback method)

### Key Features
- ✅ **Device-specific firmware compilation** - Preserves unique serial numbers
- ✅ **Secure cloud distribution** - Firebase Storage without service account keys
- ✅ **Automatic version checking** - Devices self-update when new firmware available
- ✅ **Memory-aware updates** - OTA only when sufficient memory available (>50KB)
- ✅ **Rollback protection** - Validates firmware before committing
- ✅ **Parallel deployment** - Update multiple devices simultaneously
- ✅ **Network discovery** - Automatic device detection on local networks

---

## OTA Architecture

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│                    Deployment System                         │
├─────────────────────────────────────────────────────────────┤
│  • firebase_storage_uploader.py  (Cloud uploads)             │
│  • local_bulk_ota_deploy.py     (Local network deployment)  │
│  • Device CSV files              (Deployment targets)        │
│  • PlatformIO                    (Firmware compilation)      │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                   Distribution Layer                         │
├─────────────────────────────────────────────────────────────┤
│  • Firebase Storage      (Cloud firmware storage)            │
│  • Local Network         (ArduinoOTA protocol)               │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    ESP32 Devices                             │
├─────────────────────────────────────────────────────────────┤
│  • ArduinoOTA           (Local network updates)              │
│  • HTTPClient           (Firebase downloads)                 │
│  • Update.h             (Firmware installation)              │
│  • Memory Manager       (Resource monitoring)                │
└─────────────────────────────────────────────────────────────┘
```

### Data Flow

#### Firebase Storage OTA Flow
```
Developer → Upload Firmware → Firebase Storage → Device Checks → Download → Validate → Install → Restart
```

#### Local Network OTA Flow
```
Developer → Compile Firmware → ArduinoOTA → Device Receives → Validate → Install → Restart
```

---

## Deployment Methods

### Method 1: Local Network OTA (Recommended for Development)

**When to use:**
- Devices are on the same local network
- Fast deployment needed
- Development/testing phase
- Direct access to devices

**Advantages:**
- ✅ Fastest update method
- ✅ No cloud dependencies
- ✅ Device-specific firmware compilation
- ✅ Immediate feedback

**Command:**
```bash
python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --yes
```

### Method 2: Firebase Storage OTA (Recommended for Production)

**When to use:**
- Devices deployed in field
- Remote locations without direct network access
- Scheduled/automatic updates
- Large-scale deployments

**Advantages:**
- ✅ Remote deployment capability
- ✅ Automatic device updates
- ✅ Version tracking
- ✅ Centralized firmware management
- ✅ Secure authentication (no service account keys)

**Command:**
```bash
python firebase_storage_uploader.py
```

### Method 3: Serial Upload (Fallback)

**When to use:**
- OTA not working
- Device recovery needed
- Initial firmware flash
- Debugging required

**Command:**
```bash
pio run -e esps3_board -t upload --upload-port COM3
```

---

## Firebase Storage Upload System

### Architecture

The Firebase Storage upload system uses **Application Default Credentials (ADC)** instead of service account keys, providing enhanced security and compliance with organizational policies.

### Authentication Setup

#### Step 1: Install Dependencies
```bash
pip install -r requirements.txt
```

Required packages:
- `firebase-admin>=6.2.0`
- `google-cloud-storage>=2.10.0`
- `google-auth>=2.23.0`
- `google-auth-oauthlib>=1.1.0`

#### Step 2: Authenticate with Google Cloud
```bash
gcloud auth application-default login
```

This command:
1. Opens browser for Google authentication
2. Saves credentials locally
3. Enables all Python scripts to use these credentials
4. Works for any Google Cloud service

#### Step 3: Set Project
```bash
gcloud auth application-default set-quota-project skyacres-marketplace
```

### Upload Module: `firebase_storage_uploader.py`

#### Core Classes

**`SecureFirebaseClient`** - Primary upload interface
```python
from firebase_storage_uploader import SecureFirebaseClient

client = SecureFirebaseClient(project_id='skyacres-marketplace')
```

**`GCSDirectUploader`** - Alternative using Google Cloud Storage directly
```python
from firebase_storage_uploader import GCSDirectUploader

client = GCSDirectUploader(bucket_name='skyacres-marketplace.appspot.com')
```

### Usage Examples

#### Single Device Upload
```python
from firebase_storage_uploader import SecureFirebaseClient

client = SecureFirebaseClient(project_id='skyacres-marketplace')

client.upload_firmware_for_device(
    serial_number='TL1079927967',
    firmware_path='.pio/build/TL1079927967/firmware.bin',
    version_txt_path='version.txt'
)
```

#### Bulk Device Upload
```python
devices = {
    'TL1079927967': {
        'firmware': '.pio/build/TL1079927967/firmware.bin',
        'version': 'version.txt'
    },
    'MR6384439627': {
        'firmware': '.pio/build/MR6384439627/firmware.bin',
        'version': 'version.txt'
    }
}

success_count, failed_devices = client.bulk_upload(devices)
```

#### Command Line Usage
```bash
# Single device
python example_firebase_upload.py single

# Bulk upload
python example_firebase_upload.py bulk

# Upload from CSV
python example_firebase_upload.py csv
```

### Firebase Storage Structure

Firmware is organized by device serial number:
```
skyacres-marketplace.appspot.com/
├── TL1079927967/
│   ├── firmware.bin
│   └── version.txt
├── MR6384439627/
│   ├── firmware.bin
│   └── version.txt
└── [SERIAL_NUMBER]/
    ├── firmware.bin
    └── version.txt
```

### Security Features

✅ **No service account keys** - Uses Application Default Credentials
✅ **Short-lived tokens** - Automatically refresh every hour
✅ **Least privilege** - Only requires Storage Object Admin role
✅ **Audit logging** - All operations logged in Cloud Audit Logs
✅ **Works everywhere** - Local dev, GCP environments, CI/CD

---

## Device-Side OTA Implementation

### Arduino OTA (Local Network Updates)

#### Setup in `main.cpp`
```cpp
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

void setupOTA() {
    // Set hostname for mDNS
    String hostname = systemName + "_" + DEVICE_SERIAL_NUMBER;
    ArduinoOTA.setHostname(hostname.c_str());

    // Configure OTA callbacks
    ArduinoOTA.onStart([]() {
        Serial.println("OTA Update Starting...");
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA Update Complete!");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
    });

    ArduinoOTA.begin();
    Serial.println("ArduinoOTA ready");
}

void loop() {
    ArduinoOTA.handle(); // Must be called in loop
}
```

### Firebase Storage OTA (Cloud Updates)

#### Firmware Check and Download
```cpp
#include <HTTPClient.h>
#include <Update.h>

void checkForFirmwareUpdate() {
    // Only check if sufficient memory
    if (ESP.getFreeHeap() < 50000) {
        Serial.println("Insufficient memory for OTA check");
        return;
    }

    // Download version.txt from Firebase Storage
    String versionUrl = "https://firebasestorage.googleapis.com/v0/b/skyacres-marketplace.appspot.com/o/"
                       + String(DEVICE_SERIAL_NUMBER) + "%2Fversion.txt?alt=media";

    HTTPClient http;
    http.begin(versionUrl);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String cloudVersion = http.getString();
        cloudVersion.trim();

        String currentVersion = VERSION; // From Version.txt

        if (cloudVersion != currentVersion) {
            Serial.println("New firmware available: " + cloudVersion);
            performOTAUpdate();
        }
    }
    http.end();
}

void performOTAUpdate() {
    String firmwareUrl = "https://firebasestorage.googleapis.com/v0/b/skyacres-marketplace.appspot.com/o/"
                        + String(DEVICE_SERIAL_NUMBER) + "%2Ffirmware.bin?alt=media";

    HTTPClient http;
    http.begin(firmwareUrl);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();

        if (!Update.begin(contentLength)) {
            Serial.println("Not enough space for OTA");
            http.end();
            return;
        }

        WiFiClient *stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);

        if (written == contentLength) {
            Serial.println("OTA update written successfully");
        }

        if (Update.end()) {
            if (Update.isFinished()) {
                Serial.println("OTA update completed successfully");
                ESP.restart();
            }
        }
    }
    http.end();
}
```

### Memory-Aware OTA

The system uses `memory_manager.h` to ensure OTA updates only occur when sufficient memory is available:

```cpp
#define MIN_HEAP_FOR_OTA 50000  // 50KB minimum

void safeOTACheck() {
    size_t freeHeap = ESP.getFreeHeap();

    if (freeHeap < MIN_HEAP_FOR_OTA) {
        Serial.printf("Skipping OTA check - Low memory: %d bytes\n", freeHeap);
        return;
    }

    checkForFirmwareUpdate();
}
```

### Update Schedule

Devices check for updates:
- **On startup** - First boot after power on
- **Periodic checks** - Every 6 hours during operation
- **Manual trigger** - Via Firebase command
- **Memory permitting** - Only when >50KB free heap

---

## Setup and Configuration

### Prerequisites

#### Software Requirements
- **Python 3.7+** with pip
- **PlatformIO** (`pip install platformio`)
- **Google Cloud SDK** (`gcloud`)
- **Git** for version control

#### Hardware Requirements
- **ESP32 devices** with WiFi capability
- **Minimum 50KB free heap** for OTA updates
- **Network connectivity** (WiFi)

#### Cloud Requirements
- **Firebase project** (skyacres-marketplace)
- **Firebase Storage** enabled
- **Google Cloud authentication** configured

### Initial Setup

#### 1. Clone Repository
```bash
git clone <repository-url>
cd Skyboard-FB32
```

#### 2. Install Python Dependencies
```bash
pip install -r requirements.txt
pip install -r requirements_local.txt
```

#### 3. Authenticate with Google Cloud
```bash
gcloud auth application-default login
gcloud auth application-default set-quota-project skyacres-marketplace
```

#### 4. Verify Authentication
```bash
gcloud auth application-default print-access-token
```

You should see a token starting with `ya29...`

#### 5. Configure PlatformIO
Ensure `platformio.ini` has correct board definitions:
```ini
[env:esps3_board]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

[env:esp32dev_3port]
platform = espressif32
board = esp32dev
framework = arduino
```

#### 6. Set Device Credentials
Create/edit `src/credentials.h`:
```cpp
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"
#define FIREBASE_PROJECT_ID "skyacres-marketplace"
```

---

## Deployment Procedures

### Procedure 1: Local Network Deployment

#### Step 1: Prepare Device List
Create `devices.csv`:
```csv
serial_number,board_type,target_version,system_name,ip_address,deployment_group,notes
TL1079927967,ESP32_THREE_PORT,1.58,TL01,192.168.68.150,production,Main device
MR6384439627,ESP32_S3,1.58,MR1,192.168.68.139,production,Backup device
```

#### Step 2: Set Target Version
```bash
echo "1.58" > src/Version.txt
```

#### Step 3: Test Discovery
```bash
python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --discover-only
```

Expected output:
```
[OK] Local Bulk OTA Deployer initialized
[NET] Network range: 192.168.68.0/24
[DISC] Discovering devices...
[OK] Found device: TL01 at 192.168.68.150
[OK] Found device: MR1 at 192.168.68.139
[OK] Discovery complete: 2 devices found
```

#### Step 4: Deploy
```bash
python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --yes
```

#### Step 5: Monitor Output
Watch for:
- ✅ Compilation success for each device
- ✅ Upload progress percentage
- ✅ Device restart confirmation
- ❌ Any error messages

### Procedure 2: Firebase Storage Deployment

#### Step 1: Compile Device-Specific Firmware

For each device, compile with unique serial number:
```bash
# Set serial number in platformio.ini
pio run -e esps3_board

# Firmware saved to: .pio/build/esps3_board/firmware.bin
```

Or use device-specific build flags:
```ini
[env:TL1079927967]
build_flags =
    -D DEVICE_SERIAL_NUMBER=\"TL1079927967\"
    -D VERSION=\"1.58\"
```

#### Step 2: Prepare Upload Dictionary
```python
from firebase_storage_uploader import SecureFirebaseClient

client = SecureFirebaseClient(project_id='skyacres-marketplace')

devices = {
    'TL1079927967': {
        'firmware': '.pio/build/TL1079927967/firmware.bin',
        'version': 'version.txt'
    },
    'MR6384439627': {
        'firmware': '.pio/build/MR6384439627/firmware.bin',
        'version': 'version.txt'
    }
}
```

#### Step 3: Upload Firmware
```python
success_count, failed_devices = client.bulk_upload(devices)

print(f"Success: {success_count}")
print(f"Failed: {failed_devices}")
```

#### Step 4: Verify Upload
Check Firebase Console:
1. Go to [Firebase Console](https://console.firebase.google.com)
2. Select `skyacres-marketplace`
3. Navigate to Storage
4. Verify folders exist for each serial number
5. Check `firmware.bin` and `version.txt` files

#### Step 5: Trigger Device Update
Devices will automatically check for updates based on their schedule. To force immediate update:
- Restart device (power cycle or Firebase command)
- Device checks on startup
- Downloads and installs new firmware

### Procedure 3: Emergency Serial Upload

If OTA fails, use serial upload:

#### Step 1: Connect USB Cable
Connect ESP32 to computer via USB cable

#### Step 2: Identify COM Port
Windows:
```bash
# Check Device Manager > Ports (COM & LPT)
# Look for: Silicon Labs CP210x or CH340
```

Linux/Mac:
```bash
ls /dev/tty*
# Look for: /dev/ttyUSB0 or /dev/cu.usbserial-*
```

#### Step 3: Upload Firmware
```bash
pio run -e esps3_board -t upload --upload-port COM3
```

Replace `COM3` with your actual port.

---

## Troubleshooting

### Issue 1: Authentication Failed

**Symptom:**
```
✗ Failed to initialize Firebase: Could not automatically determine credentials
```

**Solution:**
```bash
# Re-authenticate
gcloud auth application-default login

# Verify token
gcloud auth application-default print-access-token

# Set correct project
gcloud auth application-default set-quota-project skyacres-marketplace
```

### Issue 2: Device Not Found on Network

**Symptom:**
```
[ERROR] Device not found: TL01
```

**Diagnostic Steps:**
```bash
# 1. Ping device by hostname
ping TL01.local

# 2. Ping device by IP
ping 192.168.68.150

# 3. Check device is on network
python local_bulk_ota_deploy.py --csv devices.csv --discover-only

# 4. Check OTA port is open
telnet 192.168.68.150 3232
```

**Common Causes:**
- Device powered off
- Device in deep sleep mode
- Wrong network subnet
- Firewall blocking mDNS or OTA port

### Issue 3: Insufficient Memory for OTA

**Symptom:**
```
Skipping OTA check - Low memory: 18432 bytes
```

**Solution:**
- Wait for device to free memory
- Memory manager will trigger cleanup
- Device will restart if critically low
- OTA will retry after restart

**Prevention:**
- Deploy during low-activity periods
- Optimize firmware to use less memory
- Enable PSRAM on ESP32-S3

### Issue 4: Compilation Failed

**Symptom:**
```
[ERROR] Compilation failed for device TL1079927967
```

**Diagnostic Steps:**
```bash
# 1. Test manual compilation
pio run -e esps3_board

# 2. Check for syntax errors
pio check

# 3. Clean and rebuild
pio run -t clean
pio run -e esps3_board
```

**Common Causes:**
- Missing dependencies
- Syntax errors in code
- Wrong board definition
- Insufficient disk space

### Issue 5: Firebase Upload Permission Denied

**Symptom:**
```
✗ Upload failed: 403 Forbidden
```

**Solution:**
```bash
# Grant Storage Object Admin role
gcloud projects add-iam-policy-binding skyacres-marketplace \
  --member="serviceAccount:firebase-adminsdk-ydv19@skyacres-marketplace.iam.gserviceaccount.com" \
  --role="roles/storage.objectAdmin"

# Wait 1-2 minutes for propagation
# Retry upload
```

### Issue 6: OTA Update Hangs at 50%

**Symptom:**
Device shows `Progress: 50%` but never completes

**Solution:**
- Check network stability
- Verify sufficient free heap (>50KB)
- Restart device and retry
- Use serial upload as fallback

**Prevention:**
- Ensure stable WiFi during updates
- Deploy during low network traffic
- Update in smaller batches

---

## Best Practices

### Development Phase

1. **Start with Serial Upload**
   - Initial firmware flash via USB
   - Verify OTA functionality
   - Test on single device first

2. **Use Local Network OTA**
   - Fast iteration during development
   - Immediate feedback
   - Easy debugging

3. **Test Discovery**
   - Always run `--discover-only` first
   - Verify all devices are reachable
   - Confirm correct network range

### Production Deployment

1. **Use Firebase Storage OTA**
   - Remote deployment capability
   - Automatic updates
   - Version control

2. **Stage Rollouts**
   - Use deployment groups
   - Update test group first
   - Monitor for 24-48 hours
   - Then update production

3. **Deploy During Low Activity**
   - Off-peak hours
   - When devices are idle
   - Maximum free memory available

4. **Monitor Memory**
   - Check heap before updates
   - Ensure >50KB free
   - Watch for memory leaks

### Version Management

1. **Semantic Versioning**
   - Use format: `MAJOR.MINOR.PATCH`
   - Example: `1.58.0`
   - Increment appropriately

2. **Version File**
   - Always update `src/Version.txt`
   - Commit version changes
   - Tag releases in Git

3. **Changelog**
   - Document changes in each version
   - Note breaking changes
   - List bug fixes

### Device-Specific Firmware

1. **Preserve Serial Numbers**
   - Each device needs unique firmware
   - Serial number compiled into binary
   - Prevents identity conflicts

2. **Organize Build Outputs**
   ```
   firmware_compiled/
   ├── TL1079927967/
   │   └── firmware.bin
   ├── MR6384439627/
   │   └── firmware.bin
   ```

3. **Verify Before Upload**
   - Check firmware size (typically 1-2MB)
   - Verify version string
   - Test on development device

### Network Configuration

1. **Document Network Layout**
   - Subnet ranges
   - Device IP addresses
   - Hostname conventions

2. **Use Static IPs or DHCP Reservations**
   - Easier device management
   - Reliable discovery
   - Consistent addressing

3. **Configure Firewall Rules**
   - Allow mDNS (UDP 5353)
   - Allow OTA port (TCP 3232)
   - Allow HTTPS (TCP 443) for Firebase

### Backup and Recovery

1. **Keep Firmware Backups**
   - Archive all released versions
   - Store on multiple locations
   - Include build configurations

2. **Document Rollback Procedure**
   - Know how to revert
   - Test rollback process
   - Keep previous version accessible

3. **Maintain Serial Upload Capability**
   - Keep USB cables accessible
   - Document COM ports
   - Test serial upload periodically

---

## Security Considerations

### Authentication Security

✅ **Application Default Credentials**
- No long-lived service account keys
- Tokens refresh automatically
- Reduced risk of credential leakage

✅ **Least Privilege Access**
- Only grant necessary IAM roles
- Use service account for automation
- Regular permission audits

✅ **Audit Logging**
- All Firebase operations logged
- Track who deployed what when
- Monitor for unauthorized access

### Network Security

✅ **Encrypted Communications**
- HTTPS for Firebase downloads
- WiFi encryption (WPA2/WPA3)
- Secure OTA password (optional)

✅ **Network Segmentation**
- Isolate IoT devices on separate VLAN
- Restrict internet access as needed
- Monitor network traffic

✅ **Firewall Configuration**
- Only open required ports
- Block unnecessary services
- Use firewall rules for access control

### Firmware Security

✅ **Code Signing** (Future Enhancement)
- Sign firmware binaries
- Verify signature before install
- Prevent unauthorized firmware

✅ **Secure Boot** (ESP32-S3)
- Enable secure boot in production
- Protect bootloader
- Prevent firmware tampering

✅ **Version Validation**
- Check version format
- Verify version increment
- Reject downgrade attempts

### Credential Management

✅ **Separate Credentials File**
- Keep `credentials.h` out of version control
- Use environment variables for CI/CD
- Rotate passwords regularly

✅ **WiFi Security**
- Use strong WiFi passwords
- WPA3 where available
- Regular password rotation

✅ **Firebase Security Rules**
- Configure Storage security rules
- Restrict read/write access
- Validate file paths

---

## Appendix

### File Reference

#### Core Scripts
- `firebase_storage_uploader.py` - Firebase Storage upload (no keys)
- `local_bulk_ota_deploy.py` - Local network OTA deployment
- `example_firebase_upload.py` - Example usage demonstrations
- `bulk_ota_deploy.py` - Legacy Firebase deployment (with keys)

#### Documentation
- `OTA_SYSTEM_COMPLETE_GUIDE.md` - This comprehensive guide
- `FIREBASE_SETUP.md` - Firebase authentication setup
- `COMPREHENSIVE_DEPLOYMENT_GUIDE.md` - Deployment procedures

#### Configuration
- `requirements.txt` - Python dependencies (cloud)
- `requirements_local.txt` - Python dependencies (local)
- `platformio.ini` - PlatformIO configuration
- `src/credentials.h` - Device credentials (not in repo)

### Command Reference

#### Firebase Upload Commands
```bash
# Authenticate
gcloud auth application-default login
gcloud auth application-default set-quota-project skyacres-marketplace

# Upload firmware
python example_firebase_upload.py single   # Single device
python example_firebase_upload.py bulk     # Multiple devices
python example_firebase_upload.py csv      # From CSV file
```

#### Local OTA Commands
```bash
# Discovery
python local_bulk_ota_deploy.py --csv devices.csv --discover-only

# Deploy
python local_bulk_ota_deploy.py --csv devices.csv --network 192.168.68.0/24 --yes

# Group deployment
python local_bulk_ota_deploy.py --csv devices.csv --group testing --yes

# Dry run
python local_bulk_ota_deploy.py --csv devices.csv --dry-run
```

#### PlatformIO Commands
```bash
# Compile
pio run -e esps3_board

# Serial upload
pio run -e esps3_board -t upload --upload-port COM3

# OTA upload
pio run -e esps3_board -t upload --upload-port device.local

# Clean
pio run -t clean
```

### Network Diagnostics

```bash
# Ping device
ping device.local
ping 192.168.68.150

# Check OTA port
telnet device.local 3232
nc -zv device.local 3232

# Scan network
nmap -p 3232 192.168.68.0/24

# DNS lookup
nslookup device.local
```

### Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-10-01 | Initial comprehensive OTA guide |
| 1.58 | 2025-09-29 | Current production firmware version |

---

**Document Version**: 1.0.0
**Last Updated**: October 1, 2025
**Author**: SkyAcres Development Team
**Project**: Skyboard-FB32 ESP32 OTA System
