# Serial command interface

The controller accepts commands on its primary ESP-IDF console in every radio profile, including first-boot setup and recovery mode. The C5 build profiles use the chip's native USB Serial/JTAG port for both input and output. Connect to that port on the board, not its USB-to-UART bridge port if it has two USB connectors. The C6 profiles still use UART0 at 115200 baud. A secondary console may show logs and replies but does not accept commands. See [Espressif's C5 USB console guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/api-guides/usb-serial-jtag-console.html) for port identification and wiring.

Send one command per line, ending with Enter. Command names are case sensitive. Each reply is one line starting with `BC250 `, followed by JSON. `ok` is `true` or `false`; successful replies carry a `data` field. Boot messages and service logs can appear between replies, so scripts should select lines beginning with `BC250 `. A power command acknowledges queueing; use `status` to see the sensed result.

| Command | Result |
|---|---|
| `help` | List commands |
| `status` | Power, Wi-Fi, Zigbee, BLE presence, and PSU telemetry |
| `config get` | Current configuration with Wi-Fi password and password hash redacted |
| `config set <JSON patch>` | Validate and stage a partial configuration, then reboot |
| `power on`, `power off`, `power toggle`, `power force_off` | Queue a power action |
| `ble scan` | Start a 15-second discovery scan |
| `ble results` | Return discovered addresses, names, and RSSI |
| `i2c scan <SDA> <SCL>` | Scan a validated I²C pin pair; return decimal addresses |
| `zigbee commission`, `zigbee reset` | Start joining or clear only Zigbee network state |
| `wifi ap` | Open the configuration access point |
| `admin reset` | Generate, save, and display a new admin password once |
| `factory reset ERASE ALL` | Erase all NVS data, including settings and Zigbee state, then reboot |
| `reboot` | Restart the controller |

For example:

```text
status
power on
config get
config set {"hostname":"bc250-lab"}
i2c scan 4 5
admin reset
```

`config set` accepts the same partial JSON fields and safety checks as `PUT /api/v1/config`. The complete command is limited to 12,288 bytes. Changes are staged and follow the existing 30-second health check and rollback process. `admin reset` takes effect immediately and updates both the active and pending configuration, so a pending configuration rollback does not restore the old password. It leaves the setup AP password unchanged.

Treat access to the physical serial port as administrator access. There is no serial login. The generated admin password is shown once in the serial reply; keep terminal captures private. Configuration commands containing Wi-Fi or admin passwords may be visible to the terminal or its history.
