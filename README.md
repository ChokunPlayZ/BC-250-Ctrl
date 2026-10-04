# BC-250 ESP32 Power Controller

ESP-IDF firmware for local control of a BC-250 through optically isolated, low-voltage signals. It supports physical buttons and BLE presence on every target, a web interface on Wi-Fi capable targets, and native Zigbee on C5, C6, and H targets. No cloud service is required.

> **Before connecting hardware:** The controller switches low-voltage ATX and motherboard signals only. Never connect an ESP32 or optocoupler circuit to mains voltage. Read the [wiring guide](docs/WIRING.md), including standby power, GPIO biasing, and USB back-feed precautions.

## Start here

1. Choose a chip and flash-size profile in [Build and releases](docs/BUILD.md#supported-profiles).
2. Build and flash it, or select the matching release image. Follow the [build commands](docs/BUILD.md#build-and-flash).
3. Wire and check the optocouplers using the [bring-up order](docs/WIRING.md#bring-up-order). All external GPIO roles start disabled.
4. Follow [first setup](docs/SETUP.md) for a Wi-Fi target or an ESP32-H target.
5. For Zigbee control, follow the [pairing guide](docs/ZIGBEE.md#pairing-and-network-recovery).

Already configured? Follow the [upgrade guide](docs/UPGRADE.md) to keep your settings. Flashing a full image at `0x0` wipes them.

## Capabilities

| Area | What the firmware provides |
|---|---|
| Power | Five startup methods, hardware power sensing where wired, normal shutdown, and force-off. A latch method works with PS_ON alone. |
| Local control | Up to eight buttons with short, double, and long press actions; an optional status LED. |
| BLE | Up to sixteen matchers that can request power-on when a device appears. BLE disappearance never powers the board off. |
| Wi-Fi | Embedded setup and control UI, HTTP API, and live status on ESP32, S3, C3, C5, C6, and C61. |
| Zigbee | Router with standard On/Off control on C5, C6, H2, H21, and H4; optional PSU telemetry. |
| PSU monitoring | Optional read-only HP Common Slot PIC telemetry and FRU EEPROM identification over I²C. |
| Recovery and updates | Configuration rollback, triple-reset setup recovery, USB/serial settings recovery, and web OTA on 8 MB profiles. |

The power output starts inactive after reset. Sensed methods report the optocoupled board state; PS_ON latch mode reports the controller's output state because it has no independent sense input. See [power methods](docs/CONFIGURATION.md#power-methods) before choosing a circuit.

## Documentation

| Need | Guide |
|---|---|
| Choose a target, build, flash, or use release assets | [Build and releases](docs/BUILD.md) |
| Upgrade a configured board without erasing settings | [Upgrade guide](docs/UPGRADE.md) |
| Provision the controller and understand the setup AP | [First setup](docs/SETUP.md) |
| Choose power behavior, GPIOs, BLE, buttons, and optional PSU monitoring | [Configuration](docs/CONFIGURATION.md) |
| Connect the BC-250 and diagnose electrical problems | [Wiring and installation](docs/WIRING.md) |
| Connect and troubleshoot a compatible HP PSU I²C bus | [PSU monitoring](docs/PSU.md) |
| Use the interactive console | [Serial shell](docs/SERIAL.md) |
| Integrate over HTTP | [Local HTTP API](docs/API.md) |
| Pair with ZHA or Zigbee2MQTT and inspect Zigbee attributes | [Zigbee](docs/ZIGBEE.md) |
| Recover settings without replacing firmware | [USB/serial recovery](docs/RECOVERY.md) |
| Run automated and hardware checks | [Verification](docs/TESTING.md) |

The web app is embedded in the firmware. The controller does not require a CDN, MQTT broker, or companion app. Zigbee2MQTT, when used, runs on the coordinator host.
