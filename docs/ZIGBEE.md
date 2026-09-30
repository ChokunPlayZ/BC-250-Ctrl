# Zigbee setup and device definitions

The ESP32-C5/C6 controller joins an existing Zigbee network as an always-powered **router**. Endpoint 1 accepts standard On, Off, and Toggle commands and exposes the BC-250's optocoupled power-sense input as its On/Off state. Optional HP Common Slot PSU monitoring adds voltage, current, and raw fan readings.

This guide covers commissioning, Home Assistant ZHA, Zigbee2MQTT, the endpoint definition, and recovery. It describes the implementation in [zigbee_service.c](../src/zigbee_service.c); the supplied definitions are local integration files, not bundled upstream device support. Real coordinator and hardware acceptance is still required; see [verification](#verification).

## Before pairing

1. Build and flash the correct C5/C6 and flash-size profile using the [README](../README.md#build-targets).
2. Supply the controller from standby power so it stays online when the BC-250 is off. Complete the isolated power-button, PS_ON, and power-sense wiring in [WIRING.md](WIRING.md). Configure the GPIOs, polarities, and startup strategy before expecting Zigbee commands to control the board.
3. Set up a Zigbee coordinator using either ZHA or Zigbee2MQTT. The controller firmware is a router, not a coordinator. A device can belong to only one network at a time; see [ZHA's network concepts](https://www.home-assistant.io/integrations/zha/#zigbee-concepts).
4. If you want PSU sensors, enable PSU I²C, assign SDA/SCL, select the PIC address, and save **before the first device interview**. The default address is `0x5F` (decimal 95). Confirm valid readings with serial `status`. See [HP PSU wiring](WIRING.md#optional-hp-common-slot-psu-i²c) for the electrical connections.

### Controller settings

Join the open `BC250-Ctrl-XXXX` setup network and visit `http://192.168.4.1/`. Select **Zigbee** under Connection mode, then save and reboot. Alternatively, on an already wired/configured controller, edit these settings in the [serial shell](SERIAL.md):

```text
set radio zigbee
set zigbee_channel 0
set zigbee_manufacturer BC250
set zigbee_model "BC250 Controller"
config
save
```

During first setup, also provide the required GPIO settings and `set configured on` before `save`. The example above changes radio settings only; it does not assign pins.

| Setting | Default | Meaning |
|---|---|---|
| `radio_profile` (shell alias `radio`) | `wifi` | Must be `zigbee` for the Zigbee service to start |
| `zigbee_channel` | `0` | `0` searches channels 11–26; a value from 11–26 gives that channel priority, with all channels still in the secondary scan set |
| `zigbee_manufacturer` | `BC250` | Basic ManufacturerName, up to 32 bytes |
| `zigbee_model` | `BC250 Controller` | Basic ModelIdentifier, up to 32 bytes |
| `psu_i2c.enabled` | `false` | Adds Electrical Measurement and Analog Input clusters at boot |
| `psu_i2c.poll_interval_ms` | `2000` | Local PSU sampling interval; valid range 500–60000 ms |

The channel setting does not change the coordinator's network channel or move a paired device to a different network. Keep the identity strings at their defaults when using the supplied device definitions. Matching is case sensitive, including spaces; the configured hostname does not affect Zigbee matching.

### Close the setup AP

**Zigbee is paused for the entire setup AP session.** In the portal, expand **Setup Wi-Fi options**, select **Turn off setup Wi-Fi** and confirm **Turn off and disconnect**, or disconnect every Wi-Fi client and wait for the five-minute idle timeout. A connected client keeps the AP open. After it closes, Wi-Fi stops in Zigbee mode and the router resumes with its saved network data. BLE scanning and the serial shell remain available.

For manual pairing from the portal, use **Start pairing** on the overview, then **Turn off Wi-Fi & pair**. Save or discard pending edits first. The controller acknowledges the request before disconnecting the page, closes Wi-Fi, and waits for Zigbee to initialize. If startup fails, it attempts to reopen setup Wi-Fi. Pairing progress appears on the coordinator and serial shell. An existing saved network is preserved; this button does not reset pairing.

Network reset still uses serial commands or a configured local button with the AP closed. A successful 30-second configuration health check does not prove Zigbee has joined.

## Pairing and network recovery

1. Enable **permit join / add device** on the coordinator.
2. In the portal, select **Start pairing**, then **Turn off Wi-Fi & pair**. Wi-Fi closes and the page disconnects. A factory-new Zigbee stack also starts joining automatically whenever it starts.
3. To retry, reopen setup Wi-Fi with a configured button or `wifi ap` and use **Start pairing**, or enter `zigbee commission` with the AP closed. This preserves controller settings.
4. Watch the shell for `Zigbee: joined network.` and run `status`. Its Zigbee line should show `joined`. A configured status LED flashes twice per second while joining; messages still appear with `logs off`.
5. Wait for the coordinator's interview/configuration to finish. Test On and Off and compare the displayed state with the hardware sense and serial `status`.

Commands are asynchronous: `Zigbee joining request queued.` acknowledges the request, not the completed join. A failed join prints retry guidance. Ordinary reboot and closing the AP reuse the saved network data; commissioning is not needed after every restart. The local `joined` flag reflects stack initialization/steering and is not a continuous connectivity test, so also check coordinator communication.

### Move to another network

1. Remove the device from the old coordinator when available.
2. With the AP closed, run `zigbee reset`. This clears Zigbee datasets and restarts the controller while retaining GPIO, radio, BLE, and PSU settings.
3. Enable permit join on the new coordinator and allow automatic joining after restart. If needed, run `zigbee commission` again.

`factory reset ERASE ALL` and the portal's **Erase all settings** perform a full NVS reset, including controller configuration and pairing. Use them only when you intend to configure the controller again. A local button can be assigned `zigbee_commission` or `zigbee_reset`; choose its gesture deliberately, since network reset removes pairing.

## Zigbee2MQTT

Use [bc250.mjs](zigbee/zigbee2mqtt/bc250.mjs) as an external converter. It matches the default manufacturer/model, provides a switch, and adds PSU exposes only when the interviewed endpoint advertises Electrical Measurement. It does not require an MQTT client on the ESP32; Zigbee2MQTT handles MQTT on the coordinator host.

### Install and pair

1. Copy `bc250.mjs` into `external_converters/` beside Zigbee2MQTT's `configuration.yaml`. With a container, use the mounted data directory. The frontend also supports **Settings → Dev console → External converters**.
2. Enable external converters if required by your installation, merging this into the existing configuration:

   ```yaml
   advanced:
     enable_external_js: true
   ```

3. Restart Zigbee2MQTT and check its log for converter loading errors. See the [official external-converter installation guide](https://www.zigbee2mqtt.io/advanced/more/external_converters.html) and [`enable_external_js` setting](https://www.zigbee2mqtt.io/guide/configuration/all-settings.html#enable_external_js).
4. Enable Permit join, then follow [pairing](#pairing-and-network-recovery). Rename the device, for example `bc250`.
5. Check Exposes for `state`; with PSU monitoring enabled, also expect the six telemetry properties below. For an existing device, run **Reconfigure** after installing the converter. If its saved cluster list predates PSU enablement, perform another interview or remove and pair it again.

### Commands and readings

For the default MQTT base topic and friendly name `bc250`:

| Topic | JSON payload | Action |
|---|---|---|
| `zigbee2mqtt/bc250/set` | `{"state":"ON"}` | Request the configured startup sequence |
| `zigbee2mqtt/bc250/set` | `{"state":"OFF"}` | Request normal shutdown |
| `zigbee2mqtt/bc250/set` | `{"state":"TOGGLE"}` | Toggle using the On/Off cluster |
| `zigbee2mqtt/bc250/get` | `{"state":""}` | Read the On/Off attribute |
| `zigbee2mqtt/bc250/get` | `{"psu_input_voltage":""}` | Read one PSU measurement |
| `zigbee2mqtt/bc250/get` | `{"psu_fan_raw":""}` | Read fan value and validity flags |

The converter binds On/Off reporting with minimum interval 0, maximum 300 seconds, and change 1. It reads the initial state and, if present, PSU attributes during configuration. It also reads fan value/flags on Zigbee2MQTT startup and device announcements to initialize its validity cache. PSU reports come directly from firmware; it does not configure unsupported power or energy attributes. The setter returns no optimistic state, so the command itself does not publish a fabricated power result. Let the hardware sequence finish and use a report or `/get` to check it.

| Property | Unit / values |
|---|---|
| `state` | `ON` / `OFF`, from the On/Off attribute |
| `psu_input_voltage` | V, or `null` for an invalid sample |
| `psu_input_current` | A, or `null` for an invalid sample |
| `psu_output_voltage` | V, or `null` for an invalid sample |
| `psu_output_current` | A, or `null` for an invalid sample |
| `psu_fan_raw` | Raw PIC fan value, or `null` until valid flags are received / while faulted |
| `psu_available` | Boolean from Analog Input fault flags; independent of Zigbee2MQTT device availability |

Reports may arrive separately; MQTT payloads can contain cached values from preceding reports. If Home Assistant discovery is enabled in Zigbee2MQTT, check the discovered switch and sensors there too. Do not run ZHA against the same coordinator at the same time.

## Home Assistant ZHA

The standard On/Off Output endpoint can be discovered as a power switch without a custom quirk. For the named PSU sensors and invalid-reading handling, install [bc250.py](zigbee/zha/bc250.py). It uses the ZHA v2 quirk builder and applies only to the default identity with both PSU clusters on endpoint 1. A controller with PSU monitoring disabled retains normal ZHA discovery.

### Install the optional PSU definition

1. Create `/config/custom_zha_quirks/` in Home Assistant and copy `bc250.py` into it. For Container installations, this means the directory mounted at `/config` inside the container.
2. Merge this into Home Assistant's existing `configuration.yaml`:

   ```yaml
   zha:
     custom_quirks_path: /config/custom_zha_quirks
   ```

3. Check configuration and restart Home Assistant. Check the logs for import errors before pairing. This file targets the current `zhaquirks.builder` API; if that module is missing, update Home Assistant rather than installing Python packages into its managed environment. See the [upstream device-handler project](https://github.com/zigpy/zha-device-handlers) for quirk development.
4. Open ZHA's **Add device** flow and follow [pairing](#pairing-and-network-recovery). ZHA's navigation can vary by release; see the [official ZHA guide](https://www.home-assistant.io/integrations/zha/#adding-devices).
5. Check the switch and these additional entities: **PSU input voltage**, **PSU input current**, **PSU output voltage**, **PSU output current**, **PSU fan raw**, and diagnostic **PSU telemetry fault**. The fault sensor is on when the PSU sample is invalid. Numeric invalid samples become unknown.

The quirk replaces Analog Input handling to clear a stale fan reading when a fault is reported, and suppresses duplicate generic PSU entities and the generic writable Analog Input number. It leaves the standard On/Off cluster available for ZHA control. Sensor entity IDs depend on the device name and existing entity registry.

For an already paired device, restart first and check its device diagnostics for the applied quirk. Use ZHA's Reconfigure action to refresh configuration. If the endpoint's saved cluster list is stale after toggling PSU monitoring, remove and pair the device again. Installing a quirk or changing manufacturer/model strings does not automatically refresh an old interview.

### Custom manufacturer or model

If you change `zigbee_manufacturer` or `zigbee_model`, update the integration file to match the exact saved strings:

- Zigbee2MQTT: edit `manufacturerName` and `modelID` in the converter's `fingerprint`. Its top-level `vendor` and `model` are display labels; they do not replace the fingerprint.
- ZHA: edit the two arguments to `QuirkBuilder("BC250", "BC250 Controller")`.

Save/reboot the controller, restart the coordinator integration to load the changed definition, and refresh its interview. Use a friendly device name in the coordinator when you only want to rename the device in dashboards; this preserves the default matching strings.

## Device definition on the wire

Both C5 and C6 use the same identity and endpoint layout. The Zigbee stack stores its datasets in `nvs`; normal firmware updates that preserve NVS also preserve pairing.

| Field | Value |
|---|---|
| Network role | Router, maximum 10 children |
| Radio | Native 2.4 GHz IEEE 802.15.4 |
| Application endpoint | `1` |
| Application profile | Home Automation, `0x0104` |
| Application device ID | On/Off Output, `0x0002` |
| Basic ManufacturerName (`0x0004`) | `BC250` by default |
| Basic ModelIdentifier (`0x0005`) | `BC250 Controller` by default |
| Join security | Centralized network; distributed security disabled; install-code policy disabled |

### Server clusters

| Cluster | ID | Purpose |
|---|---|---|
| Basic | `0x0000` | Device identity |
| Identify | `0x0003` | Standard SDK Identify cluster |
| Groups | `0x0004` | Standard SDK group support |
| Scenes | `0x0005` | Standard SDK scene support |
| On/Off | `0x0006` | Power commands and sensed state |
| Analog Input | `0x000C` | Raw fan and PSU validity; only with PSU I²C enabled |
| Electrical Measurement | `0x0B04` | PSU AC/DC measurements; only with PSU I²C enabled |

The SDK constructs an On/Off Light endpoint, then firmware changes its device ID to On/Off Output. There is no brightness or color interface. Identify does not have a project-specific physical LED handler; the status LED's joining pattern comes from commissioning state.

### Power behavior

On/Off attribute `0x0000` is a ZCL boolean. Standard commands are Off `0x00`, On `0x01`, and Toggle `0x02`. Attribute changes request the normal power-service actions; local sense updates do not issue another power command. The power state machine handles timings, conflicting requests, and fault cooldown. Zigbee acknowledgement does not guarantee that the BC-250 completed its startup or shutdown.

The On/Off value is refreshed from the optocoupled sense input. It does not expose `starting`, `stopping`, or fault details. A coordinator can briefly show its requested state while a command is being processed; verify the final sensed state. Force-off, configuration editing, BLE presence lists, and the detailed power state are available through local controls/API/serial, not dedicated Zigbee commands or sensors. Zigbee OTA is not implemented; use the [documented update paths](../README.md#recovery-and-updates).

### PSU attributes and scaling

All listed attributes use standard cluster encoding, without a manufacturer-specific attribute header. Electrical Measurement MeasurementType (`0x0000`, bitmap32) is `0x00000041`: AC active measurement plus DC measurement.

| Reading | Cluster | Attribute | ZCL type | Conversion | Invalid value |
|---|---|---|---|---|---|
| Input voltage | `0x0B04` | RMSVoltage `0x0505` | uint16 | raw ÷ 10 = V | `0xFFFF` |
| Input current | `0x0B04` | RMSCurrent `0x0508` | uint16 | raw ÷ 100 = A | `0xFFFF` |
| Output voltage | `0x0B04` | DCVoltage `0x0100` | int16 | raw ÷ 100 = V | `0x8000` = −32768 |
| Output current | `0x0B04` | DCCurrent `0x0103` | int16 | raw ÷ 10 = A | `0x8000` = −32768 |
| Fan reading | `0x000C` | PresentValue `0x0055` | float32 | Raw PIC value, **not RPM** | Ignore when faulted |
| Sample validity | `0x000C` | StatusFlags `0x006F` | bitmap8 | Fault bit `0x02` means invalid | `0x02` when unavailable |
| Fan description | `0x000C` | Description `0x001C` | character string | `PSU fan raw` | — |

| Scaling pair | Multiplier attribute / value | Divisor attribute / value |
|---|---|---|
| AC voltage | `0x0600` / 1 | `0x0601` / 10 |
| AC current | `0x0602` / 1 | `0x0603` / 100 |
| DC voltage | `0x0200` / 1 | `0x0201` / 100 |
| DC current | `0x0202` / 1 | `0x0203` / 10 |

For example, input voltage `2305` is 230.5 V, input current `123` is 1.23 A, output voltage `1208` is 12.08 V, and output current `154` is 15.4 A. Apply scaling once. Decode DC values as signed before checking for −32768.

Firmware updates attributes on PSU samples and sends explicit reports to coordinator short address `0x0000`, **destination endpoint 1**. It sends an initial set after joining, then valid periodic reports at least 10 seconds apart, or at the PSU poll interval when that interval is longer. A detected validity change bypasses the 10-second limit. A PSU read failure is detected at the next poll, not instantly when a wire disconnects.

When unavailable, firmware updates all electrical readings to their sentinels, sets fan PresentValue locally to zero, and reports the fault flag. It does not send the invalid fan value as a fresh measurement. The coordinator must clear/ignore any cached fan reading when it receives the fault flag. Subsequent unavailable samples do not repeatedly report unless the first set failed; valid readings resume when communication recovers. StatusFlags reports are sent on the initial set and validity changes, rather than every valid sample.

PSU internal temperature is currently available through serial, web status, and HTTP only. Active power, apparent power, energy, calibrated fan RPM, and fan control are not exposed. Input volts × amps is not an active-power measurement.

## Troubleshooting

| Symptom | Check / action |
|---|---|
| `zigbee commission` / reset reports unavailable | Run `status`; confirm configured Zigbee mode and **Setup AP: closed**. After saving/rebooting, allow the stack to initialize. |
| Joining fails | Enable permit join and retry `zigbee commission`; move near the coordinator. If moving networks, use the network-reset procedure. Keep channel `0` unless you need a preferred first scan. |
| Router disappears while editing settings | The setup AP pauses Zigbee. Close it to restore pairing and communication. Devices routing through this controller also lose that route while it is paused. |
| Joined locally but missing/offline at the coordinator | The local joined flag is not a link health check. Check coordinator logs, the interview, current network, and radio coverage. |
| Zigbee2MQTT says unsupported / ZHA quirk does not apply | Check the converter/quirk loading log and exact manufacturer/model strings. Verify PSU clusters exist for the optional ZHA quirk. |
| PSU entities missing after enablement | Save/reboot with PSU enabled and refresh the coordinator interview; cluster discovery is cached. |
| 6553.5 V, −327.68 V, or other impossible readings | Invalid sentinels were scaled as measurements or scaling was applied twice. Use the supplied definition and compare with serial `status`. |
| Fan remains at an old value after a PSU fault | Check Analog Input StatusFlags, and confirm the definition clears its cache on fault. A raw zero by itself does not establish validity. |
| All PSU values unknown while Zigbee switch works | Check serial PSU availability, PIC address, standby supply, SDA/SCL, and pull-ups. PSU availability and Zigbee connectivity are separate. |
| Switch command succeeds but power does not change | Check sensed state, wiring/polarity, fault state, and cooldown in serial `status`; acknowledgement only means the Zigbee command was processed. |
| No PSU reports on another coordinator implementation | Firmware targets coordinator endpoint 1. Confirm that endpoint accepts Electrical Measurement and Analog Input reports. |

## Verification

The supplied definitions were checked locally with `zigbee-herdsman-converters` 26.108.1, `zha-quirks` 2.2.2, `zha` 2.2.2, and `zigpy` 2.2.0. Local checks cover loading, optional-cluster handling, measurement conversion, unavailable values, and fan fault/recovery handling. These checks do not establish compatibility with every Home Assistant release or replace an actual device interview.

On your real network, use the [radio interoperability checklist](TESTING.md#radio-interoperability), plus:

- Pair with PSU monitoring off: confirm one usable switch and no fabricated PSU measurements.
- Pair with PSU monitoring on: confirm both extra clusters, named sensors, and units. Compare all four measurements and the raw fan value with serial `status`.
- Request On, Off, and Toggle; also change power locally and confirm final Zigbee state follows sense.
- Observe periodic PSU reports, interrupt a PSU read, and verify sentinels clear numeric sensors and the fault flag clears the cached fan. Restore communication and verify recovery without re-pairing.
- Open/close the setup AP and restart the controller/coordinator; confirm pairing survives and reports resume.
- Test network reset separately from a full factory reset. After changing identity or optional-cluster settings, verify matching and a fresh interview.
