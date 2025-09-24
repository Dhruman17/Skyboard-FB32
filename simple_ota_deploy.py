#!/usr/bin/env python3
"""
Simple OTA deployment script (Windows compatible)
"""

import sys
import subprocess
import os
from pathlib import Path

def deploy_ota(ip, firmware_path, port=3232):
    """Deploy firmware using OTA"""
    print(f"Starting OTA deployment to {ip}...")
    print(f"Firmware: {firmware_path}")
    print(f"Port: {port}")

    if not os.path.exists(firmware_path):
        print(f"[ERROR] Firmware file not found: {firmware_path}")
        return False

    try:
        cmd = [
            'python', '-m', 'espota',
            '-i', ip,
            '-p', str(port),
            '-f', str(firmware_path)
        ]

        print(f"Command: {' '.join(cmd)}")

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)

        if result.returncode == 0:
            print("[SUCCESS] OTA deployment completed successfully")
            print("Output:", result.stdout)
            return True
        else:
            print("[FAIL] OTA deployment failed")
            print("Error:", result.stderr)
            print("Output:", result.stdout)
            return False

    except subprocess.TimeoutExpired:
        print("[ERROR] OTA deployment timed out")
        return False
    except FileNotFoundError:
        print("[ERROR] espota module not found. Install with: pip install esptool")
        return False
    except Exception as e:
        print(f"[ERROR] OTA deployment error: {e}")
        return False

def main():
    if len(sys.argv) < 3:
        print("Usage: python simple_ota_deploy.py <device_ip> <firmware_path> [port]")
        print("Example: python simple_ota_deploy.py 192.168.1.100 ./firmware/1.570/ESP32_S3/firmware.bin")
        sys.exit(1)

    device_ip = sys.argv[1]
    firmware_path = sys.argv[2]
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 3232

    success = deploy_ota(device_ip, firmware_path, port)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()