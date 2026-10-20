# MQTT and Home Assistant

MQTT is optional and disabled by default. It runs on Wi-Fi capable targets using the **Wi-Fi** radio profile; Zigbee mode and ESP32-H targets cannot run it. Physical buttons, BLE, HTTP, and serial control continue to use the same power service.

## Connect Home Assistant

1. Set up a local broker, such as the Mosquitto broker app, and connect Home Assistant's [MQTT integration](https://www.home-assistant.io/integrations/mqtt/) to it. Leave MQTT discovery enabled.
2. In the controller's web UI, open **Settings → MQTT & Home Assistant**. Enable MQTT, enter the broker's hostname or IPv4 address, port (normally `1883`), and any username/password required by the broker. Enter a host alone, such as `192.168.1.10`, without `mqtt://` or a port suffix.
3. Leave **Discovery prefix** at `homeassistant`, or match the prefix configured in Home Assistant. Save and reboot. The section shows whether the broker is connected; serial `status` and HTTP `mqtt_connected` report the same connection state.
4. In Home Assistant, open **Settings → Devices & services → MQTT**. The controller appears as one device, named after its hostname.

This implementation uses MQTT 3.1.1 over TCP on a local network. It does not support TLS or WebSockets. Broker credentials are stored with the other controller settings in NVS. HTTP configuration reads and serial configuration listings hide the MQTT password. An omitted or empty JSON `mqtt.password` preserves the saved password; `mqtt.clear_password: true` clears it. In the web UI, use **Clear saved MQTT password**. A nonempty new password replaces the saved one.

Serial setup uses the same settings:

```text
set mqtt.host 192.168.1.10
set mqtt.port 1883
set mqtt.username bc250
set mqtt.password "your-broker-password"
set mqtt.discovery_prefix homeassistant
set mqtt.enabled on
save
```

The serial editor accepts `set mqtt.password ""` to clear a password. When changing to Zigbee mode, disable MQTT first. Upgrades from configuration schemas 1–4 retain existing settings and start with MQTT disabled.

## Entities and power behavior

Discovery publishes a **Power** switch, **Force off** button, and **Power state** sensor. The switch follows sensed power in sensed methods, or commanded PS_ON state in the two latch methods. It does not confirm that an OS has booted. Normal Off observes the configured power-off delay; Power on during `off_pending` cancels that delay. Force off bypasses the delay and uses the configured force-off behavior.

When PSU I²C monitoring is enabled, discovery also adds input/output voltage and current, internal temperature, and raw fan-speed sensors. A PSU read failure marks these sensors unavailable. Fan speed is an unscaled raw register value, not RPM. Disabling PSU monitoring removes its retained discovery entries on the next broker connection.

State is sent on power-state changes and every five seconds. Discovery and state are retained at QoS 1. A retained last will marks the controller offline after an unexpected disconnect; the broker may take up to its keepalive timeout to detect this. The client reconnects automatically and republishes discovery, state, and availability. It also republishes when Home Assistant sends `online` to `<discovery_prefix>/status`. This follows [Home Assistant's discovery and availability guidance](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery).

## Topics and manual control

Identity comes from the station MAC address, so changing the hostname does not create new entities. For MAC `AA:BB:CC:DD:EE:FF`:

| Topic | Payload |
|---|---|
| `bc250/bc250_aabbccddeeff/set` | Non-retained `ON`, `OFF`, or `FORCE_OFF` commands |
| `bc250/bc250_aabbccddeeff/state` | Status JSON, including `power` (`ON`/`OFF`), `power_state`, and the same PSU object as HTTP status |
| `bc250/bc250_aabbccddeeff/availability` | Retained `online` or `offline` |
| `homeassistant/<component>/bc250_aabbccddeeff/<entity>/config` | Retained Home Assistant discovery JSON |

Command payloads are case-sensitive. Unknown, incomplete/fragmented, and retained command deliveries are ignored. Send commands with retain disabled; the Home Assistant entities already do this. A queued command can still be rejected by the power state machine, for example during fault cooldown. Check the state topic for the result.

Broker access rules should allow this controller to publish its state, availability and discovery topics, and subscribe to its command topic and Home Assistant's status topic. If discovery is missing, check the broker credentials, discovery prefix, and that the controller uses Wi-Fi mode. MQTT broker failure does not block normal startup, configuration promotion, or local control.

Disabling MQTT, changing brokers/prefixes, or factory-resetting the controller leaves discovery entries retained on the old broker. Remove the old device in Home Assistant and clear its retained discovery topics on that broker when retiring or moving it. Reusing the same MAC and discovery prefix updates the existing device.
