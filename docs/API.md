# Local HTTP API

The API is available in Wi-Fi operation. It is also available on the open configuration AP, which requires neither a Wi-Fi password nor HTTP Basic authentication. Normal station-mode requests use HTTP Basic authentication with username `admin`.

All configuration responses redact the Wi-Fi password and password hash. Sending an empty `wifi_password` preserves the current credential.

## Endpoints

### `GET /api/v1/status`

Returns firmware version, authoritative power state, sensed state, output activity, Wi-Fi status, Zigbee status, OTA capability, currently present BLE matcher labels, and an HP Common Slot protocol `psu_i2c` object. When PSU I²C is enabled and a complete checksum-verified sample is available, the object includes `age_ms`, `input_voltage_v`, `input_current_a`, `output_voltage_v`, `output_current_a`, `internal_temperature_f`, and `fan_speed_raw`. When communication fails, `available` is false and those values are omitted. An `error` string describes startup or sampling failures; transfer errors include the PIC address, register, write/read phase, and SDA/SCL GPIO levels. It clears after a complete valid sample.

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

Applies a partial JSON patch, validates GPIO ranges, blocked pins, required pins, conflicts, and timing constraints, stores it in the pending configuration slot, then reboots. The previous active configuration remains available until the new firmware has run healthily for 30 seconds. `psu_i2c` accepts `enabled`, `sda_gpio`, `scl_gpio`, `address` (decimal 88–95 for `0x58`–`0x5F`), and `poll_interval_ms` (500–60000). It is disabled by default. `radio_profile` accepts only `wifi` or `zigbee`; the removed `hybrid` value and other invalid profiles return `400 Bad Request`.

### `POST /api/v1/ble/scan`

Starts a 15-second active discovery scan.

### `GET /api/v1/ble/scan`

Returns recent discovery results with address, address type, name, and RSSI.

### `POST /api/v1/i2c/scan`

Scans 7-bit I²C addresses `0x08`–`0x77` on the supplied SDA/SCL pins and returns decimal addresses. The pins must pass the same GPIO range, blocked-pin, and conflict checks as PSU configuration; board guidance is advisory. If the PSU monitor is already running, the request must use its active pins. The scan does not change saved settings.

```json
{"sda_gpio":4,"scl_gpio":5}
```

Example response: `{"addresses":[88,95]}`. A `409 Conflict` means the running I²C bus uses different pins. A timed-out probe gets one bus-clear/retry attempt. If it still fails, the error response identifies the address and SDA/SCL GPIO levels. A separate busy message means another client held the bus for over one second; retry the scan. Internal pull-ups are enabled as a fallback, but external 3.3 V pull-ups are recommended.

### `GET /api/v1/events`

Streams server-sent `status` events when the state or optocoupled sense changes, with keepalives and automatic reconnect after a bounded one-minute session.

### `POST /api/v1/zigbee`

This endpoint returns `409 Conflict` when Zigbee is unavailable. Zigbee is paused throughout setup AP sessions, and Wi-Fi-only mode does not run Zigbee. Use serial `zigbee commission` or `zigbee reset` after closing the AP for manual joining or resetting only Zigbee network state.

### Zigbee PSU telemetry

When PSU I²C monitoring and Zigbee are both enabled, endpoint 1 exposes the standard Electrical Measurement cluster (`0x0B04`) and Analog Input cluster (`0x000C`). The controller sends attribute reports to coordinator short address `0x0000`, endpoint 1. It sends the first sample after joining, then available samples at least 10 seconds apart (or at the configured PSU poll interval if longer). It reports the transition to unavailable once, and reports again when readings return.

| Reading | Cluster attribute | Zigbee value and scale |
|---|---|---|
| PSU input voltage | RMSVoltage `0x0505` | unsigned, value ÷ 10 = V |
| PSU input current | RMSCurrent `0x0508` | unsigned, value ÷ 100 = A |
| PSU output voltage | DCVoltage `0x0100` | signed, value ÷ 100 = V |
| PSU output current | DCCurrent `0x0103` | signed, value ÷ 10 = A |
| PSU fan reading | Analog Input PresentValue `0x0055` | raw PIC value as a float; **not RPM** |

The corresponding Electrical Measurement multiplier attributes are 1 and divisors are 10 or 100 as shown above. On an invalid PSU sample, the AC attributes become `0xFFFF`, the DC attributes become `0x8000`, and Analog Input StatusFlags `0x006F` sets the fault bit (`0x02`). The fan's PresentValue is then zero and must be ignored while the fault bit is set. Zigbee coordinators can read these attributes directly; presenting each as a named sensor may require a coordinator-specific device definition.

See [the Zigbee guide](ZIGBEE.md) for pairing, the complete endpoint and scaling definitions, reporting behavior, and supplied [Zigbee2MQTT](zigbee/zigbee2mqtt/bc250.mjs) and [ZHA](zigbee/zha/bc250.py) integration files. On PSU failure, the fault flag invalidates any cached fan reading; firmware does not report the zero fan value as a new measurement.

### `POST /api/v1/factory-reset`

Only available from the configuration AP. Send `{"confirm":"ERASE ALL"}` to erase all NVS configuration and Zigbee network data, then reboot into first-boot provisioning.

### `POST /api/v1/update`

Available only in 8 MB builds. Send the application binary as the request body. ESP-IDF writes the inactive OTA partition and validates the image before switching boot slots. A newly booted image marks itself valid after 30 seconds.

The endpoint does not accept a complete factory image containing bootloader and partitions.
