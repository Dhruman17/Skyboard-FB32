"""
Secure Firebase Storage Uploader using Application Default Credentials
No service account keys required - works with gcloud auth and GCP environments
"""

import firebase_admin
from firebase_admin import credentials, storage
import os
import sys
from pathlib import Path
from typing import Dict, List, Tuple

# Fix Unicode encoding issues on Windows
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')


class SecureFirebaseClient:
    def __init__(self, project_id='skyacres-marketplace'):
        """
        Initialize Firebase Admin SDK without service account keys

        Args:
            project_id: Firebase project ID
        """
        self.project_id = project_id
        self.bucket_name = f'{project_id}.appspot.com'
        self._initialize_firebase()

    def _initialize_firebase(self):
        """Initialize Firebase using Application Default Credentials"""
        try:
            # Check if Firebase is already initialized
            firebase_admin.get_app()
            print("✓ Firebase already initialized")
        except ValueError:
            # Firebase not initialized yet
            try:
                # Use Application Default Credentials (works in GCP and local with gcloud)
                cred = credentials.ApplicationDefault()
                firebase_admin.initialize_app(cred, {
                    'storageBucket': self.bucket_name,
                    'projectId': self.project_id
                })
                print("✓ Firebase initialized using Application Default Credentials")
            except Exception as e:
                print(f"✗ Failed to initialize Firebase: {e}")
                print("\nPlease authenticate with: gcloud auth application-default login")
                raise

    def upload_file(self, local_path: str, storage_path: str) -> bool:
        """
        Upload a file to Firebase Storage

        Args:
            local_path: Path to local file
            storage_path: Destination path in Firebase Storage

        Returns:
            True if successful, False otherwise
        """
        try:
            bucket = storage.bucket()
            blob = bucket.blob(storage_path)
            blob.upload_from_filename(local_path)
            return True
        except Exception as e:
            print(f"✗ Upload failed for {storage_path}: {e}")
            return False

    def upload_firmware_for_device(self, serial_number: str, firmware_path: str,
                                   version_txt_path: str) -> bool:
        """
        Upload firmware.bin and version.txt for a specific device

        Args:
            serial_number: Device serial number
            firmware_path: Path to firmware.bin file
            version_txt_path: Path to version.txt file

        Returns:
            True if both files uploaded successfully, False otherwise
        """
        # Verify files exist
        if not os.path.exists(firmware_path):
            print(f"✗ Firmware file not found: {firmware_path}")
            return False

        if not os.path.exists(version_txt_path):
            print(f"✗ Version file not found: {version_txt_path}")
            return False

        # Upload firmware.bin
        firmware_storage_path = f'{serial_number}/firmware.bin'
        if not self.upload_file(firmware_path, firmware_storage_path):
            print(f'✗ Failed to upload firmware.bin for {serial_number}')
            return False
        print(f'✓ Uploaded firmware.bin for {serial_number}')

        # Upload version.txt
        version_storage_path = f'{serial_number}/version.txt'
        if not self.upload_file(version_txt_path, version_storage_path):
            print(f'✗ Failed to upload version.txt for {serial_number}')
            return False
        print(f'✓ Uploaded version.txt for {serial_number}')

        return True

    def bulk_upload(self, devices_dict: Dict[str, Dict[str, str]]) -> Tuple[int, List[str]]:
        """
        Upload firmware for multiple devices

        Args:
            devices_dict: Dictionary with format:
                {
                    'SERIAL_NUMBER': {
                        'firmware': 'path/to/firmware.bin',
                        'version': 'path/to/version.txt'
                    },
                    ...
                }

        Returns:
            Tuple of (success_count, list of failed device serial numbers)
        """
        success_count = 0
        failed_devices = []

        total_devices = len(devices_dict)
        print(f"\nStarting bulk upload for {total_devices} devices...")
        print("=" * 60)

        for idx, (serial_number, paths) in enumerate(devices_dict.items(), 1):
            print(f"\n[{idx}/{total_devices}] Processing {serial_number}...")
            try:
                if self.upload_firmware_for_device(
                    serial_number,
                    paths['firmware'],
                    paths['version']
                ):
                    success_count += 1
                else:
                    failed_devices.append(serial_number)
            except Exception as e:
                print(f'✗ Error uploading for {serial_number}: {e}')
                failed_devices.append(serial_number)

        # Print summary
        print(f'\n{"=" * 60}')
        print(f'Upload Summary: {success_count}/{total_devices} devices uploaded successfully')
        if failed_devices:
            print(f'Failed devices ({len(failed_devices)}): {", ".join(failed_devices)}')
        print(f'{"=" * 60}\n')

        return success_count, failed_devices


# Alternative: Direct Google Cloud Storage approach (no Firebase Admin SDK)
try:
    from google.cloud import storage as gcs_storage

    class GCSDirectUploader:
        """Alternative implementation using Google Cloud Storage directly"""
        def __init__(self, bucket_name='skyacres-marketplace.appspot.com'):
            """
            Initialize GCS client using Application Default Credentials

            Args:
                bucket_name: Full bucket name including .appspot.com
            """
            # Automatically uses ADC - no key file needed
            self.client = gcs_storage.Client()
            self.bucket = self.client.bucket(bucket_name)
            print("✓ GCS client initialized using Application Default Credentials")

        def upload_file(self, local_path: str, storage_path: str) -> bool:
            """Upload file to GCS"""
            try:
                blob = self.bucket.blob(storage_path)
                blob.upload_from_filename(local_path)
                return True
            except Exception as e:
                print(f"✗ Upload failed for {storage_path}: {e}")
                return False

        def upload_firmware_for_device(self, serial_number: str, firmware_path: str,
                                      version_txt_path: str) -> bool:
            """Upload firmware files for a device"""
            # Verify files exist
            if not os.path.exists(firmware_path):
                print(f"✗ Firmware file not found: {firmware_path}")
                return False

            if not os.path.exists(version_txt_path):
                print(f"✗ Version file not found: {version_txt_path}")
                return False

            # Upload firmware.bin
            if not self.upload_file(firmware_path, f'{serial_number}/firmware.bin'):
                return False
            print(f'✓ Uploaded firmware.bin for {serial_number}')

            # Upload version.txt
            if not self.upload_file(version_txt_path, f'{serial_number}/version.txt'):
                return False
            print(f'✓ Uploaded version.txt for {serial_number}')

            return True

        def bulk_upload(self, devices_dict: Dict[str, Dict[str, str]]) -> Tuple[int, List[str]]:
            """Upload firmware for multiple devices"""
            success_count = 0
            failed_devices = []

            total_devices = len(devices_dict)
            print(f"\nStarting bulk upload for {total_devices} devices...")
            print("=" * 60)

            for idx, (serial_number, paths) in enumerate(devices_dict.items(), 1):
                print(f"\n[{idx}/{total_devices}] Processing {serial_number}...")
                try:
                    if self.upload_firmware_for_device(
                        serial_number,
                        paths['firmware'],
                        paths['version']
                    ):
                        success_count += 1
                    else:
                        failed_devices.append(serial_number)
                except Exception as e:
                    print(f'✗ Error uploading for {serial_number}: {e}')
                    failed_devices.append(serial_number)

            # Print summary
            print(f'\n{"=" * 60}')
            print(f'Upload Summary: {success_count}/{total_devices} devices uploaded successfully')
            if failed_devices:
                print(f'Failed devices ({len(failed_devices)}): {", ".join(failed_devices)}')
            print(f'{"=" * 60}\n')

            return success_count, failed_devices

except ImportError:
    print("Note: google-cloud-storage not installed. GCSDirectUploader unavailable.")
    print("Install with: pip install google-cloud-storage")


if __name__ == '__main__':
    # Example usage
    print("Firebase Storage Uploader - Secure Authentication")
    print("=" * 60)

    # Initialize client
    client = SecureFirebaseClient(project_id='skyacres-marketplace')

    # Example 1: Upload single device
    print("\nExample: Single device upload")
    client.upload_firmware_for_device(
        serial_number='DEVICE001',
        firmware_path='.pio/build/DEVICE001/firmware.bin',
        version_txt_path='version.txt'
    )

    # Example 2: Bulk upload
    print("\nExample: Bulk device upload")
    devices = {
        'DEVICE001': {
            'firmware': '.pio/build/DEVICE001/firmware.bin',
            'version': 'version.txt'
        },
        'DEVICE002': {
            'firmware': '.pio/build/DEVICE002/firmware.bin',
            'version': 'version.txt'
        }
    }

    success_count, failed = client.bulk_upload(devices)

    if failed:
        print(f"\nRetry needed for: {failed}")
