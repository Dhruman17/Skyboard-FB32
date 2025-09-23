# Firmware Directory Structure

This directory contains firmware binaries for different board types and versions.

## Directory Structure
```
firmware/
├── {version}/
│   ├── ESP32_THREE_PORT/
│   │   └── firmware.bin
│   └── ESP32_S3/
│       └── firmware.bin
└── README.md
```

## How to Use

1. Place your compiled firmware binaries in the appropriate directories:
   - `{version}/ESP32_THREE_PORT/firmware.bin` - For 3-port ESP32 boards
   - `{version}/ESP32_S3/firmware.bin` - For ESP32-S3 boards

2. Run the bulk deployment script:
   ```bash
   python bulk_ota_deploy.py --csv test_deployment.csv --service-account path/to/service-account-key.json
   ```

## Build Instructions

To generate firmware binaries for deployment:

### For ESP32_THREE_PORT:
```bash
pio run -e esp32dev_3port
cp .pio/build/esp32dev_3port/firmware.bin firmware/{version}/ESP32_THREE_PORT/
```

### For ESP32_S3:
```bash
pio run -e esps3_board
cp .pio/build/esps3_board/firmware.bin firmware/{version}/ESP32_S3/
```

Replace `{version}` with the target firmware version (e.g., 1.570).

## Notes

- Each board type requires its own firmware binary due to different pin configurations
- The deployment script will automatically select the correct firmware based on the board_type in the CSV file
- Always test with a single device before bulk deployment