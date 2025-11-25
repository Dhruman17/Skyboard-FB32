#!/usr/bin/env python3
"""
Firebase Bulk OTA Deployment using Application Default Credentials
No service account key file required - uses gcloud auth
"""

import csv
import sys
import os
import argparse
from pathlib import Path
from datetime import datetime
import firebase_admin
from firebase_admin import credentials, storage, firestore
from firebase_storage_uploader import SecureFirebaseClient


def read_csv(csv_path, deployment_group=None):
    """Read devices from CSV file"""
    devices = []
    print(f"📖 Reading deployment list from: {csv_path}")

    try:
        with open(csv_path, 'r', newline='') as csvfile:
            reader = csv.DictReader(csvfile)
            for row_num, row in enumerate(reader, start=2):
                # Filter by deployment group if specified
                if deployment_group and row.get('deployment_group') != deployment_group:
                    continue

                serial_number = row.get('serial_number')
                board_type = row.get('board_type')
                target_version = row.get('target_version')

                if not all([serial_number, board_type, target_version]):
                    print(f"⚠️  Row {row_num}: Missing required fields")
                    continue

                devices.append(row)
                print(f"✅ Added device: {serial_number} ({board_type}) -> v{target_version}")

        print(f"📊 Total devices to deploy: {len(devices)}")
        return devices

    except FileNotFoundError:
        print(f"❌ CSV file not found: {csv_path}")
        sys.exit(1)
    except Exception as e:
        print(f"❌ Error reading CSV: {e}")
        sys.exit(1)


def find_platformio_executable():
    """Find PlatformIO executable on the system"""
    import subprocess

    # Try common PlatformIO locations
    pio_path = Path.home() / '.platformio' / 'penv' / 'Scripts' / 'pio.exe'

    # First, try the most common location directly
    if pio_path.exists():
        try:
            result = subprocess.run(
                [str(pio_path), '--version'],
                capture_output=True,
                timeout=10
            )
            if result.returncode == 0:
                print(f"✅ Found PlatformIO: {pio_path}")
                return [str(pio_path)]
        except Exception as e:
            print(f"⚠️  Error testing {pio_path}: {e}")

    # Try other common locations
    possible_commands = [
        [str(Path.home() / '.platformio' / 'penv' / 'Scripts' / 'platformio.exe')],
        ['pio'],
        ['platformio'],
        ['python', '-m', 'platformio'],
    ]

    for cmd in possible_commands:
        try:
            result = subprocess.run(
                cmd + ['--version'],
                capture_output=True,
                timeout=10
            )
            if result.returncode == 0:
                print(f"✅ Found PlatformIO: {' '.join(cmd)}")
                return cmd
        except Exception:
            continue

    print(f"❌ PlatformIO not found in any standard location")
    return None


def compile_device_firmware(serial_number, board_type, target_version):
    """Compile device-specific firmware"""
    import subprocess
    import shutil

    print(f"🔨 Compiling firmware for {serial_number}...")

    # Initialize backup variables for finally block
    backup_creds = None
    backup_config = None
    backup_version = None
    original_creds = Path("src/credentials.h")
    original_config = Path("src/config.h")
    original_version = Path("src/Version.txt")

    try:
        # Find PlatformIO
        pio_cmd = find_platformio_executable()
        if not pio_cmd:
            print(f"❌ PlatformIO not found. Please install PlatformIO.")
            print("   Install with: pip install platformio")
            return None

        # Backup original files
        backup_creds = Path("src/credentials.h.backup")
        backup_config = Path("src/config.h.backup")
        backup_version = Path("src/Version.txt.backup")

        if original_creds.exists():
            shutil.copy2(original_creds, backup_creds)
        if original_config.exists():
            shutil.copy2(original_config, backup_config)
        if original_version.exists():
            shutil.copy2(original_version, backup_version)

        # Update Version.txt with target version
        with open(original_version, 'w') as f:
            f.write(target_version)
        print(f"✅ Updated Version.txt to {target_version}")

        # Update config.h with target version
        if original_config.exists():
            with open(original_config, 'r') as f:
                config_content = f.read()

            # Replace firmware_version line
            import re
            updated_config = re.sub(
                r'const double firmware_version = [\d.]+;.*',
                f'const double firmware_version = {target_version}; // Auto-updated by deployment script',
                config_content
            )

            with open(original_config, 'w') as f:
                f.write(updated_config)
            print(f"✅ Updated config.h firmware_version to {target_version}")

        # Create device-specific credentials
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

        # Write device-specific credentials
        with open(original_creds, 'w') as f:
            f.write(temp_credentials)

        # Determine PlatformIO environment
        if board_type == 'ESP32_S3':
            env = 'esps3_board'
        elif board_type == 'ESP32_THREE_PORT':
            env = 'esp32dev_3port'
        else:
            env = 'esps3_board'

        # Compile firmware
        cmd = pio_cmd + ['run', '-e', env]
        print(f"📦 Running: {' '.join(cmd)}")

        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)

        if result.returncode == 0:
            # Copy compiled firmware
            compiled_firmware = Path(f".pio/build/{env}/firmware.bin")
            if compiled_firmware.exists():
                device_firmware_dir = Path("firmware_compiled") / serial_number
                device_firmware_dir.mkdir(parents=True, exist_ok=True)
                device_firmware_path = device_firmware_dir / "firmware.bin"
                shutil.copy2(compiled_firmware, device_firmware_path)

                print(f"✅ Firmware compiled: {device_firmware_path}")
                return str(device_firmware_path)
            else:
                print(f"❌ Compiled firmware not found")
                return None
        else:
            print(f"❌ Compilation failed:")
            print(result.stderr[:500])
            return None

    except Exception as e:
        print(f"❌ Compilation error: {e}")
        return None

    finally:
        # Restore original files
        if backup_creds and backup_creds.exists():
            shutil.copy2(backup_creds, original_creds)
            backup_creds.unlink()
        if backup_config and backup_config.exists():
            shutil.copy2(backup_config, original_config)
            backup_config.unlink()
        if backup_version and backup_version.exists():
            shutil.copy2(backup_version, original_version)
            backup_version.unlink()


def upload_firmware_to_storage(client, serial_number, firmware_path, target_version):
    """Upload firmware to Firebase Storage"""
    try:
        bucket = storage.bucket()

        # Upload firmware.bin
        firmware_blob = bucket.blob(f"{serial_number}/firmware.bin")
        print(f"📤 Uploading firmware for {serial_number}...")
        firmware_blob.upload_from_filename(firmware_path)

        # Upload Version.txt
        version_blob = bucket.blob(f"{serial_number}/Version.txt")
        version_blob.upload_from_string(target_version)

        print(f"✅ Uploaded firmware and version for {serial_number}")
        return True

    except Exception as e:
        print(f"❌ Upload failed for {serial_number}: {e}")
        return False


def update_firestore_status(db, serial_number, target_version):
    """Update Firestore with deployment info"""
    try:
        doc_ref = db.collection('Systems').document(serial_number)
        current_doc = doc_ref.get()

        if not current_doc.exists:
            print(f"⚠️  Firestore document not found for {serial_number}")
            return False

        bucket_name = storage.bucket().name
        update_data = {
            'ota_status': 'pending',
            'ota_target_version': float(target_version),
            'ota_deployed_at': firestore.SERVER_TIMESTAMP,
            'firmware_url': f"https://firebasestorage.googleapis.com/v0/b/{bucket_name}/o/{serial_number}%2Ffirmware.bin?alt=media"
        }

        doc_ref.update(update_data)
        print(f"✅ Updated Firestore for {serial_number}")
        return True

    except Exception as e:
        print(f"❌ Firestore update failed for {serial_number}: {e}")
        return False


def deploy_single_device(device, db):
    """Deploy to a single device"""
    serial_number = device['serial_number']
    board_type = device['board_type']
    target_version = device['target_version']

    print(f"\n🚀 Deploying to {serial_number}...")

    # Step 1: Compile firmware
    firmware_path = compile_device_firmware(serial_number, board_type, target_version)
    if not firmware_path:
        return False

    # Step 2: Upload to Firebase Storage
    if not upload_firmware_to_storage(None, serial_number, firmware_path, target_version):
        return False

    # Step 3: Update Firestore
    if not update_firestore_status(db, serial_number, target_version):
        return False

    print(f"✅ Successfully deployed to {serial_number}")
    return True


def main():
    parser = argparse.ArgumentParser(
        description='Firebase Bulk OTA Deployment (No service account key required)',
        epilog='Note: Authenticate first with: gcloud auth application-default login'
    )
    parser.add_argument('--csv', required=True, help='Path to deployment CSV file')
    parser.add_argument('--project-id', default='skyacres-marketplace', help='Firebase project ID')
    parser.add_argument('--storage-bucket', default='skyacres-marketplace.appspot.com', help='Firebase storage bucket')
    parser.add_argument('--group', help='Deploy only to specific deployment group')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be deployed without deploying')
    parser.add_argument('--yes', action='store_true', help='Skip confirmation prompt')

    args = parser.parse_args()

    # Validate CSV exists
    if not os.path.exists(args.csv):
        print(f"❌ CSV file not found: {args.csv}")
        sys.exit(1)

    print("=" * 60)
    print("Firebase Bulk OTA Deployment")
    print("Using Application Default Credentials")
    print("=" * 60)

    # Initialize Firebase with Application Default Credentials
    try:
        cred = credentials.ApplicationDefault()
        firebase_admin.initialize_app(cred, {
            'storageBucket': args.storage_bucket,
            'projectId': args.project_id
        })
        db = firestore.client()
        print("✅ Firebase initialized successfully\n")
    except Exception as e:
        print(f"❌ Failed to initialize Firebase: {e}")
        print("\n💡 Please authenticate first with:")
        print("   gcloud auth application-default login")
        sys.exit(1)

    # Read devices from CSV
    devices = read_csv(args.csv, args.group)

    if not devices:
        print("❌ No devices to deploy")
        sys.exit(1)

    # Dry run mode
    if args.dry_run:
        print("\n🧪 DRY RUN MODE - No actual deployment")
        print(f"Would deploy to {len(devices)} devices:")
        for device in devices:
            print(f"  - {device['serial_number']} ({device['board_type']}) -> v{device['target_version']}")
        sys.exit(0)

    # Confirm deployment
    if not args.yes:
        print(f"\n⚠️  About to deploy firmware to {len(devices)} devices.")
        confirm = input("Continue? (y/N): ").strip().lower()
        if confirm != 'y':
            print("❌ Deployment cancelled")
            sys.exit(0)

    # Deploy to all devices
    print(f"\n🎯 Starting deployment to {len(devices)} devices...")
    start_time = datetime.now()

    successful = 0
    failed = 0

    for i, device in enumerate(devices, 1):
        print(f"\n--- Device {i}/{len(devices)} ---")
        if deploy_single_device(device, db):
            successful += 1
        else:
            failed += 1

    end_time = datetime.now()
    duration = end_time - start_time

    # Print summary
    print(f"\n{'=' * 60}")
    print(f"📊 DEPLOYMENT SUMMARY")
    print(f"{'=' * 60}")
    print(f"Total devices: {len(devices)}")
    print(f"✅ Successful: {successful}")
    print(f"❌ Failed: {failed}")
    print(f"⏱️  Duration: {duration}")
    print(f"📅 Completed: {end_time.strftime('%Y-%m-%d %H:%M:%S')}")

    if failed > 0:
        print(f"\n⚠️  {failed} deployments failed. Check logs above for details.")


if __name__ == "__main__":
    main()
