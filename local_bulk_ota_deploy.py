#!/usr/bin/env python3
"""
Local Bulk OTA Deployment Script for SkyAcres Systems
Deploys firmware updates to multiple ESP32 devices on local network using ArduinoOTA
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
from concurrent.futures import ThreadPoolExecutor, as_completed

class LocalBulkOTADeployer:
    def __init__(self, network_range="192.168.1.0/24", ota_port=3232):
        """Initialize local OTA deployer"""
        self.network_range = network_range
        self.ota_port = ota_port
        self.discovered_devices = {}
        print(f"✅ Local Bulk OTA Deployer initialized")
        print(f"📡 Network range: {network_range}")
        print(f"🔌 OTA port: {ota_port}")

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
        print(f"📖 Reading deployment list from: {csv_file_path}")

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
                        print(f"⚠️ Row {row_num}: {message}")
                        continue

                    devices.append(row)
                    print(f"✅ Added device: {row['serial_number']} ({row['board_type']}) -> v{row['target_version']}")

            print(f"📊 Total devices to deploy: {len(devices)}")
            return devices

        except FileNotFoundError:
            print(f"❌ CSV file not found: {csv_file_path}")
            sys.exit(1)
        except Exception as e:
            print(f"❌ Error reading CSV: {e}")
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
            print(f"🔍 Testing direct IP {ip_hint} for {serial_number}...")
            if self.check_ota_port(ip_hint):
                print(f"✅ OTA service confirmed on direct IP {ip_hint}")
                return ip_hint, f"direct-{serial_number}"
            else:
                print(f"⚠️ OTA port {self.ota_port} not open on direct IP {ip_hint}")

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
            print(f"🔍 Trying to discover {serial_number} as {hostname}...")

            ip = self.scan_for_device_ip(hostname)
            if ip:
                print(f"📍 Found {hostname} at IPv4: {ip}")
                # For PlatformIO OTA, we don't need to check the OTA port
                # PlatformIO handles the connection directly via hostname.local
                print(f"✅ Device reachable via {hostname}.local ({ip})")
                return ip, hostname

        return None, None

    def discover_devices(self, devices):
        """Discover multiple devices on the network"""
        print(f"\n🕵️ Discovering {len(devices)} devices on network...")

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
                        print(f"✅ Device {device['serial_number']} discovered at {ip}")
                    else:
                        print(f"❌ Could not discover device {device['serial_number']}")
                except Exception as e:
                    print(f"❌ Error discovering {device['serial_number']}: {e}")

        print(f"📊 Discovered {len(discovered)}/{len(devices)} devices")
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

    def deploy_ota_to_device(self, ip, hostname, firmware_path, password="", board_type="ESP32_S3"):
        """Deploy firmware to a single device using PlatformIO OTA"""
        try:
            # Find PlatformIO executable
            pio_cmd = self.find_platformio_executable()
            if not pio_cmd:
                print(f"❌ PlatformIO not found. Please install PlatformIO or ensure it's in PATH")
                return False

            # Determine environment based on board type
            if board_type == 'ESP32_S3':
                env = 'esps3_board'
            elif board_type == 'ESP32_THREE_PORT':
                env = 'esp32dev_3port'
            else:
                env = 'esps3_board'  # Default to S3

            # Use PlatformIO OTA upload with specific environment
            cmd = pio_cmd + ['run', '-e', env, '-t', 'upload', '--upload-port', f"{hostname}.local"]

            print(f"🚀 Starting PlatformIO OTA deployment to {hostname}.local ({ip})...")
            print(f"📦 Command: {' '.join(cmd)}")

            result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)

            if result.returncode == 0:
                print(f"✅ Successfully deployed to {hostname}.local ({ip})")
                return True
            else:
                print(f"❌ PlatformIO OTA failed for {hostname}.local ({ip})")
                print(f"Error: {result.stderr}")
                print(f"Output: {result.stdout}")
                return False

        except subprocess.TimeoutExpired:
            print(f"⏱️ PlatformIO OTA timeout for {hostname}.local ({ip})")
            return False
        except Exception as e:
            print(f"❌ PlatformIO OTA error for {hostname}.local ({ip}): {e}")
            return False

    def deploy_bulk_local(self, devices, firmware_dir, max_concurrent=5):
        """Deploy to multiple devices locally"""
        print(f"\n🎯 Starting local bulk deployment...")

        # First discover all devices
        discovered_devices = self.discover_devices(devices)

        if not discovered_devices:
            print("❌ No devices discovered. Check network connectivity and device status.")
            return

        # Deploy to discovered devices
        successful_deployments = 0
        failed_deployments = 0

        def deploy_single(serial_number, device_info):
            """Deploy to single device (for threading)"""
            device = device_info['device']
            ip = device_info['ip']
            hostname = device_info['hostname']

            # Find firmware file
            firmware_path = Path(firmware_dir) / device['target_version'] / device['board_type'] / "firmware.bin"

            if not firmware_path.exists():
                print(f"❌ Firmware not found: {firmware_path}")
                return False

            return self.deploy_ota_to_device(ip, hostname, firmware_path, board_type=device['board_type'])

        # Use ThreadPoolExecutor for concurrent deployments
        with ThreadPoolExecutor(max_workers=max_concurrent) as executor:
            future_to_serial = {
                executor.submit(deploy_single, serial_number, device_info): serial_number
                for serial_number, device_info in discovered_devices.items()
            }

            for future in as_completed(future_to_serial):
                serial_number = future_to_serial[future]
                try:
                    success = future.result()
                    if success:
                        successful_deployments += 1
                    else:
                        failed_deployments += 1
                except Exception as e:
                    print(f"❌ Deployment error for {serial_number}: {e}")
                    failed_deployments += 1

        # Print summary
        total_devices = len(discovered_devices)
        print(f"\n{'='*50}")
        print(f"📊 LOCAL DEPLOYMENT SUMMARY")
        print(f"{'='*50}")
        print(f"Devices discovered: {total_devices}/{len(devices)}")
        print(f"✅ Successful deployments: {successful_deployments}")
        print(f"❌ Failed deployments: {failed_deployments}")
        print(f"📅 Completed at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")

        if failed_deployments > 0:
            print(f"\n⚠️ {failed_deployments} deployments failed. Check logs above for details.")

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

    args = parser.parse_args()

    # Validate inputs
    if not os.path.exists(args.csv):
        print(f"❌ CSV file not found: {args.csv}")
        sys.exit(1)

    if not args.discover_only and not os.path.exists(args.firmware_dir):
        print(f"❌ Firmware directory not found: {args.firmware_dir}")
        sys.exit(1)

    # Initialize deployer
    deployer = LocalBulkOTADeployer(args.network, args.ota_port)

    # Read devices from CSV
    devices = deployer.read_deployment_csv(args.csv, args.group)

    if not devices:
        print("❌ No devices to process")
        sys.exit(1)

    if args.discover_only:
        # Discovery mode only
        discovered = deployer.discover_devices(devices)
        print(f"\n📊 Discovery complete: {len(discovered)} devices found")
    elif args.dry_run:
        # Dry run mode
        print("\n🧪 DRY RUN MODE - No actual deployment will occur")
        discovered = deployer.discover_devices(devices)
        print(f"\nWould deploy to {len(discovered)} discovered devices:")
        for serial_number, info in discovered.items():
            device = info['device']
            print(f"  - {serial_number} at {info['ip']} ({device['board_type']}) -> v{device['target_version']}")
    else:
        # Deployment mode
        print(f"\n⚠️ About to deploy firmware to devices on network {args.network}")
        confirm = input("Continue? (y/N): ").strip().lower()

        if confirm != 'y':
            print("❌ Deployment cancelled")
            sys.exit(0)

        deployer.deploy_bulk_local(devices, args.firmware_dir, args.max_concurrent)

if __name__ == "__main__":
    main()