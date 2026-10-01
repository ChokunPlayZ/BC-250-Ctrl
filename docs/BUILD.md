# Build, flash, and release images

## Requirements

Install and activate [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html). The component manifest accepts IDF `>=5.5.2,<6.2.0`. The versions below follow the CI and release workflows. Use IDF 6.1 for ESP32, S3, H21, and H4; H21/H4 BLE and Zigbee are unavailable in 5.5.4.

ESP32-S2 and ESP32-P4 are excluded because they lack onboard BLE. The H targets have no Wi-Fi or web interface; they use the serial shell and Zigbee.

## Supported profiles

Choose the profile matching **both** the chip and the module's actual flash capacity. Every 8 MB profile supports web OTA; 4 MB profiles use serial/USB updates.

| Chip | Profiles | CI/release IDF |
|---|---|---|
| ESP32 | `esp32_4mb`, `esp32_8mb` | 6.1 |
| ESP32-S3 | `esp32s3_4mb`, `esp32s3_8mb` | 6.1 |
| ESP32-C3 | `esp32c3_4mb`, `esp32c3_8mb` | 5.5.5 |
| ESP32-C5 | `esp32c5_4mb`, `esp32c5_8mb` | 5.5.5 |
| ESP32-C6 | `esp32c6_4mb`, `esp32c6_8mb` | 6.0.3 |
| ESP32-C61 | `esp32c61_4mb`, `esp32c61_8mb` | 5.5.5 |
| ESP32-H2 | `esp32h2_4mb` | 5.5.5 |
| ESP32-H21 | `esp32h21_4mb` | 6.1 |
| ESP32-H4 | `esp32h4_4mb` | 6.1 |

## Build and flash

From the repository root, after activating the matching ESP-IDF environment:

```sh
python3 tools/idf_build.py esp32c5_4mb build
python3 tools/idf_build.py esp32c5_4mb -p PORT flash monitor
```

Replace the profile and `PORT` for your board. The helper accepts normal `idf.py` actions and options after the profile. For example:

```sh
python3 tools/idf_build.py esp32c5_4mb menuconfig
python3 tools/idf_build.py esp32c5_4mb -p PORT erase-flash flash
```

Generated configuration and artifacts are kept under `build/<profile>/`. Do not flash an 8 MB image to a 4 MB module. C5 profiles use the chip's native USB Serial/JTAG port for the interactive console, not a board's USB-to-UART bridge. `PORT` is commonly `/dev/ttyACM0` on Linux or a `/dev/cu.*` device on macOS. Other profiles use the ESP-IDF primary console selected for the chip, usually UART0. See [serial shell](SERIAL.md) for console details.

After flashing, follow [first setup](SETUP.md). The [wiring guide](WIRING.md) gives the safe electrical bring-up order.

## Release assets and updates

Publishing a GitHub Release from a tag such as `v1.2.3` builds every profile above, attaches the images, `SHA256SUMS`, and `bc250_recover.py`, and adds a direct image download table to the release description. Release tags must be `vMAJOR.MINOR.PATCH`, optionally with a suffix such as `-rc.1`, and at most 31 characters.

To place the table within your own release notes, put `<!-- bc250-image-table -->` at that position in the description before publishing. Otherwise, the table is placed at the top. Rerunning the release workflow updates the same table.

| Asset | Use |
|---|---|
| `bc250_ctrl-<profile>.bin` | Application image; upload through the web firmware update page on 8 MB profiles. |
| `bc250_ctrl-<profile>-full.bin` | Bootloader, partition table, and application for serial/USB flashing at address `0x0`. It replaces the flash layout and erases saved settings in padded regions. It is not an OTA image. |
| `bc250_recover.py` | Clear settings and Zigbee pairing while keeping installed firmware. See [recovery](RECOVERY.md). |
| `SHA256SUMS` | Verify downloaded assets before flashing. |

Web updates are available only on 8 MB builds. ESP-IDF writes the inactive application slot and validates the image before switching slots; the newly booted image marks itself valid after 30 seconds. For 4 MB builds, update over serial/USB. Zigbee OTA is not implemented.

The firmware version comes from Git tags. A build at `v1.2.3` reports that tag; later commits report a `git describe` value such as `v1.2.3-4-gabc1234`, and local modifications add `-dirty`. Without a matching tag in a Git checkout, the commit hash is used; a source archive without Git reports `0.0.0+unknown`. Reconfigure an existing build after changing tags so ESP-IDF refreshes the embedded version. The version appears in the boot log, serial `status`, web overview, and HTTP status response.

## Firmware architecture

`src/app_main.c` initializes configuration, the serial task, and event handling. Buttons, BLE arrivals, and Zigbee commands feed the power service, which serializes actions through `src/core/power_logic.c` and alone applies output GPIO levels. The status LED reflects state-machine state. `src/i2c_service.c` owns the shared I²C bus; PSU monitoring is an optional read-only client. The web app is embedded from `src/web_ui.html`.

Run host checks and use the hardware acceptance checklist in [Verification](TESTING.md).

## Sources

The HP Common Slot implementation draws on [DPS-1200-I2C](https://github.com/ButtSimpleIdeas/DPS-1200-I2C), [DPS-1200FB research](https://github.com/raplin/DPS-1200FB), [Common Slot pinout and EEPROM research](https://github.com/slundell/dps_charger), and the [IPMI FRU specification](https://www.intel.com/content/dam/www/public/us/en/documents/specification-updates/ipmi-platform-mgt-fru-info-storage-def-v1-0-rev-1-3-spec-update.pdf).
