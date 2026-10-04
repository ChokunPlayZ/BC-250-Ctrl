# Upgrade a controller with saved settings

Use the **application image** (`bc250_ctrl-<profile>.bin`) to update a configured board. Keep the same chip, flash-size profile, and partition layout. These paths leave the `nvs` and `zb_fct` partitions untouched, preserving controller settings, Wi-Fi credentials, admin password, and Zigbee pairing. Note your settings before starting in case recovery is needed; the configuration API redacts passwords.

**Do not use `bc250_ctrl-<profile>-full.bin` for a settings-preserving upgrade.** The full image is written at `0x0` and its padded regions erase saved settings and Zigbee pairing. Use it for first installation or when you deliberately want to wipe the configuration and set up the board again. `erase-flash` also wipes the configuration.

Check the installed chip and flash capacity against the [supported profiles](BUILD.md#supported-profiles), and download the matching application image from the release. If you changed the partition layout or flash-size profile, these instructions do not apply; plan a fresh setup after flashing the appropriate full image.

## 8 MB: update through the web interface

Open the controller's web UI, expand **Firmware update**, select `bc250_ctrl-<profile>.bin` (without `-full`), and choose **Upload & reboot**. This writes the inactive application slot and keeps saved settings and Zigbee pairing. Wait for the reboot and the 30-second health check, then confirm the new version and your settings. A Zigbee-only configuration can open its setup AP with `wifi ap` in the [serial shell](SERIAL.md) to reach the web UI.

The web updater is unavailable on 4 MB profiles and does not accept a full image.

## Serial/USB: update from a release application image

Activate an ESP-IDF environment for the chip and connect its programming port. Replace `PORT` and the example filename with your device's port and **matching** release application image. On C5, use the native USB Serial/JTAG port. These offsets are specific to this project's unchanged partition layouts; do not use them with a custom layout.

For a **4 MB** profile, write only the factory application partition:

```sh
python -m esptool --port PORT write_flash 0x20000 bc250_ctrl-esp32c5_4mb.bin
```

For an **8 MB** profile, write the first OTA application slot, then clear only the OTA selection data so the bootloader starts that slot. Run both commands before returning the controller to service:

```sh
python -m esptool --port PORT --after no_reset write_flash 0x30000 bc250_ctrl-esp32c5_8mb.bin
python -m esptool --port PORT erase_region 0x21000 0x2000
```

The second command erases `otadata`, **not** `nvs` or `zb_fct`. It resets the previous OTA slot selection and rollback history; the new application performs its normal health check after boot. If esptool reports that it cannot enter download mode, hold **BOOT**, press and release **RESET**, then release **BOOT** and retry.

The commands above use esptool 4.x spelling, supplied with ESP-IDF 5.5. With esptool 5.x, use `write-flash`, `erase-region`, and `--after no-reset` instead.

## Serial/USB: update from a source build

From the repository root, activate the matching ESP-IDF environment and run the [build helper](BUILD.md#build-and-flash) with the board's existing profile:

```sh
python3 tools/idf_build.py esp32c5_4mb -p PORT flash monitor
```

Replace the example profile and port. The normal `flash` action writes the build's bootloader, partition table, application, and (on 8 MB profiles) initial OTA data; it does not erase `nvs` or `zb_fct` when the partition layout is unchanged. Do **not** add `erase-flash` for an upgrade.

After reboot, check the version in serial `status` or the web overview and confirm the saved settings and, if used, Zigbee pairing. If a full image already overwrote the saved partitions, reinstalling the application image cannot restore their previous contents; follow [first setup](SETUP.md) and re-enter the settings.
