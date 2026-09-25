# BC-250 ESP32-C5/C6 Power Controller

ESP-IDF firmware for controlling a BC-250 locally and safely through optically isolated low-voltage signals. You can control it from the built-in web interface, Zigbee, BLE presence detection, or physical buttons. No cloud service is required.

> **Safety:** this project switches only low-voltage ATX and motherboard control signals. Never connect the ESP32 or an optocoupler circuit to mains voltage. Power the controller from ATX 5VSB so it remains alive while the BC-250 is off. Do not connect USB 5 V and 5VSB at the same time unless the dev board has verified back-feed protection or an external power-OR circuit.

## Features

### Safe power control

- Turns the BC-250 on and off through optically isolated `PS_ON#` and motherboard power-button signals.
- Reads the board's power LED through an optocoupler, so reported power state comes from the hardware rather than the last command sent.
- Supports four startup methods: PS_ON only, power button only, PS_ON followed by the power button, or both at the same time.
- Supports normal shutdown and an explicit five-second force-off action. A failed startup or shutdown enters a fault state instead of repeatedly toggling the outputs.
- Optionally monitors PSUs using the HP Common Slot protocol, including DPS-1200/750 models, through their 3.3 V I²C PIC interface. The web status and REST API show input/output voltage and current, internal temperature, and the fan reading.

### Local controls and automation

- Configures up to eight physical buttons. Each button can perform a different action for short, double, and long presses.
- Watches for up to sixteen BLE devices using an address, device name, service UUID, or manufacturer data.
- Powers on when a configured BLE device appears. Losing the BLE signal never shuts the board down.
- Shows off, starting, on, stopping, fault, and setup states through an optional status LED.

### Wi-Fi and web interface

- Provides a browser-based setup and control interface stored entirely in the firmware.
- Offers a local REST API and live power-state updates for integrations.
- Supports Wi-Fi-only, Zigbee-only, and combined Wi-Fi/Zigbee operation. BLE scanning remains available in every profile.
- Exposes the web interface during Wi-Fi operation and through the temporary setup access point. Zigbee-only mode does not keep Wi-Fi running after setup.

### Zigbee

- Operates as a Zigbee router with standard Basic, Identify, Groups, Scenes, and On/Off clusters.
- Accepts on, off, and toggle commands from Zigbee controllers such as Home Assistant ZHA or Zigbee2MQTT.
- Reports the power state measured from the BC-250 instead of assuming a command succeeded.

### Recovery and updates

- Tests new configuration for 30 seconds before making it permanent. If it cannot become healthy after two boot attempts, the previous configuration is restored.
- Opens setup mode after three consecutive quick resets, so recovery does not require a dedicated button.
- Supports browser-based firmware updates on 8 MB targets, with two application slots and bootloader rollback. The 4 MB targets are updated over serial or USB.

All external GPIOs are disabled by default. The firmware does not restore a previous output state during startup. Add external bias resistors to keep the output optocouplers off while the ESP32 is resetting or starting; see the wiring guide before connecting hardware.

## Build targets

Install and activate a native [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/get-started/index.html) environment. The component manifest accepts ESP-IDF `>=5.5.2,<6.1.0`. CI tests C5 with 5.5.4 and C6 with 6.0.1; other accepted target/version combinations are not CI-verified.

| Profile | Target | Flash | OTA |
|---|---|---:|---|
| `esp32c5_4mb` | ESP32-C5 | 4 MB | No |
| `esp32c5_8mb` | ESP32-C5 | 8 MB | Yes |
| `esp32c6_4mb` | ESP32-C6 | 4 MB | No |
| `esp32c6_8mb` | ESP32-C6 | 8 MB | Yes |

Build and upload:

```sh
python3 tools/idf_build.py esp32c5_4mb build
python3 tools/idf_build.py esp32c5_4mb -p /dev/ttyUSB0 flash monitor
```

Change the profile for C6 or 8 MB hardware and use the serial port for your system. The helper invokes `idf.py` directly and keeps each profile's generated configuration and artifacts under `build/<profile>/`. Verify the actual flash capacity before using an 8 MB image.

You can pass any normal `idf.py` action or option after the profile. For example, open configuration with `python3 tools/idf_build.py esp32c5_4mb menuconfig`, or erase and flash with `python3 tools/idf_build.py esp32c5_4mb -p /dev/ttyUSB0 erase-flash flash`.

## First setup

1. Flash the correct target while all power-control GPIO roles are still disabled.
2. Open the serial monitor. On first initialization, note the generated 12-character provisioning password. It is also the initial `admin` password and is printed once.
3. Join `BC250-Ctrl-XXXX` using that password and open `http://192.168.4.1/`.
4. Select a radio profile, assign pins from the board’s schematic, set active polarity, and configure the power timings. Configured mode requires power-sense and power-button GPIOs; every start strategy except button-only also requires PS_ON.
5. Add buttons and BLE controllers as needed. Set an admin password of at least eight characters.
   For a compatible HP Common Slot PSU, enable PSU I²C and assign SDA/SCL pins after checking [the wiring guide](docs/WIRING.md). The PIC address defaults to decimal 95 (`0x5F`).
6. For Wi-Fi or hybrid mode, configure a WPA2-or-stronger network; open, WEP, and WPA-only networks are not supported.
7. Save. The new configuration is staged and applied after reboot. After 30 seconds it becomes active if validation succeeds and any required Wi-Fi station is connected. This check does not validate Zigbee, BLE, power sense, or external hardware.

All UI and API endpoints bypass HTTP Basic authentication while the setup AP is active and rely on its WPA2 password. Anyone joined to the AP can issue control and configuration commands and, on 8 MB builds, upload firmware. A configured device's recovery AP expires after 15 minutes; first-boot provisioning stays open until configured. In normal Wi-Fi operation, sign in as user `admin` with the configured password. If configuration becomes inaccessible, reset the ESP32 three times consecutively without allowing either of the first two boots to run for 30 seconds.
The web interface can reset just the Zigbee network, or perform a full factory reset from the configuration AP; the latter also erases controller settings and returns to first-boot provisioning.

The local web service uses HTTP, so Basic-auth credentials are visible to anyone who can inspect traffic on that network. Keep it on a trusted LAN and never port-forward it. CRC protects configuration integrity, not confidentiality: Wi-Fi/AP credentials are stored in NVS, and flash encryption is not enabled by default. Secure Boot is also not enabled; the default OTA build checks image integrity but does not require a cryptographic signature.

## GPIO guidance

The conservative allowlists are:

- ESP32-C5: GPIO 0, 1, 4, 5, 6, 8, 9, 10, 23, 24
- ESP32-C6: GPIO 0, 1, 2, 3, 6, 7, 10, 11, 18, 19, 20, 21, 22, 23

These are a firmware guardrail, not a replacement for the exact dev-board schematic. Check the [C5 DevKitC pin restrictions](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c5/esp32-c5-devkitc-1/user_guide.html) or [C6 GPIO restrictions](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32c6/api-reference/peripherals/gpio.html) against your module: some otherwise valid SoC pins are routed to flash, USB, LEDs, or different headers. Advanced override is available only for intentional, reviewed installations.

See [docs/WIRING.md](docs/WIRING.md) before connecting the BC-250 and [docs/API.md](docs/API.md) for local integrations.

Address-based BLE matching is unreliable for devices that rotate private addresses; prefer stable service or manufacturer advertisement data. The configured hostname is currently stored but is not applied as a DHCP hostname or advertised through mDNS.

## Architecture

`app_main` initializes configuration and an event queue used by buttons, BLE arrivals, and Zigbee attribute commands. HTTP handlers call the power queue or Zigbee service directly. `power_service` serializes power actions into the pure `core/power_logic` state machine and alone applies output GPIO levels. The status LED consumes state-machine state, while the Zigbee On/Off attribute mirrors the sensed power input rather than the last command.

The embedded web application is compiled into the firmware. There is no cloud dependency, CDN, MQTT broker, or companion app. PSU I²C is optional and read only; switching an HP PSU's output requires a separate hardware connection to its enable signal.

## Tests

Run host tests:

```sh
cmake -S test/native -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Build every target:

```sh
python3 tools/idf_build.py esp32c5_4mb build
python3 tools/idf_build.py esp32c5_8mb build
python3 tools/idf_build.py esp32c6_4mb build
python3 tools/idf_build.py esp32c6_8mb build
```

Hardware acceptance still requires an optocoupler loopback fixture and real ZHA/Zigbee2MQTT networks. The detailed checklist is in [docs/TESTING.md](docs/TESTING.md).

## Credits

The HP Common Slot protocol implementation draws on the [DPS-1200-I2C project](https://github.com/ButtSimpleIdeas/DPS-1200-I2C) by Butt Simple Ideas, LLC. Its Arduino examples and documentation provided the register addresses, measurement scaling, and connection guidance. [Richard Aplin's DPS-1200FB work](https://github.com/raplin/DPS-1200FB) helped verify the reply checksum handling.
