# Configuration reference

Use the web interface on Wi-Fi capable targets or the [serial shell](SERIAL.md) on every target. Changes are staged and take effect after reboot; see [first setup](SETUP.md) for provisioning and rollback. This page describes what to choose. The [wiring guide](WIRING.md) describes the electrical connections.

## Power methods

`power_button.gpio` is the **output across the motherboard power-switch header**. It is separate from the local physical buttons configured under Custom buttons. Every sensed method requires this output for shutdown and a `power_sense.gpio` input for state reporting.

| Method (`timing.strategy`) | Startup action | Required GPIOs | Off behavior |
|---|---|---|---|
| `ps_on_only` | Close PS_ON until the board starts | PS_ON, power sense, motherboard switch | Pulse motherboard switch for normal shutdown |
| `button_only` | Pulse motherboard switch | Power sense, motherboard switch; PS_ON also required if hold is enabled | Pulse motherboard switch |
| `ps_on_then_button` | Close PS_ON, then pulse motherboard switch after a delay | PS_ON, power sense, motherboard switch | Pulse motherboard switch |
| `simultaneous` | Close PS_ON and pulse motherboard switch together | PS_ON, power sense, motherboard switch | Pulse motherboard switch |
| `ps_on_latched` | Close PS_ON and keep it closed | PS_ON only | Open PS_ON immediately; no graceful OS shutdown |

Sensed methods filter the power input before reporting On or Off. The default on filter is 500 ms and off filter is 2 seconds. They support an explicit five-second force-off action and a retry cooldown after startup failure. A queued command acknowledges the request, not its eventual outcome; check serial `status`, the web overview, or the API after the sequence finishes.

In latch mode, no power-sense input is read. The reported state follows the commanded PS_ON output. It cannot detect an externally started board, and reset or power loss opens PS_ON. For a local on/off button in this mode, assign **Toggle** to a Custom button gesture. Zigbee On/Off also works. `hold_ps_on` has no effect in latch mode.

### Keep PS_ON closed while power is sensed

`hold_ps_on` defaults to `false`. When enabled for a sensed method, the controller keeps its PS_ON optocoupler closed while board power is sensed, including an external start or a board already running when the service starts. It stays closed through normal shutdown and a shutdown timeout until the filtered sense input turns off. This requires a PS_ON GPIO even with `button_only` startup. A startup timeout with sense still off releases PS_ON.

The sense point must indicate that the **board is running**, rather than simply that PSU voltage exists; otherwise the hold can sustain itself. Keep the board's original hold path connected, because an ESP32 reset or recovery cannot maintain this contact. See [PS_ON wiring](WIRING.md#ps_on-output) for inactive-state biasing.

## GPIO selection

All roles default to `-1` (disabled). Select pins from the exact module and board schematic, checking flash, USB, strapping, and header use. The firmware rejects unsupported input/output pins, duplicate assignments, missing required roles, and invalid values. Pins outside a conservative recommendation are advisory unless explicitly blocked.

| Target | Conservative guidance |
|---|---|
| ESP32-C5 | GPIO 0, 1, 4, 5, 6, 8, 9, 10, 23, 24 |
| ESP32-C6 | GPIO 0, 1, 2, 3, 6, 7, 10, 11, 18, 19, 20, 21, 22, 23 |
| Other targets | Use the exact board schematic; no generic list is recommended. |

**C5 GPIO 12 and 14 are blocked for every role and I²C scan**, including with the legacy override, because saved assignments have prevented booting on the NodeMCU ESP32-C5 Mini. Existing settings using them enter recovery before GPIO services start. C6 has no corresponding hard exclusion. Classic ESP32 GPIO 34–39 are excluded because this firmware uses an internal pull on every input and those pins lack one. If an older C5 configuration cannot boot, use [USB/serial recovery](RECOVERY.md).

For output optocouplers, add an external inactive-state bias resistor. Firmware defaults to active-high output drive, but reset and bootloader behavior must be safe independently of firmware. See [Wiring and installation](WIRING.md).

## Buttons, LED, and BLE

Configure up to eight **Custom buttons**, each with a unique GPIO and actions for short, double, and long press. Available actions are `none`, `on`, `off`, `toggle`, `force_off`, `config_ap`, `zigbee_commission`, and `zigbee_reset`; actions unavailable on the selected chip are rejected. The common switch-to-ground circuit uses active-low input with the internal pull-up. The optional controller status LED has its own GPIO and reports power, setup AP, fault, and Zigbee joining states. Connection diagrams and the pattern table are in [Wiring and installation](WIRING.md#local-buttons-and-status-led).

Configure up to sixteen BLE matchers by address, exact or prefix name, service UUID, or manufacturer data. A newly present match may request power-on; losing BLE presence never requests shutdown. Devices with rotating private addresses are unreliable with address matching, so prefer stable advertisement data. The [serial shell](SERIAL.md#editing-settings) lists field names and examples. A 15-second discovery scan is available in the web UI, shell, and HTTP API.

## Radio and optional PSU monitoring

`radio_profile` is `wifi` or `zigbee`, when supported by the chip. There is no combined Wi-Fi plus Zigbee profile. C5/C6 Zigbee pauses while the setup AP is active; see [setup AP behavior](SETUP.md#setup-ap-behavior). The Zigbee channel and identity settings, pairing steps, and coordinator definitions are in [Zigbee](ZIGBEE.md).

HP Common Slot monitoring is optional and read-only. Set SDA/SCL GPIOs, a PIC address from `0x58`–`0x5F` (default `0x5F`), and a poll interval from 500 to 60000 ms. The web UI and shell can scan the bus before saving. The firmware probes EEPROM addresses `0x50`–`0x57` separately for validated FRU identity. The fan reading is raw, not calibrated RPM. PSU I²C does not switch the PSU output. Check [electrical voltage and pull-up requirements](PSU.md#electrical-connection) before enabling it.

The configured hostname is stored but is not currently applied as a DHCP hostname or advertised through mDNS.
