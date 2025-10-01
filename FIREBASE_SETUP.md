# Firebase Storage Upload Setup Guide

This guide explains how to set up secure Firebase Storage uploads without service account keys.

## Why No Service Account Keys?

Your organization has the `iam.disableServiceAccountKeyCreation` policy enabled, which prevents generating JSON key files. This is a **security best practice** that:
- Eliminates long-lived credentials
- Reduces risk of key leakage
- Uses short-lived, auto-rotating tokens
- Provides better audit trails

## Setup Instructions

### 1. Install Dependencies

```bash
pip install firebase-admin google-cloud-storage google-auth
```

### 2. Authenticate with Application Default Credentials

#### Local Development (Your Computer)

Run this command once to authenticate:

```bash
gcloud auth application-default login
```

This will:
1. Open your browser
2. Ask you to log in with your Google account
3. Store credentials locally at:
   - Windows: `%APPDATA%\gcloud\application_default_credentials.json`
   - Mac/Linux: `~/.config/gcloud/application_default_credentials.json`

These credentials will work for all local scripts until you log out or they expire.

#### Verify Authentication

```bash
gcloud auth application-default print-access-token
```

If this prints a token, you're authenticated correctly.

### 3. Grant IAM Permissions

Your service account needs Storage permissions. Run:

```bash
gcloud projects add-iam-policy-binding skyacres-marketplace \
  --member="serviceAccount:firebase-adminsdk-ydv19@skyacres-marketplace.iam.gserviceaccount.com" \
  --role="roles/storage.objectAdmin"
```

Or use the Firebase Console:
1. Go to [IAM & Admin](https://console.cloud.google.com/iam-admin/iam?project=skyacres-marketplace)
2. Find `firebase-adminsdk-ydv19@skyacres-marketplace.iam.gserviceaccount.com`
3. Click Edit (pencil icon)
4. Add role: **Storage Object Admin**

### 4. Test the Setup

```bash
python firebase_storage_uploader.py
```

You should see:
```
✓ Firebase initialized using Application Default Credentials
```

## Usage

### Basic Usage

```python
from firebase_storage_uploader import SecureFirebaseClient

# Initialize client
client = SecureFirebaseClient(project_id='skyacres-marketplace')

# Upload firmware for a single device
client.upload_firmware_for_device(
    serial_number='DEVICE001',
    firmware_path='.pio/build/DEVICE001/firmware.bin',
    version_txt_path='version.txt'
)
```

### Bulk Upload

```python
from firebase_storage_uploader import SecureFirebaseClient

client = SecureFirebaseClient(project_id='skyacres-marketplace')

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

success_count, failed_devices = client.bulk_upload(devices)

if failed_devices:
    print(f"Failed uploads: {failed_devices}")
```

### Using GCS Directly (Alternative)

If you prefer to use Google Cloud Storage directly without Firebase Admin SDK:

```python
from firebase_storage_uploader import GCSDirectUploader

client = GCSDirectUploader(bucket_name='skyacres-marketplace.appspot.com')

client.upload_firmware_for_device(
    serial_number='DEVICE001',
    firmware_path='.pio/build/DEVICE001/firmware.bin',
    version_txt_path='version.txt'
)
```

## Troubleshooting

### Error: "Could not automatically determine credentials"

**Problem:** ADC credentials not found

**Solution:**
```bash
gcloud auth application-default login
```

### Error: "Permission denied"

**Problem:** Service account lacks permissions

**Solution:** Grant Storage Object Admin role (see step 3 above)

### Error: "The caller does not have permission"

**Problem:** You're authenticated with wrong account

**Solution:**
```bash
# Check current account
gcloud auth list

# Switch to correct account
gcloud config set account YOUR_EMAIL@example.com
gcloud auth application-default login
```

### Error: "403 Forbidden" when uploading

**Problem:** Service account permissions not propagated yet

**Solution:** Wait 1-2 minutes for IAM changes to propagate, then retry

## CI/CD Setup (GitHub Actions, Cloud Build, etc.)

### GitHub Actions with Workload Identity Federation

```yaml
name: Upload Firmware

on: [push]

jobs:
  upload:
    runs-on: ubuntu-latest

    permissions:
      contents: read
      id-token: write

    steps:
      - uses: actions/checkout@v3

      - uses: google-github-actions/auth@v1
        with:
          workload_identity_provider: 'projects/PROJECT_NUMBER/locations/global/workloadIdentityPools/POOL_ID/providers/PROVIDER_ID'
          service_account: 'firebase-adminsdk-ydv19@skyacres-marketplace.iam.gserviceaccount.com'

      - name: Set up Python
        uses: actions/setup-python@v4
        with:
          python-version: '3.9'

      - name: Install dependencies
        run: pip install firebase-admin google-cloud-storage

      - name: Upload firmware
        run: python firebase_storage_uploader.py
```

### Cloud Run / Compute Engine / GKE

No setup needed! ADC automatically uses the attached service account.

Just deploy your code and it will work automatically.

## Security Best Practices ✓

✅ **No service account keys** - Uses ADC/Workload Identity
✅ **Short-lived credentials** - Tokens auto-rotate every hour
✅ **Least privilege** - Only grant necessary Storage permissions
✅ **Works everywhere** - Local dev, GCP, CI/CD
✅ **Audit trail** - All actions logged to Cloud Audit Logs

## How It Works

### Application Default Credentials (ADC) Flow

1. **Local Development:**
   - Uses credentials from `gcloud auth application-default login`
   - Tokens refresh automatically

2. **GCP Environments (Cloud Run, Compute Engine, etc.):**
   - Uses attached service account
   - No manual authentication needed

3. **CI/CD (GitHub Actions, etc.):**
   - Uses Workload Identity Federation
   - Short-lived tokens exchanged automatically

The library automatically detects which environment it's in and uses the appropriate credentials.

## Additional Resources

- [Application Default Credentials Documentation](https://cloud.google.com/docs/authentication/application-default-credentials)
- [Workload Identity Federation](https://cloud.google.com/iam/docs/workload-identity-federation)
- [Firebase Admin SDK](https://firebase.google.com/docs/admin/setup)
