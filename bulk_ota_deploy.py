#!/usr/bin/env python3
"""
Bulk OTA Deployment Script for SkyAcres Systems
Deploys firmware updates to multiple ESP32 devices via Firebase Storage
"""

import csv
import sys
import os
import argparse
from datetime import datetime
from pathlib import Path
import json
import time

# Firebase Admin SDK
import firebase_admin
from firebase_admin import credentials, storage, firestore
from google.cloud.exceptions import GoogleCloudError

class BulkOTADeployer:
    def __init__(self, service_account_key_path, project_id, storage_bucket):
        """Initialize Firebase Admin SDK"""
        try:
            # Initialize Firebase Admin SDK
            cred = credentials.Certificate(service_account_key_path)
            firebase_admin.initialize_app(cred, {
                'storageBucket': storage_bucket
            })

            self.db = firestore.client()
            self.bucket = storage.bucket()
            self.project_id = project_id

            print("[OK] Firebase Admin SDK initialized successfully")
        except Exception as e:
            print(f"[ERROR] Failed to initialize Firebase: {e}")
            sys.exit(1)

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
                for row_num, row in enumerate(reader, start=2):  # Start at 2 for header
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
            print(f"[ERROR] CSV file not found: {csv_file_path}")
            sys.exit(1)
        except Exception as e:
            print(f"[ERROR] Error reading CSV: {e}")
            sys.exit(1)

    def upload_firmware_to_storage(self, serial_number, firmware_path, target_version):
        """Upload firmware.bin and Version.txt to Firebase Storage"""
        try:
            # Upload firmware.bin
            firmware_blob_name = f"{serial_number}/firmware.bin"
            firmware_blob = self.bucket.blob(firmware_blob_name)

            print(f"📤 Uploading firmware for {serial_number}...")
            firmware_blob.upload_from_filename(firmware_path)

            # Create and upload Version.txt
            version_blob_name = f"{serial_number}/Version.txt"
            version_blob = self.bucket.blob(version_blob_name)
            version_blob.upload_from_string(target_version)

            print(f"✅ Uploaded firmware and version for {serial_number}")
            return True

        except Exception as e:
            print(f"❌ Failed to upload firmware for {serial_number}: {e}")
            return False

    def update_firestore_deployment_status(self, serial_number, target_version, status="pending"):
        """Update Firestore with deployment information"""
        try:
            doc_ref = self.db.collection('Systems').document(serial_number)

            # Get current document to preserve existing data
            current_doc = doc_ref.get()
            if not current_doc.exists:
                print(f"⚠️ Firestore document not found for {serial_number}")
                return False

            # Update with deployment info
            update_data = {
                'ota_status': status,
                'ota_target_version': float(target_version),
                'ota_deployed_at': firestore.SERVER_TIMESTAMP,
                'firmware_url': f"https://firebasestorage.googleapis.com/v0/b/{self.bucket.name}/o/{serial_number}%2Ffirmware.bin?alt=media"
            }

            # Don't update the version field yet - let the device do it after successful OTA
            doc_ref.update(update_data)

            print(f"✅ Updated Firestore deployment status for {serial_number}")
            return True

        except Exception as e:
            print(f"❌ Failed to update Firestore for {serial_number}: {e}")
            return False

    def compile_device_firmware(self, serial_number, board_type, target_version):
        """Compile device-specific firmware with unique serial number"""
        import tempfile
        import shutil

        print(f"🔨 Compiling device-specific firmware for {serial_number}...")

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
                print(f"❌ PlatformIO not found. Please install PlatformIO.")
                return None

            # Compile firmware
            import subprocess
            cmd = pio_cmd + ['run', '-e', env]
            print(f"📦 Compiling: {' '.join(cmd)}")

            result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)

            if result.returncode == 0:
                # Copy compiled firmware to device-specific location
                compiled_firmware = Path(f".pio/build/{env}/firmware.bin")
                if compiled_firmware.exists():
                    device_firmware_dir = Path("firmware_compiled") / serial_number
                    device_firmware_dir.mkdir(parents=True, exist_ok=True)
                    device_firmware_path = device_firmware_dir / "firmware.bin"
                    shutil.copy2(compiled_firmware, device_firmware_path)

                    print(f"✅ Device-specific firmware compiled: {device_firmware_path}")
                    return str(device_firmware_path)
                else:
                    print(f"❌ Compiled firmware not found at {compiled_firmware}")
                    return None
            else:
                print(f"❌ Compilation failed:")
                print(f"STDOUT: {result.stdout}")
                print(f"STDERR: {result.stderr}")
                return None

        except Exception as e:
            print(f"❌ Compilation error: {e}")
            return None
        finally:
            # Restore original credentials.h
            if backup_creds.exists():
                shutil.copy2(backup_creds, original_creds)
                backup_creds.unlink()  # Delete backup

    def find_platformio_executable(self):
        """Find PlatformIO executable"""
        import subprocess
        import os
        from pathlib import Path

        # Try common PlatformIO locations
        possible_commands = [
            ['pio'],
            ['platformio'],
            ['python', '-m', 'platformio'],
            [str(Path.home() / '.platformio' / 'penv' / 'Scripts' / 'pio.exe')],
            [str(Path.home() / '.platformio' / 'penv' / 'Scripts' / 'platformio.exe')]
        ]

        for cmd in possible_commands:
            try:
                result = subprocess.run(cmd + ['--version'], capture_output=True, timeout=10)
                if result.returncode == 0:
                    return cmd
            except:
                continue

        return None

    def deploy_single_device(self, device, firmware_dir):
        """Deploy OTA update to a single device"""
        serial_number = device['serial_number']
        board_type = device['board_type']
        target_version = device['target_version']

        print(f"\n🚀 Deploying to {serial_number}...")

        # Step 1: Compile device-specific firmware
        firmware_path = self.compile_device_firmware(serial_number, board_type, target_version)
        if not firmware_path:
            print(f"❌ Failed to compile firmware for {serial_number}")
            return False

        # Step 2: Upload firmware and version to Firebase Storage
        if not self.upload_firmware_to_storage(serial_number, firmware_path, target_version):
            return False

        # Step 3: Update Firestore with deployment status
        if not self.update_firestore_deployment_status(serial_number, target_version):
            return False

        print(f"✅ Successfully deployed to {serial_number}")
        return True

    def deploy_bulk(self, devices, firmware_dir):
        """Deploy to multiple devices"""
        successful_deployments = 0
        failed_deployments = 0

        print(f"\n🎯 Starting bulk deployment to {len(devices)} devices...")
        start_time = datetime.now()

        for i, device in enumerate(devices, 1):
            print(f"\n--- Device {i}/{len(devices)} ---")

            if self.deploy_single_device(device, firmware_dir):
                successful_deployments += 1
            else:
                failed_deployments += 1

            # Add small delay between deployments
            if i < len(devices):
                time.sleep(1)

        end_time = datetime.now()
        duration = end_time - start_time

        # Print summary
        print(f"\n{'='*50}")
        print(f"📊 DEPLOYMENT SUMMARY")
        print(f"{'='*50}")
        print(f"Total devices: {len(devices)}")
        print(f"✅ Successful: {successful_deployments}")
        print(f"❌ Failed: {failed_deployments}")
        print(f"⏱️ Duration: {duration}")
        print(f"📅 Completed at: {end_time.strftime('%Y-%m-%d %H:%M:%S')}")

        if failed_deployments > 0:
            print(f"\n⚠️ {failed_deployments} deployments failed. Check logs above for details.")

    def monitor_deployment_status(self, csv_file_path):
        """Monitor deployment status by checking device lastSeen timestamps"""
        devices = self.read_deployment_csv(csv_file_path)

        print(f"\n👀 Monitoring deployment status for {len(devices)} devices...")
        print(f"{'Serial Number':<20} {'Status':<15} {'Current Version':<15} {'Last Seen':<20}")
        print("-" * 75)

        for device in devices:
            serial_number = device['serial_number']
            try:
                doc_ref = self.db.collection('Systems').document(serial_number)
                doc = doc_ref.get()

                if doc.exists:
                    data = doc.to_dict()
                    current_version = data.get('version', 'Unknown')
                    ota_status = data.get('ota_status', 'Unknown')
                    last_seen = data.get('lastSeen', 'Unknown')

                    # Format last seen
                    if hasattr(last_seen, 'strftime'):
                        last_seen_str = last_seen.strftime('%Y-%m-%d %H:%M:%S')
                    else:
                        last_seen_str = str(last_seen)[:20]

                    print(f"{serial_number:<20} {ota_status:<15} {current_version:<15} {last_seen_str:<20}")
                else:
                    print(f"{serial_number:<20} {'NOT_FOUND':<15} {'N/A':<15} {'N/A':<20}")

            except Exception as e:
                print(f"{serial_number:<20} {'ERROR':<15} {'N/A':<15} {'N/A':<20}")

def main():
    parser = argparse.ArgumentParser(description='Bulk OTA Deployment for SkyAcres Systems')
    parser.add_argument('--csv', required=True, help='Path to deployment CSV file')
    parser.add_argument('--firmware-dir', default='./firmware', help='Directory containing firmware files')
    parser.add_argument('--service-account', required=True, help='Path to Firebase service account key JSON')
    parser.add_argument('--project-id', default='skyacres-marketplace', help='Firebase project ID')
    parser.add_argument('--storage-bucket', default='skyacres-marketplace.appspot.com', help='Firebase storage bucket')
    parser.add_argument('--group', help='Deploy only to specific deployment group')
    parser.add_argument('--monitor', action='store_true', help='Monitor deployment status instead of deploying')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be deployed without actually deploying')

    args = parser.parse_args()

    # Validate inputs
    if not os.path.exists(args.csv):
        print(f"[ERROR] CSV file not found: {args.csv}")
        sys.exit(1)

    if not os.path.exists(args.service_account):
        print(f"[ERROR] Service account key not found: {args.service_account}")
        sys.exit(1)

    if not args.monitor and not os.path.exists(args.firmware_dir):
        print(f"[ERROR] Firmware directory not found: {args.firmware_dir}")
        sys.exit(1)

    # Initialize deployer
    deployer = BulkOTADeployer(args.service_account, args.project_id, args.storage_bucket)

    if args.monitor:
        # Monitor mode
        deployer.monitor_deployment_status(args.csv)
    else:
        # Deployment mode
        devices = deployer.read_deployment_csv(args.csv, args.group)

        if not devices:
            print("❌ No devices to deploy")
            sys.exit(1)

        if args.dry_run:
            print("\n🧪 DRY RUN MODE - No actual deployment will occur")
            print(f"Would deploy to {len(devices)} devices:")
            for device in devices:
                print(f"  - {device['serial_number']} ({device['board_type']}) -> v{device['target_version']}")
        else:
            # Confirm deployment
            print(f"\n⚠️ About to deploy firmware to {len(devices)} devices.")
            confirm = input("Continue? (y/N): ").strip().lower()

            if confirm != 'y':
                print("❌ Deployment cancelled")
                sys.exit(0)

            deployer.deploy_bulk(devices, args.firmware_dir)

if __name__ == "__main__":
    main()