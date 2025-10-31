#!/usr/bin/env python3
"""
Firmware Serial Number Verification Tool
Scans compiled firmware binaries to verify they contain unique serial numbers
"""

import os
from pathlib import Path
import sys

def find_serial_numbers_in_firmware(firmware_path):
    """Extract all potential serial numbers from firmware binary"""
    try:
        with open(firmware_path, 'rb') as f:
            content = f.read()

        # Search for common serial number patterns
        # Looking for alphanumeric strings that could be serial numbers
        serials_found = []

        # Convert to string for searching (this works for ASCII embedded strings)
        content_str = content.decode('latin-1', errors='ignore')

        # Known serial patterns from your CSV
        known_serials = [
            "123456789123456789",
            "TL1079927967",
            "MR6384439627"
        ]

        for serial in known_serials:
            if serial in content_str:
                serials_found.append(serial)

        return serials_found
    except Exception as e:
        print(f"Error reading firmware: {e}")
        return []

def main():
    print("="*70)
    print("FIRMWARE SERIAL NUMBER VERIFICATION TOOL")
    print("="*70)
    print()

    firmware_dir = Path("firmware_compiled")

    if not firmware_dir.exists():
        print(f"[ERROR] Firmware directory not found: {firmware_dir}")
        print(f"[INFO] Please run the deployment script first to compile firmware.")
        sys.exit(1)

    # Scan all firmware directories
    firmware_map = {}

    for serial_dir in sorted(firmware_dir.iterdir()):
        if serial_dir.is_dir():
            firmware_file = serial_dir / "firmware.bin"
            if firmware_file.exists():
                serial_name = serial_dir.name
                serials_in_firmware = find_serial_numbers_in_firmware(firmware_file)
                firmware_map[serial_name] = {
                    'path': firmware_file,
                    'size': firmware_file.stat().st_size,
                    'serials_found': serials_in_firmware
                }

    if not firmware_map:
        print("[WARN] No firmware files found to verify.")
        print(f"[INFO] Expected location: {firmware_dir}/[SERIAL_NUMBER]/firmware.bin")
        sys.exit(0)

    # Display results
    print(f"[INFO] Found {len(firmware_map)} firmware file(s) to verify\n")

    all_valid = True
    duplicate_serials = {}

    for serial_name, info in firmware_map.items():
        size_kb = info['size'] / 1024
        print(f"Firmware Directory: {serial_name}")
        print(f"  File: {info['path']}")
        print(f"  Size: {size_kb:.2f} KB")
        print(f"  Serial Numbers Found in Binary:")

        if not info['serials_found']:
            print(f"    [ERROR] WARNING: No known serial numbers found!")
            all_valid = False
        else:
            for found_serial in info['serials_found']:
                if found_serial == serial_name:
                    print(f"    [OK] {found_serial} (CORRECT - matches directory name)")
                else:
                    print(f"    [ERROR] {found_serial} (WRONG - does not match directory name!)")
                    all_valid = False

                # Track duplicates
                if found_serial in duplicate_serials:
                    duplicate_serials[found_serial].append(serial_name)
                else:
                    duplicate_serials[found_serial] = [serial_name]
        print()

    # Check for duplicates
    print("="*70)
    print("VERIFICATION RESULTS")
    print("="*70)

    duplicates_found = {k: v for k, v in duplicate_serials.items() if len(v) > 1}

    if duplicates_found:
        print("\n[ERROR] DUPLICATE SERIAL NUMBERS DETECTED!")
        print("[ERROR] The following serial numbers appear in multiple firmware files:")
        for serial, directories in duplicates_found.items():
            print(f"  [ERROR] Serial '{serial}' found in:")
            for dir_name in directories:
                print(f"      - {dir_name}/firmware.bin")
        print("\n[ERROR] This means devices will have duplicate serial numbers!")
        print("[ERROR] You MUST recompile firmware to fix this issue.")
        all_valid = False

    if all_valid:
        print("\n[SUCCESS] All firmware files contain correct unique serial numbers!")
        print("[INFO] Each device will have its own serial number - no duplicates detected.")
        print("[INFO] Safe to deploy these firmware files.")
    else:
        print("\n[FAILED] Firmware verification FAILED!")
        print("[ERROR] Some firmware files have incorrect or duplicate serial numbers.")
        print("[ACTION] Please run the deployment script again to recompile firmware.")
        sys.exit(1)

    print("="*70)

if __name__ == "__main__":
    main()
