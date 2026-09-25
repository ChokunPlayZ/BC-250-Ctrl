# Local HTTP API

The API is available in Wi-Fi and hybrid operation. It is also available on the temporary configuration AP. Normal station-mode requests use HTTP Basic authentication with username `admin`; the AP relies on its WPA2 provisioning password.

All configuration responses redact the Wi-Fi password and password hash. Sending an empty `wifi_password` preserves the current credential.

## Endpoints

### `GET /api/v1/status`

Returns firmware version, authoritative power state, sensed state, output activity, Wi-Fi status, Zigbee status, OTA capability, currently present BLE matcher labels, and an HP Common Slot protocol `psu_i2c` object. When PSU I²C is enabled and a complete checksum-verified sample is available, the object includes `age_ms`, `input_voltage_v`, `input_current_a`, `output_voltage_v`, `output_current_a`, `internal_temperature_f`, and `fan_speed_raw`. When communication fails, `available` is false and those values are omitted.

### `POST /api/v1/power`

```json
{"action":"on"}
```

Supported actions are `on`, `off`, `toggle`, and `force_off`. The response acknowledges that the action was queued; the state machine may subsequently reject a conflicting action or one sent during fault cooldown. Read `/api/v1/status` to observe the result. `409 Conflict` means the action was invalid or the queue was full.

### `GET /api/v1/config`

Returns the complete non-secret configuration used by the setup UI.

### `PUT /api/v1/config`

Applies a partial JSON patch, validates pin conflicts and timing constraints, stores it in the pending configuration slot, then reboots. The previous active configuration remains available until the new firmware has run healthily for 30 seconds. `psu_i2c` accepts `enabled`, `sda_gpio`, `scl_gpio`, `address` (decimal 88–95 for `0x58`–`0x5F`), and `poll_interval_ms` (500–60000). It is disabled by default.

### `POST /api/v1/ble/scan`

Starts a 15-second active discovery scan.

### `GET /api/v1/ble/scan`

Returns recent discovery results with address, address type, name, and RSSI.

### `POST /api/v1/i2c/scan`

Scans 7-bit I²C addresses `0x08`–`0x77` on the supplied SDA/SCL pins and returns decimal addresses. The pins must pass the same safety and conflict checks as PSU configuration. If the PSU monitor is already running, the request must use its active pins. The scan does not change saved settings.

```json
{"sda_gpio":4,"scl_gpio":5}
```

Example response: `{"addresses":[88,95]}`. A `409 Conflict` means the running I²C bus uses different pins; a scan timeout indicates a bus or pull-up problem.

### `GET /api/v1/events`

Streams server-sent `status` events when the state or optocoupled sense changes, with keepalives and automatic reconnect after a bounded one-minute session.

### `POST /api/v1/zigbee`

Send `{"action":"commission"}` to start network steering or `{"action":"reset"}` to erase only Zigbee network state.

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

### `POST /api/v1/factory-reset`

Only available from the configuration AP. Send `{"confirm":"ERASE ALL"}` to erase all NVS configuration and Zigbee network data, then reboot into first-boot provisioning.

### `POST /api/v1/update`

Available only in 8 MB builds. Send the application binary as the request body. ESP-IDF writes the inactive OTA partition and validates the image before switching boot slots. A newly booted image marks itself valid after 30 seconds.

The endpoint does not accept a complete factory image containing bootloader and partitions.
