# SkyAcres Local OTA Deployment

## Quick Start

Deploy firmware to multiple ESP32 devices with unique serial numbers:

```bash
python local_bulk_ota_deploy.py --csv test_local_deployment.csv --yes
```

## CSV Format

```csv
serial_number,board_type,target_version,system_name,ip_address
123456789123456789,ESP32_S3,1.57,THQ,10.0.0.78
TL1079927967,ESP32_THREE_PORT,1.57,TL01,192.168.68.150
```

**Required Fields:**
- `serial_number` - Unique device identifier
- `board_type` - `ESP32_S3` or `ESP32_THREE_PORT`
- `target_version` - Firmware version

## How It Works

For each device, the script:
1. Compiles device-specific firmware with unique serial number
2. Verifies binary contains correct serial number
3. Deploys via OTA to device on local network
4. Reports success/failure per device

## Verify Firmware

Check that compiled firmware has correct serial numbers:

```bash
python verify_firmware_serials.py
```

## Common Commands

```bash
# Deploy to all devices
python local_bulk_ota_deploy.py --csv test_local_deployment.csv --yes

# Deploy to specific group
python local_bulk_ota_deploy.py --csv test_local_deployment.csv --group testing --yes

# Dry run (no actual deployment)
python local_bulk_ota_deploy.py --csv test_local_deployment.csv --dry-run

# Discover devices only
python local_bulk_ota_deploy.py --csv test_local_deployment.csv --discover-only
```

## Troubleshooting

**All devices have same serial number?**
- Run `python verify_firmware_serials.py` to check
- Delete `firmware_compiled/` directory and redeploy

**Device not found on network?**
- Verify device IP address in CSV
- Check device is on same network
- Try direct IP instead of mDNS hostname

**OTA upload fails?**
- Ensure ArduinoOTA is enabled in device firmware
- Check device has >50KB free heap memory
- Verify firewall allows port 3232

## Key Features

✓ Compiles unique firmware per device
✓ Verifies serial numbers before deployment
✓ Clean builds prevent caching issues
✓ Parallel deployment support
✓ Detailed logging and error reporting
