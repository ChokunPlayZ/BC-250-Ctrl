# First setup and access

Flash the profile for your chip and flash capacity first; see [Build and releases](BUILD.md). Bring up the hardware in the order in [Wiring and installation](WIRING.md#bring-up-order). All external GPIO roles are disabled in a fresh configuration.

## Wi-Fi capable targets

This path applies to ESP32, S3, C3, C5, C6, and C61. C5/C6 can later run Zigbee, but they use the setup access point (AP) to configure it.

1. Open the serial monitor on the primary console. On first initialization, record the generated 12-character `admin` password; it is printed once and is used after joining your normal Wi-Fi network.
2. Join the open `BC250-Ctrl-XXXX` network and open `http://192.168.4.1/`.
3. Select **Wi-Fi** or **Zigbee** connection mode, choose the [power method](CONFIGURATION.md#power-methods), and assign GPIOs that match the verified circuit. Set output and sense polarity correctly. The [GPIO guidance](CONFIGURATION.md#gpio-selection) explains hard exclusions and advisories.
4. Add any physical buttons, BLE matchers, status LED, or [HP PSU monitoring](PSU.md). Set an admin password of at least eight characters. For Wi-Fi mode, select a WPA2-or-stronger network; open, WEP, and WPA-only networks are unsupported.
5. Select **Save & reboot**. The save bar appears after an edit; **Discard** restores the loaded settings. After reboot, a valid pending configuration becomes permanent after 30 healthy seconds. If it cannot become healthy after two boot attempts, the previous configuration is restored. The health check includes any required Wi-Fi station connection. It does not prove Zigbee joining, BLE matching, power sensing, or external wiring.
6. For Zigbee mode, finish with [pairing](ZIGBEE.md#pairing-and-network-recovery). Zigbee runs after setup Wi-Fi closes.

The setup AP and serial shell remain available for recovery. Normal Wi-Fi station access uses HTTP Basic authentication with username `admin` and the configured password. The web UI and [HTTP API](API.md) use the same authentication.

## ESP32-H targets

H2, H21, and H4 have no Wi-Fi, setup AP, or HTTP service. Use the primary serial console to configure them. Choose GPIOs from the exact board schematic. For a PS_ON latch circuit, for example:

```text
set timing.strategy ps_on_latched
set ps_on.gpio <PS_ON_GPIO>
set configured on
save
```

For a sensed method, also assign the power-sense input and motherboard switch output; see [power methods](CONFIGURATION.md#power-methods). `config` lists current values and editable names. `save` validates, stages, and reboots. After a factory-new configured Zigbee router boots, it starts joining; `zigbee commission` retries. Use `ble scan` to discover nearby BLE devices. H targets require a Zigbee coordinator for remote control or configured physical buttons for local control. See [Serial shell](SERIAL.md) for all commands and setting examples.

## Setup AP behavior

Every setup or recovery AP is an **open network**. While it is active, HTTP Basic authentication is bypassed for all UI and API endpoints, including power, configuration, factory reset, and firmware upload on 8 MB builds. Anyone in radio range can use those endpoints. Keep the AP open only while needed.

The AP closes after five minutes with no connected Wi-Fi clients, including on first boot and after `wifi ap`. Any connected client keeps it open; a new five-minute period starts when the last client disconnects. The web interface can close it immediately. On Wi-Fi capable targets, the serial `wifi ap` command opens or refreshes it from any radio profile. A triple reset also opens recovery setup: reset the ESP32 three times consecutively without allowing either of the first two boots to run for 30 seconds. Triple-reset AP recovery is available only on Wi-Fi capable targets.

Opening the AP pauses a Zigbee router before Wi-Fi accepts clients. Closing it restores the configured Wi-Fi station, resumes Zigbee with saved network data in Zigbee mode, or stops Wi-Fi on an unconfigured controller. Wi-Fi and Zigbee do not run together in a combined mode. Older saved combined-mode configurations migrate to Zigbee-only while retaining other settings. The [Zigbee guide](ZIGBEE.md) covers pairing after AP closure.

## Access and security

The local web service uses HTTP. Basic-auth credentials can be seen by anyone able to inspect that network's traffic. Use a trusted LAN and do not port-forward it. NVS configuration has integrity protection, but stored Wi-Fi credentials are not encrypted by default. Secure Boot is not enabled by default, and default OTA validates image integrity without requiring a cryptographic signature. Physical serial access is administrator access; there is no serial login. The [serial shell](SERIAL.md#editing-settings) explains password handling.

If settings prevent access, use [configuration recovery](RECOVERY.md). The serial `admin reset` command generates a new admin password without erasing other settings.
