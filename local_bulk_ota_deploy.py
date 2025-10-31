#!/usr/bin/env python3
"""
Local Bulk OTA Deployment Script for SkyAcres Systems
Deploys firmware updates to multiple ESP32 devices on local network using ArduinoOTA

IMPORTANT: Device-Specific Firmware Compilation
================================================
This script compiles DEVICE-SPECIFIC firmware for each ESP32 device to preserve unique serial numbers.

How it works:
1. For each device, temporarily modifies src/credentials.h with the device's serial number
2. Compiles firmware with that serial number baked in
3. Uploads the compiled binary directly using espota.py (NOT via 'pio run -t upload')
4. Restores original credentials.h

Why espota.py instead of 'pio run -t upload'?
----------------------------------------------
Using 'pio run -t upload' would trigger a RECOMPILATION with the restored credentials.h,
losing the device-specific serial number! By using espota.py directly, we upload the
pre-compiled device-specific binary, preserving each device's unique identity.
"""

import csv
import sys
import os
import argparse
import subprocess
import threading
import time
from datetime import datetime
from pathlib import Path
import socket
import requests
from concurrent.futures import ThreadPoolExecutor, as_completed

class LocalBulkOTADeployer:
    def __init__(self, network_range="192.168.1.0/24", ota_port=3232):
        """Initialize local OTA deployer"""
        self.network_range = network_range
        self.ota_port = ota_port
        self.discovered_devices = {}
        # CRITICAL: Lock to prevent parallel compilation race conditions
        # Only one device can compile at a time to avoid src/credentials.h conflicts
        self.compilation_lock = threading.Lock()
        print(f"[OK] Local Bulk OTA Deployer initialized")
        print(f"[NET] Network range: {network_range}")
        print(f"[PORT] OTA port: {ota_port}")

    def validate_csv_entry(self, row):
        """Validate CSV row data"""
        required_fields = ['serial_number', 'board_type', 'target_version']
        for field in required_fields:
            if not row.get(field):
                return False, f"Missing required field: {field}"

        # Validate board type
        valid_board_types = ['ESP32_THREE_PORT', 'ESP32_S3']
        if row['board_type'] not in valid_board_types:
            return False, f"Invalid board type: {row['board_type']}. Must be one of {valid_board_types}"

        return True, "Valid"

    def read_deployment_csv(self, csv_file_path, deployment_group=None):
        """Read and validate CSV file"""
        devices = []
        print(f"[READ] Reading deployment list from: {csv_file_path}")

        try:
            with open(csv_file_path, 'r', newline='') as csvfile:
                reader = csv.DictReader(csvfile)
                for row_num, row in enumerate(reader, start=2):
                    # Filter by deployment group if specified
                    if deployment_group and row.get('deployment_group') != deployment_group:
                        continue

                    # Validate row
                    is_valid, message = self.validate_csv_entry(row)
                    if not is_valid:
                        print(f"[WARN] Row {row_num}: {message}")
                        continue

                    devices.append(row)
                    print(f"[OK] Added device: {row['serial_number']} ({row['board_type']}) -> v{row['target_version']}")

            print(f"[SUMMARY] Total devices to deploy: {len(devices)}")
            return devices

        except FileNotFoundError:
            print(f"[ERROR] CSV file not found: {csv_file_path}")
            sys.exit(1)
        except Exception as e:
            print(f"[ERROR] Error reading CSV: {e}")
            sys.exit(1)

    def scan_for_device_ip(self, hostname, timeout=2):
        """Try to find device IP by hostname using ping and nslookup"""
        # Try Windows ping command
        try:
            result = subprocess.run(['ping', '-n', '1', '-w', str(timeout*1000), hostname],
                                  capture_output=True, text=True, timeout=timeout+1)
            if result.returncode == 0:
                # Extract IPv4 from ping output
                lines = result.stdout.split('\n')
                for line in lines:
                    if 'Pinging' in line and '[' in line and ']' in line:
                        ip = line.split('[')[1].split(']')[0]
                        # Only return IPv4 addresses
                        if self.is_ipv4(ip):
                            return ip
        except:
            pass

        # Try nslookup for mDNS resolution
        try:
            result = subprocess.run(['nslookup', f"{hostname}.local"],
                                  capture_output=True, text=True, timeout=timeout)
            if result.returncode == 0:
                lines = result.stdout.split('\n')
                for line in lines:
                    if 'Address:' in line and not '::' in line:
                        parts = line.split()
                        if len(parts) > 1:
                            ip = parts[-1]
                            if self.is_ipv4(ip):
                                return ip
        except:
            pass

        # Try direct nslookup without .local
        try:
            result = subprocess.run(['nslookup', hostname],
                                  capture_output=True, text=True, timeout=timeout)
            if result.returncode == 0:
                lines = result.stdout.split('\n')
                for line in lines:
                    if 'Address:' in line and not '::' in line:
                        parts = line.split()
                        if len(parts) > 1:
                            ip = parts[-1]
                            if self.is_ipv4(ip):
                                return ip
        except:
            pass

        return None

    def is_ipv4(self, ip):
        """Check if string is valid IPv4 address"""
        try:
            parts = ip.split('.')
            if len(parts) != 4:
                return False
            for part in parts:
                if not (0 <= int(part) <= 255):
                    return False
            return True
        except:
            return False

    def check_ota_port(self, ip, timeout=2):
        """Check if OTA port is open on device"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            result = sock.connect_ex((ip, self.ota_port))
            sock.close()
            return result == 0
        except:
            return False

    def discover_device(self, serial_number, system_name_hint=None, ip_hint=None):
        """Discover a single device on the network"""

        # If IP address is directly provided, test it first
        if ip_hint and self.is_ipv4(ip_hint):
            print(f"[FIND] Testing direct IP {ip_hint} for {serial_number}...")
            # Skip OTA port check - PlatformIO handles OTA protocol internally
            # Just verify the IP is reachable via ping
            try:
                import platform
                if platform.system().lower() == 'windows':
                    result = subprocess.run(['ping', '-n', '1', '-w', '2000', ip_hint],
                                          capture_output=True, text=True, timeout=5)
                else:
                    result = subprocess.run(['ping', '-c', '1', '-W', '2', ip_hint],
                                          capture_output=True, text=True, timeout=5)
                if result.returncode == 0:
                    print(f"[OK] Device reachable at direct IP {ip_hint}")
                    return ip_hint, f"direct-{serial_number}"
                else:
                    print(f"[WARN] Cannot ping direct IP {ip_hint}")
            except Exception as e:
                print(f"[WARN] Cannot test direct IP {ip_hint}: {e}")

        # Try different hostname possibilities
        hostnames = []

        if system_name_hint:
            hostnames.append(system_name_hint)

        # Common patterns for system names
        hostnames.extend([
            serial_number,
            f"skyacres-{serial_number}",
            f"system-{serial_number}",
            f"esp32-{serial_number}"
        ])

        for hostname in hostnames:
            print(f"[FIND] Trying to discover {serial_number} as {hostname}...")

            ip = self.scan_for_device_ip(hostname)
            if ip:
                print(f"[FOUND] Found {hostname} at IPv4: {ip}")
                # For PlatformIO OTA, we don't need to check the OTA port
                # PlatformIO handles the connection directly via hostname.local
                print(f"[OK] Device reachable via {hostname}.local ({ip})")
                return ip, hostname

        return None, None

    def discover_devices(self, devices):
        """Discover multiple devices on the network"""
        print(f"\n[DISCOVER] Discovering {len(devices)} devices on network...")

        discovered = {}

        # Use threading for parallel discovery
        with ThreadPoolExecutor(max_workers=10) as executor:
            future_to_device = {
                executor.submit(
                    self.discover_device,
                    device['serial_number'],
                    device.get('system_name'),
                    device.get('ip_address')
                ): device
                for device in devices
            }

            for future in as_completed(future_to_device):
                device = future_to_device[future]
                try:
                    ip, hostname = future.result()
                    if ip:
                        discovered[device['serial_number']] = {
                            'ip': ip,
                            'hostname': hostname,
                            'device': device
                        }
                        print(f"[OK] Device {device['serial_number']} discovered at {ip}")
                    else:
                        print(f"[ERROR] Could not discover device {device['serial_number']}")
                except Exception as e:
                    print(f"[ERROR] Error discovering {device['serial_number']}: {e}")

        print(f"[SUMMARY] Discovered {len(discovered)}/{len(devices)} devices")
        return discovered

    def find_platformio_executable(self):
        """Find PlatformIO executable in common locations"""
        import os
        from pathlib import Path

        # Common PlatformIO installation paths on Windows
        possible_paths = [
            'pio',  # If in PATH
            'platformio',  # If in PATH
            Path.home() / '.platformio' / 'penv' / 'Scripts' / 'pio.exe',
            Path.home() / '.platformio' / 'penv' / 'Scripts' / 'platformio.exe',
            Path(os.environ.get('APPDATA', '')) / 'Python' / 'Scripts' / 'pio.exe',
            Path(os.environ.get('APPDATA', '')) / 'Python' / 'Scripts' / 'platformio.exe',
            'python -m platformio',  # As Python module
        ]

        for path in possible_paths:
            try:
                if isinstance(path, str):
                    if path.startswith('python -m'):
                        # Test python module
                        result = subprocess.run(['python', '-m', 'platformio', '--version'],
                                              capture_output=True, timeout=10)
                        if result.returncode == 0:
                            return ['python', '-m', 'platformio']
                    else:
                        # Test direct command
                        result = subprocess.run([path, '--version'], capture_output=True, timeout=10)
                        if result.returncode == 0:
                            return [path]
                else:
                    # Test path object
                    if path.exists():
                        result = subprocess.run([str(path), '--version'], capture_output=True, timeout=10)
                        if result.returncode == 0:
                            return [str(path)]
            except:
                continue

        return None

    def verify_firmware_serial_number(self, firmware_path, expected_serial):
        """Verify that the compiled firmware contains the expected serial number"""
        try:
            with open(firmware_path, 'rb') as f:
                firmware_content = f.read()
                # Search for serial number as ASCII string in binary
                if expected_serial.encode('ascii') in firmware_content:
                    print(f"[VERIFY] OK - Firmware contains serial number: {expected_serial}")
                    return True
                else:
                    print(f"[VERIFY] ERROR - Firmware does NOT contain expected serial number: {expected_serial}")
                    return False
        except Exception as e:
            print(f"[VERIFY] Error verifying firmware: {e}")
            return False

    def compile_device_firmware(self, serial_number, board_type):
        """Compile device-specific firmware with unique serial number

        CRITICAL: This function uses a lock to prevent race conditions when multiple
        threads try to compile simultaneously. Only one compilation can happen at a time
        because all compilations modify the same src/credentials.h file.
        """
        import shutil
        import subprocess
        from pathlib import Path

        # CRITICAL: Acquire lock before compilation to prevent parallel access to credentials.h
        with self.compilation_lock:
            print(f"\n{'='*60}")
            print(f"[COMPILE] Compiling device-specific firmware for: {serial_number}")
            print(f"[COMPILE] Board type: {board_type}")
            print(f"{'='*60}")

            return self._compile_device_firmware_locked(serial_number, board_type)

    def _compile_device_firmware_locked(self, serial_number, board_type):
        """Internal compilation function (must be called with lock held)"""
        import shutil
        import subprocess
        from pathlib import Path

        try:
            # Create temporary credentials file with device-specific serial number
            temp_credentials = f"""#ifndef CREDENTIALS_H
#define CREDENTIALS_H

// Serial number and delays for system
String serialNumber = "{serial_number}"; // Unique serial number for each system

// Known Wi-Fi Networks
struct WiFiCredentials
{{
    const char *ssid;
    const char *password;
}};

String setupWifiName = "SkyAcres Setup " + serialNumber;

// Firebase Credentials
#define API_KEY "AIzaSyDfp9KFIxgs9Wb0AiJTENejm1GLjS2MCQI"
#define FIREBASE_PROJECT_ID "skyacres-marketplace"
#define USER_EMAIL "info@skyacres.ca"
#define USER_PASSWORD "SkyacresBC"

#endif // CREDENTIALS_H
"""

            # Backup original credentials.h
            original_creds = Path("src/credentials.h")
            backup_creds = Path("src/credentials.h.backup")
            shutil.copy2(original_creds, backup_creds)

            # Write device-specific credentials
            print(f"[COMPILE] Injecting serial number into credentials.h: {serial_number}")
            with open(original_creds, 'w') as f:
                f.write(temp_credentials)

            # Verify the file was written correctly
            with open(original_creds, 'r') as f:
                if serial_number not in f.read():
                    print(f"[ERROR] Failed to inject serial number into credentials.h!")
                    return None

            # Determine PlatformIO environment
            if board_type == 'ESP32_S3':
                env = 'esps3_board'
            elif board_type == 'ESP32_THREE_PORT':
                env = 'esp32dev_3port'
            else:
                env = 'esps3_board'  # Default

            # Find PlatformIO executable
            pio_cmd = self.find_platformio_executable()
            if not pio_cmd:
                print(f"[ERROR] PlatformIO not found. Please install PlatformIO.")
                return None

            # Clean build to ensure fresh compilation
            print(f"[COMPILE] Cleaning previous build for environment: {env}")
            clean_cmd = pio_cmd + ['run', '-e', env, '-t', 'clean']
            subprocess.run(clean_cmd, capture_output=True, text=True, timeout=60)

            # Compile firmware
            cmd = pio_cmd + ['run', '-e', env]
            print(f"[CMD] Compiling: {' '.join(cmd)}")

            result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)

            if result.returncode == 0:
                # Copy compiled firmware to device-specific location
                compiled_firmware = Path(f".pio/build/{env}/firmware.bin")
                if compiled_firmware.exists():
                    device_firmware_dir = Path("firmware_compiled") / serial_number
                    device_firmware_dir.mkdir(parents=True, exist_ok=True)
                    device_firmware_path = device_firmware_dir / "firmware.bin"
                    shutil.copy2(compiled_firmware, device_firmware_path)

                    # CRITICAL: Verify the firmware contains the correct serial number
                    print(f"[COMPILE] Verifying firmware contains serial number...")
                    if self.verify_firmware_serial_number(str(device_firmware_path), serial_number):
                        file_size = device_firmware_path.stat().st_size / 1024  # KB
                        print(f"[OK] Device-specific firmware compiled successfully!")
                        print(f"[OK]   Path: {device_firmware_path}")
                        print(f"[OK]   Size: {file_size:.2f} KB")
                        print(f"[OK]   Serial: {serial_number}")
                        return str(device_firmware_path)
                    else:
                        print(f"[ERROR] Firmware verification FAILED - serial number not found in binary!")
                        print(f"[ERROR] This firmware would cause all devices to have the same serial number!")
                        return None
                else:
                    print(f"[ERROR] Compiled firmware not found at {compiled_firmware}")
                    return None
            else:
                print(f"[ERROR] Compilation failed:")
                print(f"STDOUT: {result.stdout}")
                print(f"STDERR: {result.stderr}")
                return None

        except Exception as e:
            print(f"[ERROR] Compilation error: {e}")
            import traceback
            traceback.print_exc()
            return None
        finally:
            # Restore original credentials.h
            if backup_creds.exists():
                print(f"[COMPILE] Restoring original credentials.h")
                shutil.copy2(backup_creds, original_creds)
                backup_creds.unlink()  # Delete backup

    def check_ota_ready(self, ip, hostname):
        """Check if device is ready for OTA by testing port connectivity"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)  # 5 second timeout
            result = sock.connect_ex((ip, self.ota_port))
            sock.close()
            return result == 0
        except Exception:
            return False

    def prepare_device_for_ota(self, ip, hostname):
        """Trigger OTA preparation mode on device via HTTP endpoint"""
        try:
            url = f"http://{ip}/prepare-ota"
            print(f"[OTA] Triggering OTA preparation mode on {hostname} ({ip})...")
            response = requests.get(url, timeout=5)
            if response.status_code == 200:
                print(f"[OK] OTA preparation mode activated on {hostname}")
                print(f"[INFO] {response.text.splitlines()[0]}")  # First line of response
                time.sleep(2)  # Give device time to free memory
                return True
            else:
                print(f"[WARN] Failed to activate OTA preparation mode (HTTP {response.status_code})")
                return False
        except requests.exceptions.RequestException as e:
            print(f"[WARN] Could not reach HTTP endpoint: {e}")
            print(f"[INFO] Device might be running old firmware without OTA preparation support")
            return False

    def deploy_ota_to_device(self, ip, hostname, firmware_path, password="", board_type="ESP32_S3"):
        """Deploy firmware to a single device using PlatformIO OTA (with device-specific binary swap)"""
        try:
            # Verify firmware binary exists
            if not os.path.exists(firmware_path):
                print(f"[ERROR] Device-specific firmware binary not found: {firmware_path}")
                return False

            # Determine environment based on board type
            if board_type == 'ESP32_S3':
                env = 'esps3_board'
            elif board_type == 'ESP32_THREE_PORT':
                env = 'esp32dev_3port'
            else:
                env = 'esps3_board'  # Default to S3

            # Find PlatformIO executable
            pio_cmd = self.find_platformio_executable()
            if not pio_cmd:
                print(f"[ERROR] PlatformIO not found")
                return False

            # CLEVER TRICK: Temporarily replace the build firmware with device-specific one
            # This allows PlatformIO to handle OTA upload (which works), but with our custom binary
            from pathlib import Path
            import shutil

            build_firmware = Path(f".pio/build/{env}/firmware.bin")
            backup_firmware = Path(f".pio/build/{env}/firmware.bin.backup")

            # Backup original firmware (if it exists)
            if build_firmware.exists():
                shutil.copy2(build_firmware, backup_firmware)

            # Replace with device-specific firmware
            shutil.copy2(firmware_path, build_firmware)

            try:
                # Use hostname.local for mDNS resolution
                target = f"{hostname}.local"
                cmd = pio_cmd + ['run', '-e', env, '-t', 'upload', '--upload-port', target]

                print(f"[OTA] Uploading device-specific firmware to {hostname}.local ({ip})...")
                print(f"[OTA] Firmware: {firmware_path}")
                print(f"[CMD] Command: {' '.join(cmd)}")

                result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)

                if result.returncode == 0:
                    print(f"[OK] Successfully deployed device-specific firmware to {hostname}.local ({ip})")
                    return True
                else:
                    print(f"[ERROR] OTA upload failed for {hostname}.local ({ip})")

                    # Check for common error patterns and provide helpful suggestions
                    error_output = result.stderr + result.stdout
                    if "No response from device" in error_output or "No Answer" in error_output:
                        print(f"[HINT] Device not responding to OTA - check if:")
                        print(f"       - Device is awake (not in deep sleep)")
                        print(f"       - ArduinoOTA is enabled in firmware")
                        print(f"       - Device has sufficient memory for OTA (>50KB free heap)")
                        print(f"       - No firewall blocking port {self.ota_port}")
                    elif "Error Uploading" in error_output:
                        print(f"[HINT] Upload error - device may have disconnected during upload")
                    elif "timeout" in error_output.lower():
                        print(f"[HINT] Upload timeout - check network stability and device responsiveness")
                    elif "Connection refused" in error_output:
                        print(f"[HINT] Connection refused - device may not have OTA enabled")

                    print(f"Error output: {result.stderr}")
                    if result.stdout and len(result.stdout) < 2000:
                        print(f"Standard output: {result.stdout}")
                    return False

            finally:
                # Restore original firmware backup
                if backup_firmware.exists():
                    shutil.copy2(backup_firmware, build_firmware)
                    backup_firmware.unlink()  # Delete backup

        except subprocess.TimeoutExpired:
            print(f"[ERROR] OTA upload timeout for {hostname}.local ({ip})")
            return False
        except Exception as e:
            print(f"[ERROR] OTA upload error for {hostname}.local ({ip}): {e}")
            import traceback
            traceback.print_exc()
            return False

    def deploy_bulk_local(self, devices, firmware_dir, max_concurrent=5):
        """Deploy to multiple devices locally"""
        print(f"\n[DEPLOY] Starting local bulk deployment...")

        # First discover all devices
        discovered_devices = self.discover_devices(devices)

        if not discovered_devices:
            print("[ERROR] No devices discovered. Check network connectivity and device status.")
            return

        # Deploy to discovered devices
        successful_deployments = 0
        failed_deployments = 0
        skipped_deployments = 0

        def deploy_single(serial_number, device_info):
            """Deploy to single device (for threading)"""
            device = device_info['device']
            ip = device_info['ip']
            hostname = device_info['hostname']

            print(f"\n{'#'*70}")
            print(f"# DEPLOYING TO DEVICE: {serial_number}")
            print(f"# IP: {ip} | Hostname: {hostname} | Board: {device['board_type']}")
            print(f"{'#'*70}\n")

            # Step 1: Compile device-specific firmware
            print(f"[DEVICE] Compiling firmware for {serial_number}...")
            firmware_path = self.compile_device_firmware(serial_number, device['board_type'])

            if not firmware_path:
                print(f"[ERROR] Failed to compile firmware for {serial_number}")
                return False

            # Use system_name from CSV instead of discovered hostname for OTA
            system_name = device.get('system_name', hostname)
            result = self.deploy_ota_to_device(ip, system_name, firmware_path, board_type=device['board_type'])

            if result:
                print(f"\n[SUCCESS] Device {serial_number} deployed successfully with unique firmware!\n")
            else:
                print(f"\n[FAILED] Device {serial_number} deployment failed!\n")

            return result

        # Use ThreadPoolExecutor for concurrent deployments
        with ThreadPoolExecutor(max_workers=max_concurrent) as executor:
            future_to_serial = {
                executor.submit(deploy_single, serial_number, device_info): serial_number
                for serial_number, device_info in discovered_devices.items()
            }

            for future in as_completed(future_to_serial):
                serial_number = future_to_serial[future]
                try:
                    result = future.result()
                    if result == 'firmware_missing':
                        skipped_deployments += 1
                    elif result:
                        successful_deployments += 1
                    else:
                        failed_deployments += 1
                except Exception as e:
                    print(f"[ERROR] Deployment error for {serial_number}: {e}")
                    failed_deployments += 1

        # Print summary
        total_devices = len(discovered_devices)
        total_requested = len(devices)
        undiscovered_devices = total_requested - total_devices

        print(f"\n{'='*70}")
        print(f"[SUMMARY] LOCAL DEPLOYMENT SUMMARY")
        print(f"{'='*70}")
        print(f"Devices in CSV: {total_requested}")
        print(f"Devices discovered: {total_devices}")
        if undiscovered_devices > 0:
            print(f"[FIND] Devices not found on network: {undiscovered_devices}")
        if skipped_deployments > 0:
            print(f"[WARN] Deployments skipped (missing firmware): {skipped_deployments}")
        print(f"[OK] Successful deployments: {successful_deployments}")
        print(f"[ERROR] Failed deployments: {failed_deployments}")

        # List all device-specific firmware compiled
        print(f"\n[FIRMWARE] Device-specific firmware files created:")
        from pathlib import Path
        firmware_base = Path("firmware_compiled")
        if firmware_base.exists():
            for serial_dir in sorted(firmware_base.iterdir()):
                if serial_dir.is_dir():
                    firmware_file = serial_dir / "firmware.bin"
                    if firmware_file.exists():
                        size_kb = firmware_file.stat().st_size / 1024
                        print(f"  [OK] {serial_dir.name}: {firmware_file} ({size_kb:.2f} KB)")

        print(f"\n[TIME] Completed at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"{'='*70}")

        if failed_deployments > 0:
            print(f"\n[WARN] {failed_deployments} deployments failed. Check logs above for details.")
        elif successful_deployments == 0 and total_devices > 0:
            print(f"\n[WARN] No deployments completed successfully.")
        elif successful_deployments > 0:
            print(f"\n[SUCCESS] {successful_deployments} device(s) updated with unique firmware!")
            print(f"[INFO] Each device now has its own serial number - no duplicates!")
            print(f"[INFO] You can verify by checking the Firebase logs for each device.")

def main():
    parser = argparse.ArgumentParser(description='Local Bulk OTA Deployment for SkyAcres Systems')
    parser.add_argument('--csv', required=True, help='Path to deployment CSV file')
    parser.add_argument('--firmware-dir', default='./firmware', help='Directory containing firmware files')
    parser.add_argument('--network', default='192.168.1.0/24', help='Network range to scan')
    parser.add_argument('--ota-port', type=int, default=3232, help='ArduinoOTA port (default: 3232)')
    parser.add_argument('--group', help='Deploy only to specific deployment group')
    parser.add_argument('--max-concurrent', type=int, default=5, help='Maximum concurrent deployments')
    parser.add_argument('--discover-only', action='store_true', help='Only discover devices, do not deploy')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be deployed without actually deploying')
    parser.add_argument('--yes', '-y', action='store_true', help='Skip confirmation prompts')

    args = parser.parse_args()

    # Validate inputs
    if not os.path.exists(args.csv):
        print(f"[ERROR] CSV file not found: {args.csv}")
        sys.exit(1)

    if not args.discover_only and not os.path.exists(args.firmware_dir):
        print(f"[ERROR] Firmware directory not found: {args.firmware_dir}")
        sys.exit(1)

    # Initialize deployer
    deployer = LocalBulkOTADeployer(args.network, args.ota_port)

    # Read devices from CSV
    devices = deployer.read_deployment_csv(args.csv, args.group)

    if not devices:
        print("[ERROR] No devices to process")
        sys.exit(1)

    if args.discover_only:
        # Discovery mode only
        discovered = deployer.discover_devices(devices)
        print(f"\n[SUMMARY] Discovery complete: {len(discovered)} devices found")
    elif args.dry_run:
        # Dry run mode
        print("\n[DRY-RUN] DRY RUN MODE - No actual deployment will occur")
        discovered = deployer.discover_devices(devices)
        print(f"\nWould deploy to {len(discovered)} discovered devices:")
        for serial_number, info in discovered.items():
            device = info['device']
            print(f"  - {serial_number} at {info['ip']} ({device['board_type']}) -> v{device['target_version']}")
    else:
        # Deployment mode
        print(f"\n[WARN] About to deploy firmware to devices on network {args.network}")
        if not args.yes:
            confirm = input("Continue? (y/N): ").strip().lower()
            if confirm != 'y':
                print("[ERROR] Deployment cancelled")
                sys.exit(0)

        deployer.deploy_bulk_local(devices, args.firmware_dir, args.max_concurrent)

if __name__ == "__main__":
    main()