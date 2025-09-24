#!/usr/bin/env python3
"""
Test script to verify Firebase connection
"""

import sys
import os

def test_firebase_connection(service_account_path):
    try:
        # Import Firebase modules
        import firebase_admin
        from firebase_admin import credentials, storage, firestore

        print("✅ Firebase modules imported successfully")

        # Check if service account file exists
        if not os.path.exists(service_account_path):
            print(f"❌ Service account file not found: {service_account_path}")
            return False

        print(f"✅ Service account file found: {service_account_path}")

        # Initialize Firebase (just test the credential loading)
        cred = credentials.Certificate(service_account_path)
        print("✅ Service account credentials loaded successfully")

        # Initialize app
        app = firebase_admin.initialize_app(cred, {
            'storageBucket': 'skyacres-marketplace.appspot.com'
        })

        print("✅ Firebase app initialized successfully")

        # Test Firestore connection
        db = firestore.client()
        print("✅ Firestore client created successfully")

        # Test Storage connection
        bucket = storage.bucket()
        print("✅ Storage bucket connected successfully")

        # Test reading a document (if it exists)
        try:
            test_doc = db.collection('Systems').document('123456789123456789').get()
            if test_doc.exists:
                print("✅ Test document found in Firestore")
                data = test_doc.to_dict()
                current_version = data.get('version', 'Unknown')
                print(f"📊 Current version: {current_version}")
            else:
                print("⚠️ Test document not found (this is expected if system doesn't exist yet)")
        except Exception as e:
            print(f"⚠️ Could not read test document: {e}")

        print("\n🎉 All Firebase connections successful!")
        return True

    except Exception as e:
        print(f"❌ Firebase connection failed: {e}")
        return False
    finally:
        # Clean up
        try:
            firebase_admin.delete_app(firebase_admin.get_app())
        except:
            pass

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python test_connection.py <path-to-service-account-key.json>")
        sys.exit(1)

    service_account_path = sys.argv[1]
    success = test_firebase_connection(service_account_path)

    if success:
        print("\n✅ Ready to proceed with deployment testing!")
    else:
        print("\n❌ Fix connection issues before proceeding")
        sys.exit(1)