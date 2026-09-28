# Serial shell

The controller opens an interactive `bc250>` prompt on its primary ESP-IDF console in every radio profile, including first-boot setup and recovery mode. C5 profiles use the chip's native USB Serial/JTAG port for input and output. Connect to that port, rather than the USB-to-UART bridge if the board has two USB connectors. C6 profiles use UART0 at 115200 baud. A secondary console shows output but does not accept commands. See [Espressif's C5 USB console guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/api-guides/usb-serial-jtag-console.html) for port identification and wiring.

Enter ordinary commands and read plain-text replies. There are no JSON commands or JSON replies in the shell. On an ANSI terminal, use Left/Right to edit, Up/Down for recent commands, Tab to complete commands and setting names, Backspace/Delete to correct mistakes, and Ctrl+C to cancel the current line. History holds up to 20 commands in memory and clears on reboot. `set` commands are not added to history.

The shell detects basic terminals automatically and provides a prompt, echo, backspace, and Ctrl+C without terminal escape sequences. Enter `terminal ansi` to enable full editing after switching to a compatible terminal, or `terminal plain` to return to basic input. Disable local echo in your terminal because the controller echoes input. Service logs are reduced to errors once the shell starts; `logs on` restores informational logs and `logs off` returns to errors only.

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
| `status` | Readable power, Wi-Fi, Zigbee, BLE presence, and PSU telemetry with units |
| `on`, `off`, `toggle`, `force-off` | Queue a power action; `power <action>` also works |
| `config` | List settings, including unsaved edits; passwords stay hidden |
| `set <setting> <value>` | Edit one setting in memory |
| `save` | Validate all edits, stage the configuration, and reboot |
| `discard` | Discard unsaved edits |
| `ble scan` | Start a 15-second discovery scan |
| `ble results` | Show discovered addresses, names, address types, and RSSI |
| `i2c scan <SDA> <SCL>` | Scan validated I²C pins; show hexadecimal and decimal addresses |
| `zigbee commission`, `zigbee reset` | Start joining or clear only Zigbee network state |
| `wifi ap` | Open the setup access point |
| `admin reset` | Generate, save, and display a new admin password once |
| `factory reset ERASE ALL` | Erase all NVS data, including settings and Zigbee state, then reboot |
| `reboot` | Restart and discard unsaved shell edits |
| `logs on`, `logs off` | Enable service logs or show only errors |
| `terminal plain`, `terminal ansi` | Switch between basic input and full line editing |

`config get` and `config show` are aliases for `config`. `config set <setting> <value>`, `config save`, and `config discard` are also supported. Command names and setting values are case sensitive. Leading/trailing spaces and repeated spaces between arguments are accepted. Each command is limited to 512 characters.

## Editing settings

Enter `config` to see setting names and values; Tab completes editable names after `set `. Quote values containing spaces with single or double quotes. Backslash escapes a character outside single quotes. Shell expansion is not performed, so `$` is literal. Boolean values accept `on`/`off`, `true`/`false`, `yes`/`no`, or `1`/`0`. Numbers are decimal, with explicit `0x` hexadecimal also accepted. Use `-1` to disable a GPIO.

```text
set radio hybrid
set wifi_ssid "My Wi-Fi"
set wifi_password "my network password"
set admin_password "my new admin password"
set ps_on.gpio 4
set ps_on.active_high on
set power_button.gpio 5
set power_sense.gpio 6
set status_led.gpio 8
set timing.strategy ps_on_then_button
set psu_i2c.sda_gpio 9
set psu_i2c.scl_gpio 10
set psu_i2c.address 0x5f
set psu_i2c.enabled on
config
save
```

These pin numbers are examples; choose pins for your board and wiring. `radio` is a short alias for `radio_profile`. The `pins.` prefix is optional for GPIO roles, so `set pins.ps_on.gpio 4` also works. Startup strategies are `ps_on_only`, `button_only`, `ps_on_then_button`, and `simultaneous`. An empty `wifi_password` clears the credential in the shell.

Buttons and BLE matchers use zero-based slots. Set `button_count` or `ble_device_count` to include the slots you want, then edit their fields:

```text
set button_count 1
set buttons.0.gpio 23
set buttons.0.short_action toggle
set buttons.0.enabled on
set ble_device_count 1
set ble_devices.0.label "My phone"
set ble_devices.0.type name_prefix
set ble_devices.0.value "My phone"
set ble_devices.0.min_rssi -80
set ble_devices.0.enabled on
```

Button actions are `none`, `on`, `off`, `toggle`, `force_off`, `config_ap`, `zigbee_commission`, and `zigbee_reset`. BLE matcher types are `address`, `name_exact`, `name_prefix`, `service_uuid`, and `manufacturer_data`. `config` shows fields for slots included by the current counts. Unused slots remain available through Tab completion.

Edits remain in memory until `save`; `bc250*>` marks unsaved changes. You can edit several related fields before validation, including swapping GPIO roles. `save` applies the existing configuration safety checks, stages the result, and reboots. Invalid settings leave edits available for correction. Saved changes follow the existing 30-second health check and rollback process. During first setup, set `configured on` after supplying the required pins and radio settings. `status`, scans, and power commands always use the running configuration until reboot.

`admin reset` takes effect immediately, updates both active and pending configuration, and refreshes the password hash in unsaved shell edits. Neither pending rollback nor a later shell save restores the old password. The setup AP stays open without a Wi-Fi password.

Treat physical serial access as administrator access. There is no serial login. The generated admin password appears once; keep terminal captures private. Entered passwords are echoed, so terminal recordings can contain them. The HTTP API continues to use JSON for local integrations; see [API.md](API.md).
