# BC-250 ESP32-C5/C6 Power Controller

Local-first ESP-IDF firmware for safely controlling a BC-250 board through optically isolated low-voltage signals. It supports Wi-Fi configuration and control, Zigbee router operation, BLE controller presence, configurable local buttons, an optional status LED, and transactional recovery.

> **Safety:** this project switches only low-voltage ATX and motherboard control signals. Never connect the ESP32 or an optocoupler circuit to mains voltage. Power the controller from ATX 5VSB so it remains alive while the BC-250 is off. Do not connect USB 5 V and 5VSB at the same time unless the dev board has verified back-feed protection or an external power-OR circuit.

## Implemented behavior

- The power state machine is the only module that drives `PS_ON#` or the motherboard button.
- The optocoupled power-LED input is authoritative for `off`, `starting`, `on`, `stopping`, and `fault` state.
- Start strategies: PS_ON only, button only, PS_ON then button, and simultaneous.
- Default sequence: PS_ON, 500 ms delay, 250 ms button pulse, sensed-on wait, 1 s handoff, 15 s timeout.
- Graceful shutdown uses a 250 ms button pulse; explicit force-off holds it for 5 s.
- Requests are serialized and idempotent, with a 60 s retry cooldown after a fault.
- Up to eight independently configured physical buttons with short, double, and long actions.
- Up to sixteen BLE matchers by static/public address, name, service UUID, or masked manufacturer data.
- Wi-Fi, Zigbee, and hybrid radio profiles; ESP-IDF coexistence is enabled.
- Captive setup AP, authenticated local REST API, embedded web UI, and live SSE status events.
- Zigbee router endpoint with Basic, Identify, Groups, Scenes, and On/Off server clusters.
- Versioned CRC-protected configuration with pending/active slots and automatic rollback.
- Triple-reset recovery opens configuration mode without relying on an assigned GPIO.
- 8 MB targets provide dual OTA slots and bootloader rollback; 4 MB targets use serial/USB updates.

All external GPIO roles default to disabled (`-1`). No output is restored from saved state during boot. The optocoupler output inputs also need external inactive-state bias resistors so they remain off during reset and before application startup; see the wiring guide.

## Build targets

Install and activate a native [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/get-started/index.html) environment. The component manifest accepts ESP-IDF 5.5.2 through 6.0.x; CI uses 5.5.4 for C5 and 6.0.1 for C6.

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
2. Open the serial monitor. On first boot, note the generated provisioning password. It is printed once.
3. Join `BC250-Ctrl-XXXX` using that password and open `http://192.168.4.1/`.
4. Select a radio profile, assign pins from the board’s schematic, set active polarity, and configure the power timings.
5. Add buttons and BLE controllers as needed. Set an admin password of at least eight characters.
6. Save. The new configuration is staged and applied after reboot. It becomes active after 30 seconds of healthy runtime; repeated failed boots roll it back.

The setup AP bypasses HTTP Basic authentication because WPA2 protects provisioning access. A configured device's recovery AP expires after 15 minutes; first-boot provisioning stays open until configured. In normal Wi-Fi operation, sign in as user `admin` with the configured password. If configuration becomes inaccessible, reset the ESP32 three times within 30 seconds to reopen the AP.
The web interface can reset just the Zigbee network, or perform a full factory reset from the configuration AP; the latter also erases controller settings and returns to first-boot provisioning.

The local web service uses HTTP, so Basic-auth credentials are visible to anyone who can inspect traffic on that network. Keep it on a trusted LAN, never port-forward it, and provision ESP-IDF Secure Boot before relying on signed firmware authenticity; the default OTA build checks image integrity but does not require a signature.

## GPIO guidance

The conservative allowlists are:

- ESP32-C5: GPIO 0, 1, 4, 5, 6, 8, 9, 10, 23, 24
- ESP32-C6: GPIO 0, 1, 2, 3, 6, 7, 10, 11, 18, 19, 20, 21, 22, 23

These are a firmware guardrail, not a replacement for the exact dev-board schematic. Check the [C5 DevKitC pin restrictions](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c5/esp32-c5-devkitc-1/user_guide.html) or [C6 GPIO restrictions](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32c6/api-reference/peripherals/gpio.html) against your module: some otherwise valid SoC pins are routed to flash, USB, LEDs, or different headers. Advanced override is available only for intentional, reviewed installations.

See [docs/WIRING.md](docs/WIRING.md) before connecting the BC-250 and [docs/API.md](docs/API.md) for local integrations.

## Architecture

`app_main` initializes configuration and a central event queue. Button, BLE, Wi-Fi, web, and Zigbee callbacks publish actions. `power_service` serializes them into the pure `core/power_logic` state machine and alone applies output GPIO levels. The status LED and Zigbee attribute consume reported state; they never infer success from the last command.

The embedded web application is compiled into the firmware. There is no cloud dependency, CDN, MQTT broker, or companion app.

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
