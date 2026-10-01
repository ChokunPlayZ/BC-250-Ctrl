# BC-250 ESP32 Power Controller

ESP-IDF firmware for controlling a BC-250 locally through optically isolated low-voltage signals. Wi-Fi targets offer a built-in web interface; ESP32-H targets use the serial console. BLE presence detection and physical buttons work on every supported target. Native Zigbee is available on C5, C6, and H targets. No cloud service is required.

> **Safety:** this project switches only low-voltage ATX and motherboard control signals. Never connect the ESP32 or an optocoupler circuit to mains voltage. Power the controller from ATX 5VSB so it remains alive while the BC-250 is off. Do not connect USB 5 V and 5VSB at the same time unless the dev board has verified back-feed protection or an external power-OR circuit.

## Features

### Safe power control

- Turns the BC-250 on and off through optically isolated `PS_ON#` and motherboard power-button signals.
- Reads the board's power LED through an optocoupler, so reported power state comes from the hardware rather than the last command sent.
- Supports five power methods: sensed PS_ON only, power button only, PS_ON followed by the power button, both at the same time, and PS_ON latch without a power-sense input.
- Optionally keeps the ESP32's PS_ON contact closed while board power is detected, including external startup and shutdown until the board turns off. Enable **Keep PS_ON closed while board power is detected** in Power wiring & timing; it defaults to off.
- Supports normal shutdown and an explicit five-second force-off action for sensed methods. PS_ON latch opens PS_ON immediately on an off command.
- Optionally monitors PSUs using the HP Common Slot protocol, including DPS-1200/750 models, through their I²C PIC interface. The web status and REST API show input/output voltage and current, internal temperature, the fan reading, and any validated identification available from an EEPROM on the same bus. A web and serial data viewer can show cached raw PIC readings and EEPROM bytes. Check the PSU-side bus voltage before connecting it to 3.3 V ESP32 GPIOs.

### Local controls and automation

- Configures up to eight physical buttons. Each button can perform a different action for short, double, and long presses.
- Watches for up to sixteen BLE devices using an address, device name, service UUID, or manufacturer data.
- Powers on when a configured BLE device appears. Losing the BLE signal never shuts the board down.
- Shows off, starting, on, stopping, fault, and setup states through an optional status LED.

### Wi-Fi and web interface

- Provides a mobile-friendly browser interface stored entirely in the firmware, with large touch controls, collapsible settings, inline validation, and a fixed Save and reboot bar.
- Offers a local REST API and live power-state updates for integrations.
- Opens an interactive serial shell with readable status, command history, Tab completion, and simple settings commands such as `set hostname bc250-lab` and `save`.
- Supports Wi-Fi mode on ESP32, S3, C3, C5, C6, and C61; supports Zigbee mode on C5, C6, H2, H21, and H4. BLE scanning remains available in every profile.
- Exposes the web interface during Wi-Fi operation and through the temporary setup access point on Wi-Fi targets. H targets use serial setup and do not provide an access point or web interface.

### Zigbee

- Operates as a Zigbee router with standard Basic, Identify, Groups, Scenes, and On/Off clusters.
- Accepts on, off, and toggle commands from Zigbee controllers such as Home Assistant ZHA or Zigbee2MQTT.
- Reports the power state measured from the BC-250 instead of assuming a command succeeded.
- Includes a [detailed Zigbee setup guide](docs/ZIGBEE.md) with pairing, recovery, endpoint/attribute definitions, and ready-to-copy [Zigbee2MQTT converters](docs/zigbee/zigbee2mqtt/bc250.mjs) and [ZHA PSU sensor quirks](docs/zigbee/zha/bc250.py).
- When HP PSU I²C monitoring is enabled, reports input AC voltage/current, output DC voltage/current, and the raw fan reading to the Zigbee coordinator. Periodic reports are spaced at least 10 seconds apart; a failed PSU read or recovery is reported immediately. Fan data is a raw value, not calibrated RPM. See [Zigbee telemetry](docs/API.md#zigbee-psu-telemetry) for attributes and units.

### Recovery and updates

- Tests new configuration for 30 seconds before making it permanent. If it cannot become healthy after two boot attempts, the previous configuration is restored.
- Opens setup mode after three consecutive quick resets, so recovery does not require a dedicated button.
- Provides standalone C5 recovery images that erase saved settings without loading GPIO assignments; see [recovery flashing](docs/RECOVERY.md).
- Supports browser-based firmware updates on 8 MB targets, with two application slots and bootloader rollback. The 4 MB targets are updated over serial or USB.

All external GPIOs are disabled by default. The firmware does not restore a previous output state during startup. With PS_ON hold enabled, the power service asserts PS_ON if it detects that the board is already on. Add external bias resistors to keep the output optocouplers off while the ESP32 is resetting or starting; see the wiring guide before connecting hardware.

## Build targets

Install and activate [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html). The component manifest accepts `>=5.5.2,<6.2.0`. Use IDF 6.1 for ESP32, S3, H21, and H4; H21/H4 BLE and native Zigbee are unavailable in 5.5.4. CI also builds C3, C5, C61, and H2 with 5.5.4 and C6 with 6.0.1. [Espressif's BLE target table](https://docs.espressif.com/projects/esp-idf/en/latest/esp32h2/api-guides/ble/overview.html) lists the supported BLE chips. ESP32-S2 and ESP32-P4 have no onboard BLE and are excluded.

| Profile | Target | Flash | OTA |
|---|---|---:|---|
| `esp32_4mb`, `esp32_8mb` | Classic ESP32 | 4 / 8 MB | 8 MB only |
| `esp32s3_4mb`, `esp32s3_8mb` | ESP32-S3 | 4 / 8 MB | 8 MB only |
| `esp32c3_4mb`, `esp32c3_8mb` | ESP32-C3 | 4 / 8 MB | 8 MB only |
| `esp32c5_4mb` | ESP32-C5 | 4 MB | No |
| `esp32c5_8mb` | ESP32-C5 | 8 MB | Yes |
| `esp32c6_4mb` | ESP32-C6 | 4 MB | No |
| `esp32c6_8mb` | ESP32-C6 | 8 MB | Yes |
| `esp32c61_4mb`, `esp32c61_8mb` | ESP32-C61 | 4 / 8 MB | 8 MB only |
| `esp32h2_4mb` | ESP32-H2 | 4 MB | No |
| `esp32h21_4mb` | ESP32-H21 | 4 MB | No |
| `esp32h4_4mb` | ESP32-H4 | 4 MB | No |

Build and upload:

```sh
python3 tools/idf_build.py esp32c5_4mb build
python3 tools/idf_build.py esp32c5_4mb -p PORT flash monitor
```

Choose the profile for your chip and actual flash capacity. For C5 profiles, connect the ESP32-C5's native USB Serial/JTAG port (not a USB-to-UART bridge port); `PORT` is typically `/dev/ttyACM0` on Linux or a `/dev/cu.*` device on macOS. Other profiles use the ESP-IDF default primary console for that chip, usually UART0. The helper keeps generated configuration and artifacts under `build/<profile>/`. Do not use an 8 MB image on a 4 MB module.

ESP32-H chips have no Wi-Fi. Configure them through the serial shell: assign the required power GPIOs, set `configured on`, then `save`. Zigbee starts after reboot and joins automatically when factory new; `zigbee commission` retries joining. See [serial setup](docs/SERIAL.md). An H chip needs a Zigbee coordinator for remote control, or configured physical buttons for local control.

You can pass any normal `idf.py` action or option after the profile. For example, open configuration with `python3 tools/idf_build.py esp32c5_4mb menuconfig`, or erase and flash with `python3 tools/idf_build.py esp32c5_4mb -p PORT erase-flash flash`.

## First setup

These steps apply to Wi-Fi targets. For H targets, follow [serial setup](docs/SERIAL.md#first-setup-on-esp32-h).

1. Flash the correct target while all power-control GPIO roles are still disabled.
2. Open the serial monitor. On first initialization, note the generated 12-character `admin` password, which is printed once and is used after joining your normal Wi-Fi network.
3. Join the open `BC250-Ctrl-XXXX` network without a password and open `http://192.168.4.1/`.
4. Select a radio profile, assign pins from the board’s schematic, set active polarity, and configure the power timings. Sensed methods require power-sense and motherboard switch output GPIOs; every method except button-only requires PS_ON. **PS_ON latch (no power sense)** needs only PS_ON. For a physical on/off button in latch mode, configure a Custom button with its short press set to **Toggle**. Zigbee On/Off works without a button.
5. Add buttons and BLE controllers as needed. For the optional local button and status LED, follow the [connection diagrams and matching settings](docs/WIRING.md#local-buttons-and-status-led). Set an admin password of at least eight characters.
   For a compatible HP Common Slot PSU, assign SDA/SCL pins after checking [the wiring guide](docs/WIRING.md). The UI can scan the bus and select a detected PIC address before saving; the PIC address defaults to decimal 95 (`0x5F`). Enable PSU I²C to monitor it after reboot.
6. For Wi-Fi mode, configure a WPA2-or-stronger network; open, WEP, and WPA-only networks are not supported.
7. The save bar appears only after an edit. Choose **Save & reboot** to apply changes, or **Discard** to restore the loaded settings. The new configuration is staged and applied after reboot. After 30 seconds it becomes active if validation succeeds and any required Wi-Fi station is connected. This check does not validate Zigbee, BLE, power sense, or external hardware.

Setup and recovery APs are open networks, including on devices with settings saved by older firmware. All UI and API endpoints bypass HTTP Basic authentication while the setup AP is active. Anyone in range can join the AP, issue control and configuration commands and, on 8 MB builds, upload firmware. Every setup and recovery AP closes after five minutes with no connected Wi-Fi clients, including first boot and the serial `wifi ap` command. It stays open while any client is connected, and a fresh five-minute grace period starts when the last client disconnects. The serial `wifi ap` command opens the same setup AP in every radio profile and refreshes the grace period when it is already open. You can also turn it off from the web interface. Opening an AP pauses the Zigbee router before Wi-Fi starts accepting clients. Closing the AP restores the configured Wi-Fi station in Wi-Fi mode; Zigbee-only devices stop Wi-Fi and resume Zigbee using their saved network data. Unconfigured devices stop Wi-Fi. In Zigbee-only mode, the status LED pulses for one second on and one second off throughout the AP session, then returns to its normal power-state pattern. In normal Wi-Fi operation, sign in as user `admin` with the configured password. If configuration becomes inaccessible, reset the ESP32 three times consecutively without allowing either of the first two boots to run for 30 seconds.
With Zigbee mode saved, use **Start pairing** on the web overview, then **Turn off Wi-Fi & pair**. This closes setup Wi-Fi before starting Zigbee; follow pairing on your hub. Save or discard pending edits first. The serial `zigbee commission` and `zigbee reset` commands remain available while the AP is closed to join or reset just the Zigbee network. The web interface can perform a full factory reset from the configuration AP, erasing controller settings and returning to first-boot provisioning. The [serial command interface](docs/SERIAL.md) is also available without Wi-Fi and can generate a new admin password without erasing settings.

For coordinator setup, pairing steps, and device definitions, follow [docs/ZIGBEE.md](docs/ZIGBEE.md).

The local web service uses HTTP, so Basic-auth credentials are visible to anyone who can inspect traffic on that network. Keep it on a trusted LAN and never port-forward it. CRC protects configuration integrity, not confidentiality: Wi-Fi credentials are stored in NVS, and flash encryption is not enabled by default. Secure Boot is also not enabled; the default OTA build checks image integrity but does not require a cryptographic signature.

Wi-Fi + Zigbee mode has been removed. Older saved combined-mode configurations load as Zigbee-only and retain their Wi-Fi credentials and other settings. [Espressif lists Wi-Fi AP operation alongside a Zigbee router as unsupported](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32c5/api-guides/coexist.html), so the setup AP and Zigbee router take turns using the radio.

## GPIO guidance

The conservative GPIO guidance lists for the original boards are:

- ESP32-C5: GPIO 0, 1, 4, 5, 6, 8, 9, 10, 23, 24
- ESP32-C6: GPIO 0, 1, 2, 3, 6, 7, 10, 11, 18, 19, 20, 21, 22, 23

**ESP32-C5 GPIO 12 and 14 are blocked:** saved assignments have been reported to prevent booting on the NodeMCU ESP32-C5 Mini. All C5 profiles reject these pins for outputs, inputs, buttons, status LEDs, and I²C scans, even with the legacy override enabled. Existing settings using these pins enter recovery mode before GPIO services start. A unit that cannot run its current firmware can be cleared with the [standalone recovery image](docs/RECOVERY.md). C6 profiles do not have these exclusions.

For the added targets, no generic pin list is recommended because exposed pins vary by module. The firmware rejects GPIOs that the selected chip does not support for the assigned input or output role. Classic ESP32 GPIO 34–39 are also excluded because this firmware configures an internal pull on every input, and those pins have none. Check the exact board schematic for flash, USB, strapping, and header use. Other chip-valid pins produce advisory warnings and can be saved without an override, except for the blocked C5 pins above. Invalid GPIO values, duplicate role assignments, and missing required pins prevent saving. The web interface validates basic inputs as you edit; target-specific GPIO errors are returned on save.

See [docs/WIRING.md](docs/WIRING.md) before connecting the BC-250 and [docs/API.md](docs/API.md) for local integrations.

Address-based BLE matching is unreliable for devices that rotate private addresses; prefer stable service or manufacturer advertisement data. The configured hostname is currently stored but is not applied as a DHCP hostname or advertised through mDNS.

## Architecture

`app_main` initializes configuration, the serial command task, and an event queue used by buttons, BLE arrivals, and Zigbee attribute commands. HTTP and serial handlers call the power queue or Zigbee service directly. `power_service` serializes power actions into the pure `core/power_logic` state machine and alone applies output GPIO levels. The status LED consumes state-machine state. Zigbee On/Off follows power sense in sensed methods and the controller's PS_ON state in latch mode. The shared `i2c_service` owns the I²C bus, coordinates device transactions and scans, and can serve additional I²C clients alongside PSU monitoring.

The embedded web application is compiled into the firmware. There is no cloud dependency, CDN, MQTT broker, or companion app. PSU I²C is optional and read only; switching an HP PSU's output requires a separate hardware connection to its enable signal.

## Tests

Run host tests:

```sh
cmake -S test/native -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Build the C3/C5/C61/H2 profiles with ESP-IDF 5.5.4 (C6 also builds with this version locally):

```sh
python3 tools/idf_build.py esp32c3_4mb build
python3 tools/idf_build.py esp32c5_4mb build
python3 tools/idf_build.py esp32c5_8mb build
python3 tools/idf_build.py esp32c6_4mb build
python3 tools/idf_build.py esp32c6_8mb build
python3 tools/idf_build.py esp32c61_4mb build
python3 tools/idf_build.py esp32h2_4mb build
```

Switch to ESP-IDF 6.1 for classic ESP32, S3, H21, and H4:

```sh
python3 tools/idf_build.py esp32_4mb build
python3 tools/idf_build.py esp32s3_4mb build
python3 tools/idf_build.py esp32h21_4mb build
python3 tools/idf_build.py esp32h4_4mb build
```

Hardware acceptance still requires an optocoupler loopback fixture and, for Zigbee targets, a real coordinator. The detailed checklist is in [docs/TESTING.md](docs/TESTING.md).

## Credits

The HP Common Slot protocol implementation draws on the [DPS-1200-I2C project](https://github.com/ButtSimpleIdeas/DPS-1200-I2C) by Butt Simple Ideas, LLC. Its Arduino examples and documentation provided the register addresses, measurement scaling, and connection guidance. [Richard Aplin's DPS-1200FB work](https://github.com/raplin/DPS-1200FB) helped verify the reply checksum handling. [slundell's DPS charger research](https://github.com/slundell/dps_charger) documented the separate identification EEPROM and Common Slot pinout; EEPROM fields are decoded using the [IPMI FRU Information Storage Definition](https://www.intel.com/content/dam/www/public/us/en/documents/specification-updates/ipmi-platform-mgt-fru-info-storage-def-v1-0-rev-1-3-spec-update.pdf).
