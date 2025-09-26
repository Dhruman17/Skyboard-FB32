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