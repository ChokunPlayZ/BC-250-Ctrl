# HP Common Slot PSU monitoring

The optional PSU client reads PIC telemetry and a separate identification EEPROM. It never commands the PSU output or writes EEPROM contents. Switching PSU output requires a separate electrical enable connection. The [configuration reference](CONFIGURATION.md#radio-and-optional-psu-monitoring) lists supported addresses and polling settings; the [HTTP API](API.md) describes decoded and cached raw data.

## Electrical connection

Connect the PSU's SDA and SCL to the configured ESP32 I²C pins and connect their signal grounds. **Measure the idle PSU-side bus voltage before connecting it.** ESP32 GPIOs require 3.3 V logic; [DPS-1200FB notes](https://github.com/raplin/DPS-1200FB#connecting-i2c) describe weak 5 V pull-ups on that model. Use a suitable bidirectional level shifter if the PSU side is 5 V. Provide external pull-ups on the ESP32 side, typically 2.2–4.7 kΩ from each line to 3.3 V. Firmware's weak internal pull-ups are only a fallback. These connections are not optically isolated.

The [measured DPS-1200FB pinout](https://github.com/slundell/dps_charger#connection) lists connector contact 30 as signal ground, 31 as SCL, and 32 as SDA. With ESP32 SDA GPIO 1 and SCL GPIO 2, connect contact 32 to GPIO 1, 31 to GPIO 2, and 30 to ESP32 ground. Check contact numbering on both sides of the connector and verify the exact PSU model electrically; do not assume a DPS-460EB is identical. Leave monitoring disabled until voltage, grounding, and pinout are verified.

## Addresses and readings

The PIC typically responds at `0x58`–`0x5F`; `0x5F` is common when address pins A0–A2 are high. A FRU EEPROM may respond at `0x50`–`0x57`, but the two addresses do not have to differ by eight. An observed DPS-460EB scan found `0x57` and `0x58`; an ACK alone does not identify either device. Some PSUs answer only while running. An unavailable reading while the PSU is off can be expected.

PIC transactions are read-only and require a valid reply checksum before telemetry is marked available. The firmware separately probes the EEPROM range and accepts only checksum-verified FRU identity. It retries identification about once a minute, independently of PIC telemetry. Unsupported or corrupt FRU fields produce an identity error without hiding valid electrical readings. Available identity fields can include manufacturer, product name, part number, revision, serial/CT number, board part number, and rated capacity.

The controller reports input/output voltage and current, internal temperature in Celsius, and a **raw fan value**, which is not calibrated RPM. The [reference PIC sketch](https://github.com/ButtSimpleIdeas/DPS-1200-I2C/blob/master/dps1200_read_volts_fan/dps1200_read_volts_fan.ino) reports temperature in Fahrenheit; this firmware converts it. DPS-460EB PIC protocol and EEPROM contents still need hardware verification.

The implementation follows the proprietary PIC format documented by [DPS-1200/750 research](https://github.com/ButtSimpleIdeas/DPS-1200-I2C/blob/master/Readme.md). A Common Slot connector described elsewhere as PMBus does not prove this PSU implements that protocol. A protocol mismatch may prevent readings after an address ACK; it does not explain SCL being held low before a scan starts.

## Scan and fault diagnosis

Run `i2c scan <SDA> <SCL>` in the [serial shell](SERIAL.md#commands) or **Scan I²C bus** in the web UI. A complete scan probes 112 addresses, `0x08`–`0x77`. Results from an incomplete scan are inconclusive. If SCL is low before scanning, the controller stops with 0 addresses probed. A held-low line during probing or the three-second scan limit stops early but preserves previously found devices. An isolated timeout can still allow later addresses to be scanned if both lines return high. An old error mentioning `0x08` only identifies the first probe, not the PSU address.

After enabling monitoring and rebooting, inspect serial `status` or web PSU status for the last sampling error. The controller keeps each register command/reply pair under the bus lock, permits up to 20 ms of clock stretching subject to the driver, and retries the pair once after a timeout. Failed samples are retried at the configured poll interval.

| Observation | Check |
|---|---|
| `SDA ...=low` or `SCL ...=low` | Measure both idle lines against ESP32 ground; check shared ground, swapped/shorted wires, pull-ups, level shifter, and PSU power. Disconnect the PSU to isolate the side holding the line low. |
| Both lines high but timeout | Check header pins, pull-up strength, wiring length/noise, and PSU compatibility. A static GPIO snapshot cannot establish timing. |
| `ESP_ERR_INVALID_STATE` during register write | On IDF 5.5.4 this may mean the transaction ended before completion, including a command-byte NACK. Address ACK alone does not prove protocol compatibility; use a logic analyzer to distinguish NACK from timing failure. |
| `ESP_ERR_INVALID_RESPONSE` | Check PIC address and PSU power; the transfer was not acknowledged. |
| `reply checksum failed` | Check signal quality and whether the PSU implements the PIC protocol. |
| `I2C bus busy` | Another scan/client held the bus; retry. |

On IDF 5.5.4/C5, a timeout can also print `i2c.common: GPIO 1 is not usable, maybe conflict with others` for SDA/SCL during bus recovery. The [driver bus-clear path](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_i2c/i2c_master.c) can reconfigure already reserved pins and trigger the [pin-configuration warning](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_i2c/i2c_common.c). Warnings at initial bus creation, before a failed transfer, should instead be investigated as an actual conflict.
