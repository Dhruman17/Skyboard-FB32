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

# Firebase Storage integration
try:
    from firebase_storage_uploader import SecureFirebaseClient
    FIREBASE_AVAILABLE = True
except ImportError:
    FIREBASE_AVAILABLE = False
    print("[WARN] Firebase Storage uploader not available. Install with: pip install firebase-admin google-cloud-storage")

class LocalBulkOTADeployer:
    def __init__(self, network_range="192.168.1.0/24", ota_port=3232, firebase_project_id=None):
        """Initialize local OTA deployer"""
        self.network_range = network_range
        self.ota_port = ota_port
        self.discovered_devices = {}
        self.firebase_client = None

        print(f"[OK] Local Bulk OTA Deployer initialized")
        print(f"[NET] Network range: {network_range}")
        print(f"[PORT] OTA port: {ota_port}")

        # Initialize Firebase client if requested
        if firebase_project_id and FIREBASE_AVAILABLE:
            try:
                self.firebase_client = SecureFirebaseClient(project_id=firebase_project_id)
                print(f"[FIREBASE] Firebase Storage client initialized for project: {firebase_project_id}")
            except Exception as e:
                print(f"[WARN] Failed to initialize Firebase client: {e}")
                print(f"[INFO] Continuing without Firebase upload capability")

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
        import platform
        from pathlib import Path

        # Common PlatformIO installation paths for Windows and macOS/Linux
        possible_paths = [
            'pio',  # If in PATH
            'platformio',  # If in PATH
        ]

        # Add platform-specific paths
        if platform.system() == 'Windows':
            possible_paths.extend([
                Path.home() / '.platformio' / 'penv' / 'Scripts' / 'pio.exe',
                Path.home() / '.platformio' / 'penv' / 'Scripts' / 'platformio.exe',
                Path(os.environ.get('APPDATA', '')) / 'Python' / 'Scripts' / 'pio.exe',
                Path(os.environ.get('APPDATA', '')) / 'Python' / 'Scripts' / 'platformio.exe',
            ])
        else:  # macOS/Linux
            possible_paths.extend([
                Path.home() / '.platformio' / 'penv' / 'bin' / 'pio',
                Path.home() / '.platformio' / 'penv' / 'bin' / 'platformio',
                Path.home() / '.local' / 'bin' / 'pio',
                Path.home() / '.local' / 'bin' / 'platformio',
            ])

        # Try Python module last
        possible_paths.append('python3 -m platformio')

        for path in possible_paths:
            try:
                if isinstance(path, str):
                    if 'python' in path and '-m' in path:
                        # Test python module (use python3 on macOS/Linux, python on Windows)
                        python_cmd = 'python3' if platform.system() != 'Windows' else 'python'
                        result = subprocess.run([python_cmd, '-m', 'platformio', '--version'],
                                              capture_output=True, timeout=10)
                        if result.returncode == 0:
                            return [python_cmd, '-m', 'platformio']
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

    def create_version_file(self, serial_number, version, firmware_dir):
        """Create Version.txt file for a device (capital V to match Firebase naming)"""
        try:
            version_file_path = Path(firmware_dir) / "Version.txt"
            with open(version_file_path, 'w') as f:
                f.write(f"{version}\n")
            print(f"[VERSION] Created Version.txt: {version}")
            return str(version_file_path)
        except Exception as e:
            print(f"[ERROR] Failed to create Version.txt: {e}")
            return None

    def compile_device_firmware(self, serial_number, board_type):
        """Compile device-specific firmware with unique serial number"""
        import shutil
        from pathlib import Path

        print(f"[COMPILE] Compiling device-specific firmware for {serial_number}...")

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
            with open(original_creds, 'w') as f:
                f.write(temp_credentials)

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

            # Compile firmware
            import subprocess
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

                    print(f"[OK] Device-specific firmware compiled: {device_firmware_path}")
                    return str(device_firmware_path)
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
            return None
        finally:
            # Restore original credentials.h
            if backup_creds.exists():
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
                # Use IP address directly if available to avoid mDNS resolution issues
                # Otherwise fall back to hostname.local for mDNS resolution
                if self.is_ipv4(ip):
                    target = ip
                else:
                    target = hostname if hostname.endswith('.local') else f"{hostname}.local"
                cmd = pio_cmd + ['run', '-e', env, '-t', 'upload', '--upload-port', target]

                print(f"[OTA] Uploading device-specific firmware to {target} ({ip})...")
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

    def upload_firmware_to_firebase(self, serial_number, firmware_path, version_path):
        """Upload firmware to Firebase Storage for a device"""
        if not self.firebase_client:
            print(f"[WARN] Firebase client not initialized. Skipping upload for {serial_number}")
            return False

        try:
            print(f"[FIREBASE] Uploading firmware for {serial_number} to Firebase Storage...")
            success = self.firebase_client.upload_firmware_for_device(
                serial_number=serial_number,
                firmware_path=firmware_path,
                version_txt_path=version_path
            )

            if success:
                print(f"[FIREBASE] Successfully uploaded firmware for {serial_number}")
                return True
            else:
                print(f"[FIREBASE] Failed to upload firmware for {serial_number}")
                return False

        except Exception as e:
            print(f"[ERROR] Firebase upload error for {serial_number}: {e}")
            return False

    def deploy_bulk_local(self, devices, firmware_dir, max_concurrent=5, upload_to_firebase=False):
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
        firebase_upload_success = 0
        firebase_upload_failed = 0

        def deploy_single(serial_number, device_info):
            """Deploy to single device (for threading)"""
            device = device_info['device']
            ip = device_info['ip']
            hostname = device_info['hostname']

            # Step 1: Compile device-specific firmware
            print(f"[DEVICE] Compiling firmware for {serial_number}...")
            firmware_path = self.compile_device_firmware(serial_number, device['board_type'])

            if not firmware_path:
                print(f"[ERROR] Failed to compile firmware for {serial_number}")
                return False, False

            # Step 2: Create version.txt file
            firmware_dir_path = Path(firmware_path).parent
            version_path = self.create_version_file(serial_number, device['target_version'], firmware_dir_path)

            # Step 3: Deploy OTA to device
            # Use system_name from CSV instead of discovered hostname for OTA
            system_name = device.get('system_name', hostname)
            ota_success = self.deploy_ota_to_device(ip, system_name, firmware_path, board_type=device['board_type'])

            # Step 4: Upload to Firebase if requested
            firebase_success = False
            if upload_to_firebase and version_path:
                firebase_success = self.upload_firmware_to_firebase(serial_number, firmware_path, version_path)

            return ota_success, firebase_success

        # Use ThreadPoolExecutor for concurrent deployments
        with ThreadPoolExecutor(max_workers=max_concurrent) as executor:
            future_to_serial = {
                executor.submit(deploy_single, serial_number, device_info): serial_number
                for serial_number, device_info in discovered_devices.items()
            }

            for future in as_completed(future_to_serial):
                serial_number = future_to_serial[future]
                try:
                    ota_result, firebase_result = future.result()
                    if ota_result == 'firmware_missing':
                        skipped_deployments += 1
                    elif ota_result:
                        successful_deployments += 1
                    else:
                        failed_deployments += 1

                    # Track Firebase upload results
                    if upload_to_firebase:
                        if firebase_result:
                            firebase_upload_success += 1
                        else:
                            firebase_upload_failed += 1

                except Exception as e:
                    print(f"[ERROR] Deployment error for {serial_number}: {e}")
                    failed_deployments += 1
                    if upload_to_firebase:
                        firebase_upload_failed += 1

        # Print summary
        total_devices = len(discovered_devices)
        total_requested = len(devices)
        undiscovered_devices = total_requested - total_devices

        print(f"\n{'='*50}")
        print(f"[SUMMARY] LOCAL DEPLOYMENT SUMMARY")
        print(f"{'='*50}")
        print(f"Devices in CSV: {total_requested}")
        print(f"Devices discovered: {total_devices}")
        if undiscovered_devices > 0:
            print(f"[FIND] Devices not found on network: {undiscovered_devices}")
        if skipped_deployments > 0:
            print(f"[WARN] Deployments skipped (missing firmware): {skipped_deployments}")
        print(f"[OK] Successful OTA deployments: {successful_deployments}")
        print(f"[ERROR] Failed OTA deployments: {failed_deployments}")

        if upload_to_firebase:
            print(f"\n[FIREBASE] Firebase Storage Upload Summary:")
            print(f"[OK] Successful uploads: {firebase_upload_success}")
            print(f"[ERROR] Failed uploads: {firebase_upload_failed}")

        print(f"[TIME] Completed at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")

        if failed_deployments > 0:
            print(f"\n[WARN] {failed_deployments} deployments failed. Check logs above for details.")
        elif successful_deployments == 0 and total_devices > 0:
            print(f"\n[WARN] No deployments completed successfully.")
        elif successful_deployments > 0:
            print(f"\n[SUCCESS] {successful_deployments} device(s) updated successfully!")

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

    # Firebase Storage options
    parser.add_argument('--upload-to-firebase', action='store_true',
                        help='Upload compiled firmware to Firebase Storage after local OTA deployment')
    parser.add_argument('--firebase-only', action='store_true',
                        help='Only upload to Firebase Storage, skip local OTA deployment')
    parser.add_argument('--firebase-project', default='skyacres-marketplace',
                        help='Firebase project ID (default: skyacres-marketplace)')

    args = parser.parse_args()

    # Validate inputs
    if not os.path.exists(args.csv):
        print(f"[ERROR] CSV file not found: {args.csv}")
        sys.exit(1)

    if not args.discover_only and not os.path.exists(args.firmware_dir):
        print(f"[ERROR] Firmware directory not found: {args.firmware_dir}")
        sys.exit(1)

    # Check Firebase availability if Firebase options are used
    if (args.upload_to_firebase or args.firebase_only) and not FIREBASE_AVAILABLE:
        print(f"[ERROR] Firebase Storage integration not available.")
        print(f"[INFO] Install required packages: pip install firebase-admin google-cloud-storage")
        print(f"[INFO] See FIREBASE_SETUP.md for authentication setup")
        sys.exit(1)

    # Initialize deployer with optional Firebase support
    firebase_project_id = None
    if args.upload_to_firebase or args.firebase_only:
        firebase_project_id = args.firebase_project

    deployer = LocalBulkOTADeployer(args.network, args.ota_port, firebase_project_id=firebase_project_id)

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
            if args.upload_to_firebase or args.firebase_only:
                print(f"    Would also upload to Firebase Storage")
    elif args.firebase_only:
        # Firebase-only mode - compile and upload without local OTA
        print(f"\n[FIREBASE] Firebase-only mode: Compiling and uploading firmware to Firebase Storage")
        if not args.yes:
            confirm = input(f"Continue with Firebase upload for {len(devices)} device(s)? (y/N): ").strip().lower()
            if confirm != 'y':
                print("[CANCELLED] Firebase upload cancelled")
                sys.exit(0)

        successful_uploads = 0
        failed_uploads = 0

        for device in devices:
            serial_number = device['serial_number']
            print(f"\n[DEVICE] Processing {serial_number}...")

            # Compile firmware
            firmware_path = deployer.compile_device_firmware(serial_number, device['board_type'])
            if not firmware_path:
                print(f"[ERROR] Failed to compile firmware for {serial_number}")
                failed_uploads += 1
                continue

            # Create version file
            firmware_dir_path = Path(firmware_path).parent
            version_path = deployer.create_version_file(serial_number, device['target_version'], firmware_dir_path)

            # Upload to Firebase
            if version_path and deployer.upload_firmware_to_firebase(serial_number, firmware_path, version_path):
                successful_uploads += 1
            else:
                failed_uploads += 1

        print(f"\n{'='*50}")
        print(f"[SUMMARY] FIREBASE UPLOAD SUMMARY")
        print(f"{'='*50}")
        print(f"Total devices: {len(devices)}")
        print(f"[OK] Successful uploads: {successful_uploads}")
        print(f"[ERROR] Failed uploads: {failed_uploads}")
        print(f"[TIME] Completed at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    else:
        # Deployment mode (local OTA with optional Firebase upload)
        print(f"\n[WARN] About to deploy firmware to devices on network {args.network}")
        if args.upload_to_firebase:
            print(f"[INFO] Firmware will also be uploaded to Firebase Storage")
        if not args.yes:
            confirm = input("Continue? (y/N): ").strip().lower()
            if confirm != 'y':
                print("[ERROR] Deployment cancelled")
                sys.exit(0)

        deployer.deploy_bulk_local(devices, args.firmware_dir, args.max_concurrent,
                                   upload_to_firebase=args.upload_to_firebase)

if __name__ == "__main__":
    main()