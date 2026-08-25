# Amandeep OTA runbook — 2026-08-25

## Confirmed scope

- Systems: A001 through A007
- Serial numbers: SAC202604011 through SAC202604017
- Hardware advertised by every controller: `esp32s3`
- PlatformIO profile: `ESP32_S3` / `esps3_board`
- Target firmware: `1.61` from current `main`
- Deployment manifest: `amandeep_local_ota_2026-08-25.csv`
- Final result: A001–A007 verified on `1.61` with fresh post-reboot
  heartbeats and Firestore OTA status `complete`

## Local-network note

The controllers advertise Arduino OTA over mDNS. Local OTA additionally
requires stable two-way traffic between the deployment computer and each
controller. A visible mDNS hostname alone is not sufficient.

Connect the computer to the same primary 2.4 GHz LAN (Ethernet is ideal) and do
not use local OTA while this check fails:

```bash
ping -c 1 192.168.2.84
```

## Safe local rollout

Run from the `Skyboard-FB32` directory.

1. Confirm all seven devices are discoverable without changing them:

   ```bash
   python3 local_bulk_ota_deploy.py \
     --csv amandeep_local_ota_2026-08-25.csv \
     --network 192.168.2.0/24 \
     --discover-only
   ```

2. Update A001 only as the pilot:

   ```bash
   python3 local_bulk_ota_deploy.py \
     --csv amandeep_local_ota_2026-08-25.csv \
     --network 192.168.2.0/24 \
     --group pilot \
     --max-concurrent 1
   ```

3. Wait for A001 to reboot. Confirm in the app/Firestore that its serial is
   `SAC202604011`, version is `1.61`, and `lastSeen` advances. Also confirm the
   lights and atomizers retain the correct settings. The expected heartbeat is
   approximately every 30 seconds.

4. Update the remaining fleet sequentially:

   ```bash
   python3 local_bulk_ota_deploy.py \
     --csv amandeep_local_ota_2026-08-25.csv \
     --network 192.168.2.0/24 \
     --group fleet \
     --max-concurrent 1
   ```

5. Verify every targeted serial/version/heartbeat value. A failed or undiscovered
   unit must remain failed in the summary; do not substitute another IP.

## Firebase OTA

Firebase OTA only installs when the storage version is greater than the version
running on the controller. The installed `1.568` firmware checked Storage every
60 seconds despite an outdated source comment that said six hours.

The controllers fetch OTA artifacts without Firebase Auth. The deployed Storage
rule therefore permits anonymous reads only for `firmware.bin` and `Version.txt`
under the seven approved serial-number paths; client writes remain denied.

For future releases:

1. Authenticate with `gcloud auth application-default login`.
2. Increase the approved firmware version in source and the deployment CSV.
3. Publish to one fresh-heartbeat pilot with
   `firebase_bulk_ota_no_key.py --group pilot`.
4. Confirm the new version and a post-reboot heartbeat in Firestore.
5. Publish to the `fleet` group and independently verify every controller.

After all devices run `1.61`, uploading `1.61` again does not exercise an
installation. Test the next approved version on one pilot before expanding.

The stale 5–6 hour app timestamps are not expected behavior for live firmware:
the current S3 loop attempts a heartbeat every 30 seconds. Local mDNS proves the
controllers are powered and on Wi-Fi, but it does not prove they can reach
Firebase. If timestamps remain stale after OTA, check DNS/internet access and
Firebase authentication from the controller network.
