# Serial shell

The controller opens an interactive `bc250>` prompt on its primary ESP-IDF console in every radio profile, including first-boot setup and recovery mode. C5 profiles use the chip's native USB Serial/JTAG port for input and output. Connect to that port, rather than the USB-to-UART bridge if the board has two USB connectors. C6 profiles use UART0 at 115200 baud. A secondary console shows output but does not accept commands. See [Espressif's C5 USB console guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/api-guides/usb-serial-jtag-console.html) for port identification and wiring.

Enter ordinary commands and read plain-text replies. There are no JSON commands or JSON replies in the shell. On an ANSI terminal, use Left/Right to edit, Up/Down for recent commands, Tab to complete commands and setting names, Backspace/Delete to correct mistakes, and Ctrl+C to cancel the current line. History holds up to 20 commands in memory and clears on reboot. `set` commands are not added to history.

The shell detects basic terminals automatically and provides a prompt, echo, backspace, and Ctrl+C without terminal escape sequences. Enter `terminal ansi` to enable full editing after switching to a compatible terminal, or `terminal plain` to return to basic input. Disable local echo in your terminal because the controller echoes input. Informational service logs stay enabled by default. `logs off` reduces them to errors for a quieter prompt; `logs on` restores them. This choice lasts until reboot.

At boot, the console prints the operating mode and radio profile, configuration source and validation status, hostname, Wi-Fi SSID and whether a password is set, Zigbee identity/channel, GPIO assignments, power timings, saved BLE controllers and enabled counts, button actions, and PSU I²C settings. The summary does not print passwords. GPIO `-1` means disabled and Zigbee channel `0` means automatic selection.

Runtime logs show controller arrival with its address and RSSI, absence and rearming, and whether arrival queues power-on or skips it because power is already sensed on. The power service then reports whether a queued command actually triggers a sequence, is unnecessary, or is rejected because shutdown or retry cooldown is active. It also logs debounced button presses/releases and short/double/long gestures with their assigned actions, PS_ON and power-button output changes, filtered power-sense changes, sequence timeouts, Wi-Fi connection/reconnect and AP client events, Zigbee startup/pairing/pause/resume and power requests, configuration promotion, PSU availability, reboot/reset, and firmware upload results. Repeated matching BLE advertisements and unchanged power/PSU polling do not produce informational logs.

```text
bc250> status
Firmware:      0.1.0
Power:         off (sense: off)
...
bc250> on
Power command queued. Enter status to check the result.
bc250> set hostname bc250-lab
Setting updated. Enter save to apply, or discard to undo.
bc250*> set wifi_ssid "My Wi-Fi"
Setting updated. Enter save to apply, or discard to undo.
bc250*> save
Settings saved. Rebooting...
```

| Command | Result |
|---|---|
| `help` or `?` | Show commands and examples |
| `status` | Readable power, Wi-Fi, Zigbee, BLE presence, PSU telemetry, and available EEPROM identification |
| `on`, `off`, `toggle`, `force-off` | Queue a power action; `power <action>` also works |
| `config` | List settings, including unsaved edits; passwords stay hidden |
| `set <setting> <value>` | Edit one setting in memory |
| `save` | Validate all edits, stage the configuration, and reboot |
| `discard` | Discard unsaved edits |
| `ble scan` | Start a 15-second discovery scan |
| `ble results` | Show discovered addresses, names, address types, and RSSI |
| `i2c scan <SDA> <SCL>` | Scan validated I²C pins; show addresses, progress, and partial results after a fault |
| `psu data` | View cached raw PIC registers and a hex/ASCII EEPROM dump, with sample ages |
| `zigbee commission`, `zigbee reset` | Start joining or clear only Zigbee network state |
| `wifi ap` | Open the setup AP in any radio profile; closes after five minutes with no connected clients |
| `admin reset` | Generate, save, and display a new admin password once |
| `factory reset ERASE ALL` | Erase all NVS data, including settings and Zigbee state, then reboot |
| `reboot` | Restart and discard unsaved shell edits |
| `logs on`, `logs off` | Enable service logs or show only errors |
| `terminal plain`, `terminal ansi` | Switch between basic input and full line editing |

`config get` and `config show` are aliases for `config`. `config set <setting> <value>`, `config save`, and `config discard` are also supported. Command names and setting values are case sensitive. Leading/trailing spaces and repeated spaces between arguments are accepted. Each command is limited to 512 characters.

The radio profile accepts `wifi` or `zigbee`. `wifi ap` pauses Zigbee while the AP is open; closing the AP from the portal or allowing it to expire resumes Zigbee without erasing its pairing. Run `zigbee commission` and `zigbee reset` with the AP closed.

The shell prints `Zigbee: joining network. Enable permit-join on your coordinator.` when joining actually starts, including automatic joining on a factory-new network and joining from a local button. It prints the result when joining succeeds or fails, or `Zigbee: joining stopped.` if the stack is paused, reset, or leaves the network during an attempt. These messages appear even with `logs off`. The `status` command shows `Zigbee: joining` while an attempt is active. A configured status LED flashes twice every second during the attempt; after a failure, enable permit-join and run `zigbee commission` to retry.

For coordinator pairing, moving to another network, radio settings, and ZHA/Zigbee2MQTT device definitions, see [ZIGBEE.md](ZIGBEE.md).

## Editing settings

Enter `config` to see setting names and values; Tab completes editable names after `set `. Quote values containing spaces with single or double quotes. Backslash escapes a character outside single quotes. Shell expansion is not performed, so `$` is literal. Boolean values accept `on`/`off`, `true`/`false`, `yes`/`no`, or `1`/`0`. Numbers are decimal, with explicit `0x` hexadecimal also accepted. Use `-1` to disable a GPIO.

```text
set radio wifi
set wifi_ssid "My Wi-Fi"
set wifi_password "my network password"
set admin_password "my new admin password"
set ps_on.gpio 4
set ps_on.active_high on
set power_button.gpio 5
set power_sense.gpio 6
set status_led.gpio 8
set timing.strategy ps_on_then_button
set hold_ps_on on
set psu_i2c.sda_gpio 9
set psu_i2c.scl_gpio 10
set psu_i2c.address 0x5f
set psu_i2c.enabled on
config
save
```

These pin numbers are examples; choose pins for your board and wiring. `power_button.gpio` is the **motherboard power-switch output**, which drives the optocoupler at the motherboard header; it does not read a physical pushbutton. `radio` is a short alias for `radio_profile`. The `pins.` prefix is optional for GPIO roles, so `set pins.ps_on.gpio 4` also works. Startup strategies are `ps_on_only`, `button_only`, `ps_on_then_button`, and `simultaneous`. An empty `wifi_password` clears the credential in the shell.

All physical buttons, including power and auxiliary buttons, use the custom `buttons` slots. For a switch from GPIO to ESP ground, set `active_high off` and `pull_up on` to use the ESP internal pull-up without an external resistor. Buttons and BLE matchers use zero-based slots. Set `button_count` or `ble_device_count` to include the slots you want, then edit their fields:

```text
set button_count 1
set buttons.0.gpio 23
set buttons.0.active_high off
set buttons.0.pull_up on
set buttons.0.short_action toggle
set buttons.0.double_action none
set buttons.0.long_action force_off
set buttons.0.enabled on
set ble_device_count 1
set ble_devices.0.label "My phone"
set ble_devices.0.type name_prefix
set ble_devices.0.value "My phone"
set ble_devices.0.min_rssi -80
set ble_devices.0.enabled on
```

Button actions are `none`, `on`, `off`, `toggle`, `force_off`, `config_ap`, `zigbee_commission`, and `zigbee_reset`. BLE matcher types are `address`, `name_exact`, `name_prefix`, `service_uuid`, and `manufacturer_data`. `config` shows fields for slots included by the current counts. Unused slots remain available through Tab completion.

Edits remain in memory until `save`; `bc250*>` marks unsaved changes. Each successful edit reports current configuration errors and GPIO advisories immediately. You can still edit several related fields, including swapping GPIO roles, before saving. GPIO guidance is warning-only except GPIO 12 and 14 on C5, which are blocked because of NodeMCU C5 Mini boot failures. The legacy override cannot bypass these exclusions. `save` checks blocked and required pins, duplicate assignments, numeric ranges, and configuration consistency, stages the result, and reboots. Invalid settings leave edits available for correction. Saved changes follow the existing 30-second health check and rollback process. During first setup, set `configured on` after supplying the required pins and radio settings. `status`, scans, and power commands always use the running configuration until reboot.

`admin reset` takes effect immediately, updates both active and pending configuration, and refreshes the password hash in unsaved shell edits. Neither pending rollback nor a later shell save restores the old password. The setup AP stays open without a Wi-Fi password.

Treat physical serial access as administrator access. There is no serial login. The generated admin password appears once; keep terminal captures private. Entered passwords are echoed, so terminal recordings can contain them. The HTTP API continues to use JSON for local integrations; see [API.md](API.md).
