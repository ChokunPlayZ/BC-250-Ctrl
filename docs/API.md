# Local HTTP API

The API is available on Wi-Fi targets in Wi-Fi operation. It is also available on their open configuration AP, which requires neither a Wi-Fi password nor HTTP Basic authentication. Normal station-mode requests use HTTP Basic authentication with username `admin`. ESP32-H targets have no Wi-Fi or HTTP API; use the serial shell and Zigbee.

All configuration responses redact the Wi-Fi password and password hash. Sending an empty `wifi_password` preserves the current credential. For AP access, expiry, and network security, see [First setup](SETUP.md#setup-ap-behavior). Zigbee clusters, attribute scaling, and PSU report timing are documented in the [Zigbee device definition](ZIGBEE.md#device-definition-on-the-wire).

## Endpoint index

| Task | Endpoints |
|---|---|
| Observe and control | `GET /api/v1/status`, `GET /api/v1/events`, `POST /api/v1/power` |
| Configure and discover | `GET /api/v1/config`, `PUT /api/v1/config`, `GET/POST /api/v1/ble/scan`, `POST /api/v1/i2c/scan`, `GET /api/v1/psu/data` |
| Radio and recovery | `POST /api/v1/wifi/ap/close`, `POST /api/v1/zigbee`, `POST /api/v1/factory-reset`, `POST /api/v1/update` |

## Endpoints

### `GET /api/v1/status`

Returns firmware version, authoritative power state, sensed state, output activity, Wi-Fi status, Zigbee status, OTA capability, currently present BLE matcher labels, and an HP Common Slot protocol `psu_i2c` object. When PSU I²C is enabled and a complete checksum-verified sample is available, the object includes `age_ms`, `input_voltage_v`, `input_current_a`, `output_voltage_v`, `output_current_a`, `internal_temperature_c`, and `fan_speed_raw`. The temperature is in degrees Celsius. When communication fails, `available` is false and those values are omitted. An `error` string describes startup or sampling failures; transfer errors include the PIC address, register, write/read phase, and SDA/SCL GPIO levels. It clears after a complete valid sample.

`power_sense_available` is false in PS_ON latch mode. In that mode, `power_state` follows the commanded PS_ON output; `sensed_on` is false because no power input is read.

When PSU I²C is enabled, `psu_i2c.identity` reports the last EEPROM address tried or validated, independent `available`/`error` state, and any checksum-verified FRU `manufacturer`, `product_name`, `part_number`, `revision`, `serial_number`, `board_part_number`, and `rated_capacity_w`. The firmware probes `0x50`–`0x57` and accepts the first address containing a valid FRU; the EEPROM need not be at PIC address minus eight. Empty or unsupported fields are omitted. Identification is retried about once a minute; its failure does not hide valid PIC telemetry. The EEPROM is read only.

### `GET /api/v1/psu/data`

Returns cached raw PSU data without starting an I²C transfer. `pic_read` indicates whether a complete PIC sample has ever succeeded, and `pic_available` says whether the latest poll is valid. When `pic_read` is true, `pic_registers` maps `0x08`, `0x0A`, `0x0E`, `0x10`, `0x1C`, and `0x1E` to their unscaled 16-bit readings; `pic_age_ms` measures the age of that sample. When `eeprom_read` is true, `eeprom_address`, `eeprom_age_ms`, and `eeprom_hex` provide the last complete 256-byte EEPROM read as 512 hexadecimal characters. Raw EEPROM bytes remain available even when their FRU data does not decode. The endpoint has the same authentication as the status endpoint.

### `POST /api/v1/wifi/ap/close`

Closes the active setup/recovery AP without saving configuration or rebooting. Returns `202 Accepted` before disconnecting clients. Wi-Fi devices resume their saved Wi-Fi station connection; Zigbee-only devices stop Wi-Fi and resume the Zigbee router with its saved pairing. Unconfigured devices stop Wi-Fi. Returns `409 Conflict` if the AP is already off. All APs also close automatically after five minutes without any connected Wi-Fi clients; a connected client keeps the AP open, and the idle period restarts when the last client leaves.

### `POST /api/v1/power`

```json
{"action":"on"}
```

Supported actions are `on`, `off`, `toggle`, and `force_off`. The response acknowledges that the action was queued; the state machine may subsequently reject a conflicting action or one sent during fault cooldown. Read `/api/v1/status` to observe the result. `409 Conflict` means the action was invalid or the queue was full.

### `GET /api/v1/config`

Returns the complete non-secret configuration used by the setup UI, plus `recommended_gpios` for advisory board guidance and `blocked_gpios` for hard exclusions (`[12,14]` on C5, `[]` on C6). Pins outside the recommended list are allowed without an override unless blocked. The legacy `advanced_gpio_override` field remains accepted for compatibility and cannot bypass blocked pins.

### `PUT /api/v1/config`

Applies a partial JSON patch, validates GPIO ranges, blocked pins, required pins, conflicts, and timing constraints, stores it in the pending configuration slot, then reboots. The previous active configuration remains available until the new firmware has run healthily for 30 seconds. `psu_i2c` accepts `enabled`, `sda_gpio`, `scl_gpio`, `address` (decimal 88–95 for `0x58`–`0x5F`), and `poll_interval_ms` (500–60000). It is disabled by default. `radio_profile` accepts only `wifi` or `zigbee` when supported by the target. The removed `hybrid` value and other invalid profiles return `400 Bad Request`. For method requirements and GPIO restrictions, see [Configuration](CONFIGURATION.md).

`hold_ps_on` is a boolean, default `false`. Set `{"hold_ps_on":true}` to keep the ESP32's PS_ON contact closed whenever board power is detected, including external startup, an already-running board at service startup, and shutdown until power sense turns off. It requires an assigned PS_ON GPIO even with button-only startup. A shutdown timeout preserves the hold while the board is still sensed on. This field is also returned by `GET /api/v1/config`; omitting it from a patch preserves its current value.

Set `timing.strategy` to `4` for PS_ON latch mode. This requires a PS_ON GPIO but permits `power_sense.gpio` and `power_button.gpio` to be `-1`. `hold_ps_on` has no effect in latch mode.

### `POST /api/v1/ble/scan`

Starts a 15-second active discovery scan.

### `GET /api/v1/ble/scan`

Returns recent discovery results with address, address type, name, and RSSI.

### `POST /api/v1/i2c/scan`

Scans 7-bit I²C addresses `0x08`–`0x77` on the supplied SDA/SCL pins and returns decimal addresses. The pins must pass the same GPIO range, blocked-pin, and conflict checks as PSU configuration; board guidance is advisory. If the PSU monitor is already running, the request must use its active pins. The scan does not change saved settings.

```json
{"sda_gpio":4,"scl_gpio":5}
```

Example complete response: `{"complete":true,"scanned_addresses":112,"timeout_count":0,"addresses":[88,95]}`. A `409 Conflict` means the running I²C bus uses different pins. If SCL is already low, the scan returns `complete:false`, `scanned_addresses:0`, and a clock-held-low error without probing or resetting the bus. A timed-out probe gets one bus-clear/retry attempt only when the clock is released. If the bus returns idle, the scan continues and reports the timed-out address in `error` with `complete:false`; results can still contain later devices. A held-low line during probing or the three-second scan limit stops early, with `scanned_addresses` and any devices found before the fault preserved in a `200 OK` response. Thus `addresses:[]` with `complete:false` is inconclusive. A separate busy message means another client held the bus for over one second; retry the scan. Internal pull-ups are enabled as a fallback, but external 3.3 V pull-ups are recommended.

### `GET /api/v1/events`

Streams server-sent `status` events when the state or optocoupled sense changes, with keepalives and automatic reconnect after a bounded one-minute session.

### `POST /api/v1/zigbee`

Send `{"action":"commission"}` to start pairing. On the setup AP, this requires a saved, configured Zigbee profile. It returns `202 Accepted` with `{"accepted":true,"disconnecting":true}`, then closes Wi-Fi and waits up to 15 seconds for Zigbee initialization before requesting joining. Factory-new automatic joining is not duplicated, and an existing joined network is preserved. Follow the result on the coordinator or serial shell; the HTTP acknowledgement does not mean pairing has completed. If initialization or queueing fails, the controller attempts to reopen setup Wi-Fi.

Wi-Fi mode, incomplete setup, or another pending radio change returns `409 Conflict` without closing Wi-Fi. Without the AP, commissioning still requires Zigbee to be running. `{"action":"reset"}` retains the existing behavior: Zigbee must be running, so use serial `zigbee reset` after closing the AP to clear only the Zigbee network.

### `POST /api/v1/factory-reset`

Only available from the configuration AP. Send `{"confirm":"ERASE ALL"}` to erase all NVS configuration and Zigbee network data, then reboot into first-boot provisioning.

### `POST /api/v1/update`

Available only in 8 MB builds. Send the application binary as the request body. ESP-IDF writes the inactive OTA partition and validates the image before switching boot slots. A newly booted image marks itself valid after 30 seconds.

The endpoint does not accept a complete factory image containing bootloader and partitions.
