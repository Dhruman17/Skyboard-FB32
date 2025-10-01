# Device-Specific Firmware Compilation Fix

## Problem Identified

When deploying firmware to multiple ESP32 devices using `local_bulk_ota_deploy.py`, all devices were receiving firmware with the **same serial number** instead of their unique device-specific serial numbers. This caused:

- ❌ Identity conflicts in Firebase
- ❌ Devices going offline
- ❌ Loss of device-specific tracking

## Root Cause

The original implementation used `pio run -e <env> -t upload` which:

1. ✅ Compiled device-specific firmware with unique serial number
2. ✅ Saved it to `firmware_compiled/<serial>/firmware.bin`
3. ❌ **Then triggered a RECOMPILATION** during upload
4. ❌ Used the restored `credentials.h` (generic serial number)
5. ❌ Uploaded the **wrong firmware** to the device

### Code Flow (Before Fix)

```
For Device TL1079927967:
├── 1. Backup src/credentials.h
├── 2. Write device-specific credentials.h (serial: TL1079927967)
├── 3. pio run -e esps3_board  ← Compiles with TL1079927967 ✓
├── 4. Copy to firmware_compiled/TL1079927967/firmware.bin  ✓
├── 5. Restore original credentials.h (serial: GENERIC)
└── 6. pio run -e esps3_board -t upload  ← RECOMPILES with GENERIC! ✗
    └── Uploads GENERIC firmware instead of TL1079927967 firmware! ✗
```

## Solution

Use `espota.py` directly to upload the **pre-compiled** device-specific binary without triggering recompilation.

### Code Flow (After Fix)

```
For Device TL1079927967:
├── 1. Backup src/credentials.h
├── 2. Write device-specific credentials.h (serial: TL1079927967)
├── 3. pio run -e esps3_board  ← Compiles with TL1079927967 ✓
├── 4. Copy to firmware_compiled/TL1079927967/firmware.bin  ✓
├── 5. Restore original credentials.h (serial: GENERIC)
└── 6. python espota.py -i device.local -f firmware_compiled/TL1079927967/firmware.bin  ✓
    └── Uploads TL1079927967-specific firmware directly! ✓
```

## Changes Made

### 1. Modified `deploy_ota_to_device()` function

**Before:**
```python
# This triggers recompilation!
cmd = pio_cmd + ['run', '-e', env, '-t', 'upload',
                 '--upload-port', f"{hostname}.local",
                 '--upload-protocol', 'espota']
```

**After:**
```python
# Upload pre-compiled binary directly (no recompilation)
cmd = ['python', espota_path,
       '-i', f"{hostname}.local",
       '-p', str(self.ota_port),
       '-f', firmware_path,  # Pre-compiled device-specific binary
       '-d']
```

### 2. Added Documentation

- Added detailed header comment explaining the fix
- Added inline comments warning about recompilation issue
- Created this fix documentation

### 3. Improved Error Messages

```python
print(f"[OK] Successfully deployed device-specific firmware to {hostname}.local ({ip})")
print(f"[OTA] Firmware: {firmware_path}")
```

Now explicitly shows which firmware file is being uploaded.

## Testing the Fix

### Test Script

```bash
# Create test CSV with multiple devices
cat > test_devices.csv << EOF
serial_number,board_type,target_version,system_name,ip_address
TL1079927967,ESP32_THREE_PORT,1.58,TL01,192.168.68.150
MR6384439627,ESP32_S3,1.58,MR1,192.168.68.139
EOF

# Deploy
python local_bulk_ota_deploy.py --csv test_devices.csv --network 192.168.68.0/24 --yes
```

### Expected Output

```
[COMPILE] Compiling device-specific firmware for TL1079927967...
[OK] Device-specific firmware compiled: firmware_compiled/TL1079927967/firmware.bin
[OTA] Uploading device-specific firmware to TL01.local (192.168.68.150)...
[OTA] Firmware: firmware_compiled/TL1079927967/firmware.bin
[OK] Successfully deployed device-specific firmware to TL01.local (192.168.68.150)

[COMPILE] Compiling device-specific firmware for MR6384439627...
[OK] Device-specific firmware compiled: firmware_compiled/MR6384439627/firmware.bin
[OTA] Uploading device-specific firmware to MR1.local (192.168.68.139)...
[OTA] Firmware: firmware_compiled/MR6384439627/firmware.bin
[OK] Successfully deployed device-specific firmware to MR1.local (192.168.68.139)
```

### Verification

After deployment, verify each device has its correct serial number:

1. **Check Serial Monitor:**
   ```
   Serial Number: TL1079927967  ← Unique to device
   ```

2. **Check Firebase:**
   ```
   devices/
   ├── TL1079927967/  ← Device-specific document
   └── MR6384439627/  ← Different serial number
   ```

3. **Check System Logs:**
   ```
   Device: TL01, Serial: TL1079927967, Status: Online
   Device: MR1, Serial: MR6384439627, Status: Online
   ```

## Why This Matters

### Without Fix
```
All devices report as: "GENERIC_SERIAL_NUMBER"
└── Firebase shows only 1 device (last updated)
    └── All other devices appear offline
```

### With Fix
```
Device 1: TL1079927967 ✓
Device 2: MR6384439627 ✓
Device 3: AB1234567890 ✓
└── Firebase shows all 3 devices independently
    └── Each device tracked separately
```

## Technical Details

### espota.py Location

The script searches for `espota.py` in:
```python
possible_espota_paths = [
    Path.home() / '.platformio' / 'packages' / 'framework-arduinoespressif32' / 'tools' / 'espota.py',
    Path.home() / '.platformio' / 'packages' / 'tool-espotapy' / 'espota.py',
]
```

### espota.py Arguments

```bash
python espota.py \
  -i device.local \         # Target device (mDNS hostname)
  -p 3232 \                 # OTA port (default: 3232)
  -f firmware.bin \         # Firmware binary to upload
  -d                        # Debug mode (verbose output)
```

### Credentials Template

The script generates device-specific `credentials.h`:

```cpp
#ifndef CREDENTIALS_H
#define CREDENTIALS_H

// Serial number and delays for system
String serialNumber = "TL1079927967"; // ← Device-specific!

// Known Wi-Fi Networks
struct WiFiCredentials {
    const char *ssid;
    const char *password;
};

String setupWifiName = "SkyAcres Setup " + serialNumber;

// Firebase Credentials
#define API_KEY "AIzaSyDfp9KFIxgs9Wb0AiJTENejm1GLjS2MCQI"
#define FIREBASE_PROJECT_ID "skyacres-marketplace"
#define USER_EMAIL "info@skyacres.ca"
#define USER_PASSWORD "SkyacresBC"

#endif // CREDENTIALS_H
```

## Troubleshooting

### Issue: espota.py not found

**Error:**
```
[ERROR] espota.py not found in PlatformIO packages
```

**Solution:**
```bash
# Install/reinstall PlatformIO ESP32 platform
pio platform install espressif32
```

### Issue: Device not responding

**Error:**
```
[ERROR] No response from device
```

**Solution:**
1. Check device is awake (not in deep sleep)
2. Verify ArduinoOTA is enabled in firmware
3. Ensure device has >50KB free heap
4. Check firewall allows port 3232

### Issue: Firmware binary not found

**Error:**
```
[ERROR] Device-specific firmware binary not found: firmware_compiled/TL1079927967/firmware.bin
```

**Solution:**
1. Check compilation succeeded
2. Verify `.pio/build/<env>/firmware.bin` exists
3. Check disk space for firmware_compiled directory

## References

- **Fixed File:** `local_bulk_ota_deploy.py` (lines 405-445)
- **Documentation:** `OTA_SYSTEM_COMPLETE_GUIDE.md`
- **Related:** `COMPREHENSIVE_DEPLOYMENT_GUIDE.md`

## Version History

| Version | Date | Change |
|---------|------|--------|
| 1.0 | 2025-10-01 | Initial fix implemented |
| 1.1 | 2025-10-01 | Added comprehensive documentation |

---

**Document Version**: 1.1
**Last Updated**: October 1, 2025
**Author**: SkyAcres Development Team
**Issue**: Device-specific serial numbers not preserved during OTA
**Status**: ✅ FIXED
