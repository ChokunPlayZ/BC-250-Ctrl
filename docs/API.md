# Local HTTP API

The API is available in Wi-Fi and hybrid operation. It is also available on the temporary configuration AP. Normal station-mode requests use HTTP Basic authentication with username `admin`; the AP relies on its WPA2 provisioning password.

All configuration responses redact the Wi-Fi password and password hash. Sending an empty `wifi_password` preserves the current credential.

## Endpoints

### `GET /api/v1/status`

Returns firmware version, authoritative power state, sensed state, output activity, Wi-Fi status, Zigbee status, OTA capability, and currently present BLE matcher labels.

### `POST /api/v1/power`

```json
{"action":"on"}
```

Supported actions are `on`, `off`, `toggle`, and `force_off`. A `409 Conflict` response means the state machine rejected the request, normally because it is cooling down after a fault.

### `GET /api/v1/config`

Returns the complete non-secret configuration used by the setup UI.

### `PUT /api/v1/config`

Applies a partial JSON patch, validates pin conflicts and timing constraints, stores it in the pending configuration slot, then reboots. The previous active configuration remains available until the new firmware has run healthily for 30 seconds.

### `POST /api/v1/ble/scan`

Starts a 15-second active discovery scan.

### `GET /api/v1/ble/scan`

Returns recent discovery results with address, address type, name, and RSSI.

### `GET /api/v1/events`

Returns a server-sent `status` event. The current implementation is a one-shot snapshot; reconnect to receive a fresh event.

### `POST /api/v1/update`

Available only in 8 MB builds. Send the application binary as the request body. ESP-IDF writes the inactive OTA partition and validates the image before switching boot slots. A newly booted image marks itself valid after 30 seconds.

The endpoint does not accept a complete factory image containing bootloader and partitions.

