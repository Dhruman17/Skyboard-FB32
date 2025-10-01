#!/usr/bin/env python3
"""
Example script demonstrating Firebase Storage firmware upload
without service account keys
"""

import os
import sys
from pathlib import Path
from firebase_storage_uploader import SecureFirebaseClient, GCSDirectUploader


def example_single_device_upload():
    """Example 1: Upload firmware for a single device"""
    print("\n" + "=" * 60)
    print("Example 1: Single Device Upload")
    print("=" * 60)

    client = SecureFirebaseClient(project_id='skyacres-marketplace')

    serial_number = 'DEVICE001'
    firmware_path = f'.pio/build/{serial_number}/firmware.bin'
    version_path = 'version.txt'

    success = client.upload_firmware_for_device(
        serial_number=serial_number,
        firmware_path=firmware_path,
        version_txt_path=version_path
    )

    if success:
        print(f"\n✓ Successfully uploaded firmware for {serial_number}")
    else:
        print(f"\n✗ Failed to upload firmware for {serial_number}")

    return success


def example_bulk_upload():
    """Example 2: Bulk upload firmware for multiple devices"""
    print("\n" + "=" * 60)
    print("Example 2: Bulk Device Upload")
    print("=" * 60)

    client = SecureFirebaseClient(project_id='skyacres-marketplace')

    # Build device dictionary from .pio/build directory
    devices = {}
    build_dir = Path('.pio/build')

    if build_dir.exists():
        for device_dir in build_dir.iterdir():
            if device_dir.is_dir():
                firmware_path = device_dir / 'firmware.bin'
                if firmware_path.exists():
                    devices[device_dir.name] = {
                        'firmware': str(firmware_path),
                        'version': 'version.txt'
                    }

    if not devices:
        # Fallback to manual list
        devices = {
            'DEVICE001': {
                'firmware': '.pio/build/DEVICE001/firmware.bin',
                'version': 'version.txt'
            },
            'DEVICE002': {
                'firmware': '.pio/build/DEVICE002/firmware.bin',
                'version': 'version.txt'
            },
            'DEVICE003': {
                'firmware': '.pio/build/DEVICE003/firmware.bin',
                'version': 'version.txt'
            }
        }

    print(f"Found {len(devices)} devices to upload:")
    for serial in devices.keys():
        print(f"  - {serial}")

    success_count, failed_devices = client.bulk_upload(devices)

    if failed_devices:
        print(f"\n⚠ Failed to upload {len(failed_devices)} device(s)")
        print("Retry needed for:", ", ".join(failed_devices))
        return False
    else:
        print(f"\n✓ All {success_count} devices uploaded successfully!")
        return True


def example_from_csv():
    """Example 3: Upload firmware from CSV device list"""
    print("\n" + "=" * 60)
    print("Example 3: Upload from CSV Device List")
    print("=" * 60)

    import csv

    client = SecureFirebaseClient(project_id='skyacres-marketplace')

    # Look for CSV file (adjust filename as needed)
    csv_files = ['devices.csv', 'device_list.csv', 'test_thq_only.csv']
    csv_path = None

    for filename in csv_files:
        if os.path.exists(filename):
            csv_path = filename
            break

    if not csv_path:
        print("✗ No device CSV file found")
        print(f"Looked for: {', '.join(csv_files)}")
        return False

    print(f"Reading devices from: {csv_path}")

    # Read CSV and build device dictionary
    devices = {}
    try:
        with open(csv_path, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                # Adjust column names based on your CSV format
                serial = row.get('serial_number') or row.get('Serial') or row.get('device_id')
                if serial:
                    devices[serial] = {
                        'firmware': f'.pio/build/{serial}/firmware.bin',
                        'version': 'version.txt'
                    }
    except Exception as e:
        print(f"✗ Error reading CSV: {e}")
        return False

    if not devices:
        print("✗ No valid devices found in CSV")
        return False

    print(f"Found {len(devices)} devices in CSV:")
    for serial in list(devices.keys())[:5]:  # Show first 5
        print(f"  - {serial}")
    if len(devices) > 5:
        print(f"  ... and {len(devices) - 5} more")

    # Upload
    success_count, failed_devices = client.bulk_upload(devices)

    if failed_devices:
        print(f"\n⚠ Failed to upload {len(failed_devices)} device(s)")
        return False
    else:
        print(f"\n✓ All {success_count} devices uploaded successfully!")
        return True


def example_gcs_direct():
    """Example 4: Using Google Cloud Storage directly (alternative method)"""
    print("\n" + "=" * 60)
    print("Example 4: GCS Direct Upload (Alternative)")
    print("=" * 60)

    try:
        client = GCSDirectUploader(bucket_name='skyacres-marketplace.appspot.com')

        serial_number = 'DEVICE001'
        firmware_path = f'.pio/build/{serial_number}/firmware.bin'
        version_path = 'version.txt'

        success = client.upload_firmware_for_device(
            serial_number=serial_number,
            firmware_path=firmware_path,
            version_txt_path=version_path
        )

        if success:
            print(f"\n✓ Successfully uploaded firmware for {serial_number} via GCS")
        else:
            print(f"\n✗ Failed to upload firmware for {serial_number}")

        return success

    except NameError:
        print("✗ GCSDirectUploader not available")
        print("Install with: pip install google-cloud-storage")
        return False


def main():
    """Main function - run examples based on command line arguments"""
    print("Firebase Storage Firmware Upload Examples")
    print("=" * 60)
    print("Using Application Default Credentials (No service account keys)")

    if len(sys.argv) < 2:
        print("\nUsage:")
        print("  python example_firebase_upload.py single    - Upload single device")
        print("  python example_firebase_upload.py bulk      - Upload multiple devices")
        print("  python example_firebase_upload.py csv       - Upload from CSV file")
        print("  python example_firebase_upload.py gcs       - Use GCS direct upload")
        print("  python example_firebase_upload.py all       - Run all examples")
        print("\nNote: Authenticate first with:")
        print("  gcloud auth application-default login")
        sys.exit(1)

    mode = sys.argv[1].lower()

    try:
        if mode == 'single':
            example_single_device_upload()
        elif mode == 'bulk':
            example_bulk_upload()
        elif mode == 'csv':
            example_from_csv()
        elif mode == 'gcs':
            example_gcs_direct()
        elif mode == 'all':
            example_single_device_upload()
            example_bulk_upload()
            example_from_csv()
            example_gcs_direct()
        else:
            print(f"✗ Unknown mode: {mode}")
            print("Valid modes: single, bulk, csv, gcs, all")
            sys.exit(1)

    except Exception as e:
        print(f"\n✗ Error: {e}")
        print("\nTroubleshooting:")
        print("1. Authenticate: gcloud auth application-default login")
        print("2. Check permissions: Ensure service account has Storage Object Admin role")
        print("3. Verify files exist: Check .pio/build/*/firmware.bin and version.txt")
        sys.exit(1)


if __name__ == '__main__':
    main()
