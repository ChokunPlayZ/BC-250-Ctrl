# Standalone configuration recovery

Use this image when saved settings prevent the normal firmware from starting, including GPIO 12 or 14 assignments on the NodeMCU ESP32-C5 Mini.

The recovery firmware erases the entire `nvs` partition and the `zb_fct` partition. This removes active and pending controller settings, Wi-Fi credentials, the admin password, and Zigbee network/factory state. It never loads controller settings, starts radios, or configures external GPIO roles. It prints `RECOVERY COMPLETE` and stays idle. Every boot of this image repeats the erase; flash normal firmware after recovery.

GPIO 12 and 14 are blocked in normal C5 firmware following the reported Mini boot failures. Espressif also documents GPIO 14 as a [native USB Serial/JTAG pin](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-reference/peripherals/gpio.html); assigning it as a normal GPIO disables that USB function.

## Build and flash over USB

Activate the ESP-IDF environment as for normal firmware. Match the recovery profile to the unit's flash capacity:

| Flash | Recovery profile | Normal profile |
|---|---|---|
| 4 MB | `esp32c5_4mb_recovery` | `esp32c5_4mb` |
| 8 MB | `esp32c5_8mb_recovery` | `esp32c5_8mb` |

For a 4 MB unit:

```sh
python3 tools/idf_build.py esp32c5_4mb_recovery build merge-bin -o bc250_recovery-full.bin
python3 tools/idf_build.py esp32c5_4mb_recovery -p PORT flash monitor
```

Use `esp32c5_8mb_recovery` for an 8 MB unit. The helper builds a separate minimal project under `recovery/`; it uses the same partition layout as the matching normal profile. Recovery does not depend on the Zigbee/BLE application libraries.

If the unit is unresponsive, disconnect circuitry from GPIO 12/14 and use the board's BOOT and RESET buttons to enter ROM download mode: hold BOOT, press and release RESET, then release BOOT. Connect to the native USB port and select its serial port. Flashing replaces the installed application; it does not require the broken application or its web interface to run.

Wait for `RECOVERY COMPLETE` in the serial monitor. An erase error or `recovery incomplete` means the reset did not finish successfully. Exit the monitor with Ctrl+], then flash normal firmware:

```sh
python3 tools/idf_build.py esp32c5_4mb -p PORT flash monitor
```

Use `esp32c5_8mb` for an 8 MB unit. The normal firmware then performs first setup with all external GPIO roles disabled, generates a new admin password, and opens `BC250-Ctrl-XXXX`. Assign different pins before saving. Zigbee must be paired again.

## Single-file image

The merged image is `build/<recovery-profile>/bc250_recovery-full.bin`. It includes the bootloader, partition table, application and, for 8 MB, initial OTA selection data. Flash this **full image at address `0x0`**, over USB/serial only. For example:

```sh
python3 -m esptool --chip esp32c5 --port PORT write_flash 0x0 build/esp32c5_4mb_recovery/bc250_recovery-full.bin
```

Use the 8 MB profile's path for an 8 MB unit. Do not upload the full recovery image through the web OTA page. The separate `bc250_recovery.bin` is application-only and must not be flashed at `0x0`; use the full image or the build helper's `flash` action.

After recovery, you can also use a merged normal firmware image. Build it with the matching profile:

```sh
python3 tools/idf_build.py esp32c5_4mb build merge-bin -o bc250_ctrl-full.bin
```

Flash `build/esp32c5_4mb/bc250_ctrl-full.bin` at `0x0` after `RECOVERY COMPLETE`. For 8 MB, use the `esp32c5_8mb` profile and path. These full images include blank data between the bootloader and application, so they clear saved settings as part of flashing; use the helper's ordinary `flash` action when you need to retain settings during a normal update.

For source builds, a complete `erase-flash` followed by normal firmware flashing remains an alternative:

```sh
python3 tools/idf_build.py esp32c5_4mb -p PORT erase-flash flash monitor
```

That alternative erases the whole chip, including all application slots and crash dumps.
