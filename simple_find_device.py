#!/usr/bin/env python3
"""
Simple device discovery tool for SkyAcres ESP32 devices (Windows compatible)
"""

import subprocess
import socket
import sys

def test_hostname(hostname):
    """Test if hostname resolves and responds"""
    print(f"Testing hostname: {hostname}")

    # Try ping
    try:
        result = subprocess.run(['ping', '-n', '1', '-w', '2000', hostname],
                              capture_output=True, text=True, timeout=5)
        if result.returncode == 0:
            print(f"[SUCCESS] Ping successful!")
            print(result.stdout.split('\n')[1])  # Show ping result line
            return True
        else:
            print(f"[FAIL] Ping failed")
    except Exception as e:
        print(f"[ERROR] Ping error: {e}")

    return False

def test_ota_port(ip, port=3232):
    """Test if OTA port is open"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        result = sock.connect_ex((ip, port))
        sock.close()
        if result == 0:
            print(f"[SUCCESS] OTA port {port} is open on {ip}")
            return True
        else:
            print(f"[FAIL] OTA port {port} is closed on {ip}")
            return False
    except Exception as e:
        print(f"[ERROR] Port test error: {e}")
        return False

def main():
    if len(sys.argv) < 2:
        print("Usage: python simple_find_device.py <device_serial_number>")
        print("Example: python simple_find_device.py 123456789123456789")
        sys.exit(1)

    serial_number = sys.argv[1]

    print(f"Looking for device: {serial_number}")
    print("="*50)

    # Test different hostname patterns
    hostnames = [
        serial_number,
        f"{serial_number}.local",
        f"skyacres-{serial_number}",
        f"skyacres-{serial_number}.local",
        f"system-{serial_number}",
        f"system-{serial_number}.local",
        f"esp32-{serial_number}",
        f"esp32-{serial_number}.local"
    ]

    found_ips = []

    for hostname in hostnames:
        print(f"\nTesting: {hostname}")
        if test_hostname(hostname):
            # Try to extract IP for port testing
            try:
                result = subprocess.run(['ping', '-n', '1', '-w', '2000', hostname],
                                      capture_output=True, text=True, timeout=5)
                if result.returncode == 0:
                    lines = result.stdout.split('\n')
                    for line in lines:
                        if 'Pinging' in line and '[' in line and ']' in line:
                            ip = line.split('[')[1].split(']')[0]
                            if '.' in ip and '::' not in ip:  # IPv4
                                found_ips.append((ip, hostname))
                                test_ota_port(ip)
                                break
            except:
                pass

    if found_ips:
        print(f"\n[SUCCESS] Found {len(found_ips)} IPv4 addresses:")
        for ip, hostname in found_ips:
            print(f"  - {hostname}: {ip}")

        print(f"\nTo use in deployment, add this IP to your CSV:")
        print(f"   {serial_number},...,...,...,...,{found_ips[0][0]}")
    else:
        print(f"\n[FAIL] Device {serial_number} not found on network")
        print("Tips:")
        print("  - Device is powered on and connected to WiFi")
        print("  - Device is on the same network as this computer")
        print("  - ArduinoOTA is enabled in the firmware")

if __name__ == "__main__":
    main()