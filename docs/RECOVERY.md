# USB/serial configuration recovery

Use `tools/recover.py` when saved settings prevent the normal firmware from starting. It works with every supported BC-250 chip and both 4 MB and 8 MB flash layouts. The tool reads the partition table from the connected device, confirms it matches a BC-250 layout, and erases only `nvs` and `zb_fct`. The installed firmware, bootloader, partition table, OTA slot selection, and crash dump remain in place.

This removes controller settings, Wi-Fi credentials, the admin password, and Zigbee pairing/factory state. After recovery, the existing firmware starts first setup with all external GPIO roles disabled. The tool does not load settings or run the application during the erase.

## Run the tool

Activate an ESP-IDF environment for the chip. The tool uses ESP-IDF's Python `parttool` and `esptool`; [Espressif documents partition operations](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html#partition-tool-parttool-py). Connect the chip's primary USB/serial programming port and run:

```sh
python3 tools/recover.py --port PORT
```

For a copy downloaded from a GitHub Release, run `python3 bc250_recover.py --port PORT` instead. The tool shows the detected flash layout and erase ranges, then asks you to type `ERASE ALL`. Use `--yes` for unattended use after checking the port and device. It leaves the chip in download mode; press **RESET** after `RECOVERY COMPLETE` to start the existing firmware.

On the ESP32-C5, use the native USB Serial/JTAG port. If the device does not enter download mode automatically, hold **BOOT**, press and release **RESET**, then release **BOOT** before running the tool. The same BOOT/RESET sequence applies to other boards whose USB/serial adapter cannot reset the chip automatically. A failed or cancelled run leaves the chip in download mode; check the output and press RESET when ready.

The tool refuses an unknown partition table and does not use a guessed flash size or erase the whole chip. It is intended for the two partition layouts shipped by this project. If your device uses a custom layout, inspect its actual partition table and recover it with ESP-IDF's `parttool.py` for that layout.

After reboot, connect through the normal [first setup](../README.md#first-setup) path; H2/H21/H4 targets use [serial setup](SERIAL.md#first-setup-on-esp32-h). Zigbee devices must be paired again. If the tool reports `Recovery incomplete`, repeat it before rebooting so both partitions are erased.
