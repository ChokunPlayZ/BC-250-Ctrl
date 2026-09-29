# Verification checklist

## Automated checks

- Run native state-machine, BLE matcher, shell parsing, typed settings, GPIO advisory/exclusion, recovery erase failure, Wi-Fi mode/client inactivity tests, Zigbee pause/resume lifecycle tests, status LED pattern tests, and I²C scan/PSU transfer tests (recovery, partial scan and timeout budget, checksum/NACK failure, EEPROM identity and capacity parsing, GPIO diagnostics, busy bus, and concurrent scan/startup).
- Build C5/C6 in both 4 MB and 8 MB layouts, and both standalone C5 recovery profiles.
- Confirm each application fits its selected partition.
- Review compiler warnings and check that generated configuration files are not committed.

## Loopback fixture

Use optocoupler outputs to drive isolated sense inputs rather than connecting outputs directly to GPIOs.

- Confirm both outputs remain inactive during reset, bootloader operation, firmware startup, and recovery AP mode.
- Exercise PS_ON only, button only, delayed PS_ON then button, and zero-delay simultaneous start.
- Verify button pulse, sensed-on handoff, start timeout, 60-second retry cooldown, graceful shutdown, force-off hold, and shutdown timeout.
- Simulate missing, blinking, and stuck power-LED inputs.
- Brown out and watchdog-reset the ESP32 at every sequence phase; no restart may produce an unintended pulse.

## Serial event logging

- Capture first-boot, saved, pending, rolled-back, and invalid-configuration startup. Check mode/profile, configuration source, controller counts and slots (including disabled and unnamed slots), GPIOs, button actions, radio and PSU settings. Wi-Fi passwords and admin credentials must not appear in the new summary; the existing one-time first-boot admin password remains separate.
- Verify informational logs continue after the shell starts. `logs off` must suppress routine service events, `logs on` must restore them, and reboot must enable them again. Check that commands remain usable while events are logged.
- Detect a saved BLE controller with power off, already on, already starting, during shutdown, and during retry cooldown. Check arrival/address/RSSI, queued/skipped decisions, actual sequence trigger or rejection, and absence/rearming. Continuous advertisements must not repeat arrival logs or retrigger power.
- Exercise each button's press/release and short/double/long gestures, including an action of `none`. Verify slot/GPIO, assigned action, actual output pulses, sense transitions, sequence completion/timeouts, and queue-failure messages without changing power timing or gesture behavior.
- Disconnect/reconnect Wi-Fi, join/leave the setup AP, pause/resume Zigbee, commission/rejoin/leave/reset Zigbee, and send power commands over serial, HTTP, and Zigbee. Verify source, result, and error logs. Check configuration save/promotion, PSU availability/restoration, and OTA upload results without logging every telemetry sample.

## Configuration and recovery

- Boot with no controller settings in NVS and verify provisioning reaches the setup AP without a stack protection fault; reboot and verify the saved settings load.
- Join the setup and recovery APs without a password, including after upgrading a device with an older saved provisioning password; verify the portal opens at `http://192.168.4.1/`.
- Verify configuration saving and promotion after 30 seconds stay within the serial, HTTP, and health-check task stacks, including with all BLE matcher slots populated.
- Trigger a controlled panic in a test image and verify the core dump saves successfully using its reserved stack.
- Reject duplicate, out-of-range, and missing required pins. On C5, reject GPIO 12/14 for every active role and I²C scans through the web UI, API and serial shell, including with the legacy override enabled. On C6, verify these pins retain advisory behavior. Other GPIOs outside the conservative guidance display warnings and save without an override.
- Edit web inputs without leaving the field: verify blank/fractional/out-of-range numbers, missing required GPIOs or SSIDs, duplicate roles, short admin passwords, and dependent timing errors appear immediately and clear when corrected. GPIO advisories must leave Save enabled when all other fields are valid.
- Exercise short, double, and long press actions on every configured button.
- Save a valid pending configuration and verify promotion after 30 seconds.
- Force repeated boot failure and verify rollback to the previous active configuration and recovery AP.
- Corrupt the NVS blob and verify safe defaults with all external roles disabled.
- Verify triple-reset recovery without configured buttons.
- Verify factory reset separately from Zigbee network reset.
- Load an older active/pending C5 configuration using GPIO 12/14, then flash updated normal firmware without erasing NVS; verify no GPIO services start and the recovery AP and serial shell remain accessible.
- On each C5 flash layout, flash the full recovery image at 0x0 over USB while active/pending settings and Zigbee state exist. Verify `RECOVERY COMPLETE`, no external GPIO activity, and no AP. Reboot recovery and verify the erase safely repeats. Reflash normal firmware and verify first setup, a fresh admin password, all external GPIOs disabled, and a factory-new Zigbee network. Exercise the 8 MB case with the previous application selected in ota_1; recovery must run from ota_0.
- Exercise serial status/configuration queries, power commands, BLE/I²C scans, and Zigbee commands in each radio profile. Run `wifi ap` from Wi-Fi station, Zigbee-only, unconfigured, and recovery AP states; verify the first-setup SSID, open authentication, captive DNS, and portal at `http://192.168.4.1/`, including after repeated commands. Leave each AP without connected clients for five minutes and verify it closes. Keep a client connected for more than 15 minutes and verify it remains open; disconnect the last client and verify a fresh five-minute grace period. Repeat with multiple clients, reconnect just before expiry, and verify Wi-Fi station restoration and Zigbee-only Wi-Fi shutdown.
- In Zigbee-only mode with a configured status LED, open the AP and verify a repeating one-second-on/one-second-off pulse, even while power is on or faulted. Close the AP manually, let it time out, or reboot and verify the normal power-state LED pattern returns.
- Test the web UI at 320, 390, and 430 px phone widths and desktop width: no horizontal scrolling, visible field labels, large touch controls, collapsible settings, and an accessible fixed Save and reboot bar. Verify changing profiles shows the matching radio fields; hidden-section errors show a count and Show inputs to fix opens/focuses the first invalid field. Add/edit/remove physical buttons and BLE controllers, exercise I²C/BLE scans, and verify the saved request preserves values. Confirm Force off asks before executing, setup AP closure acknowledges before disconnecting, and unavailable factory reset/OTA controls follow device status.
- In a captive portal browser that suppresses JavaScript dialogs, press Turn off setup AP and verify the confirmation appears inside the page. Keep AP on must cancel without a request. Turn off and disconnect must send one request, disable both confirmation buttons while waiting, and show the closing acknowledgement. Simulate an HTTP error and verify the message appears, the buttons become available for retry, and status updates continue.
- Reset the admin password over serial during an active pending configuration, verify immediate web login with the new password, then force rollback and verify the new password still works.
- Verify serial oversized commands are rejected and the next command still succeeds.
- Verify the `bc250>` prompt, echo, cursor editing, backspace/delete, Up/Down history, Tab completion, and Ctrl+C cancellation in an ANSI terminal; verify echo, backspace, and cancellation in a basic terminal.
- Verify `status`, `config`, BLE results, I²C results, errors, and help use readable text without JSON. Confirm `on`, `off`, `toggle`, and `force-off` aliases work.
- Edit several related settings with `set`, including quoted SSIDs/passwords and swapping GPIO roles. Verify `bc250*>`, invalid-save correction, `discard`, and persistence only after `save` and reboot.
- Verify passwords stay hidden in `config`, and newly entered `set` commands are not added to history. Reset the admin password while shell edits exist and confirm a later save preserves the new password.
- On both C5 flash layouts, connect through the chip's native USB Serial/JTAG port and verify `help`, `status`, `config get`, and `admin reset` accept input after boot and after reconnecting USB.

## I²C hardware checks

- On each chip, scan GPIO 1/2 with a known compatible PSU and verify PIC and EEPROM addresses. Repeat with a different valid pin pair while monitoring is disabled; temporary scans must release the bus. While monitoring runs, reject scans on different pins and verify scans on active pins do not interrupt command/reply pairs.
- Verify a complete scan reports 112 attempted addresses. With SCL held low before scanning, it should report 0 of 112 addresses, avoid probing or bus reset, and not claim no devices exist. If SCL falls during a scan, preserve any earlier devices and stop early. An isolated timeout with both lines idle must permit later addresses to be scanned. Check the web and serial progress/error messages.
- Confirm idle SDA/SCL voltage and external pull-ups on the ESP32 side. Disconnect the PSU and confirm scans complete without found devices. Test missing external pull-ups separately; internal pull-ups are only a fallback.
- With a PSU whose EEPROM contains valid FRU data, check the web and serial identity fields against its label. Confirm EEPROM absence, corrupt FRU checksums, and unsupported text encoding do not suppress otherwise valid PIC telemetry. Verify no EEPROM data-write transaction is sent.
- Interrupt a transfer or hold a line low through a suitable test fixture. Verify a bounded bus-clear/retry, a timeout error naming the address/register/phase and pin levels, continued power/serial/web operation, and automatic telemetry restoration after releasing the fault. On C5, distinguish recovery's GPIO reservation warnings from startup conflicts.
- Select an absent PIC address and inject a corrupt reply. Verify NACK/checksum errors remain unavailable in API and Zigbee, with the error visible through serial and web status; a complete valid sample must clear it.

## Radio interoperability

- Confirm Wi-Fi-only and Zigbee-only profiles. Verify that the web UI and serial shell offer only these profiles and that API/serial attempts to select `hybrid` are rejected. Load older active and pending combined-mode settings and verify migration to Zigbee-only without losing other settings or network pairing.
- After configuring Zigbee and rebooting, run `wifi ap`, join from a phone and computer, and verify DHCP, captive DNS, and portal access. Repeat while joining a Zigbee network and after pairing. Verify the Zigbee stack is fully stopped before the AP starts, remains paused for the AP session, and resumes with the same pairing after manual closure and idle expiry. Repeat rapid close/reopen cycles and triple-reset recovery; configuration and Zigbee commands must remain available over serial after the AP closes.
- Learn each BLE matcher form. An absent-to-present transition must request power within two seconds of receiving an advertisement; disappearance must never request shutdown.
- Confirm rotating-private-address devices produce an unreliable-match warning in deployment documentation or are matched by stable advertisement content.
- Join both Home Assistant ZHA and Zigbee2MQTT as a router.
- Verify On, Off, and Toggle control and ensure the reported On/Off attribute follows the optocoupled power sense, not the last command.
- Exercise commissioning, rejoin, leave/reset, coordinator restart, and router recovery.
- With a configured status LED, verify factory-new automatic joining, serial `zigbee commission`, and a commissioning button all flash twice every second and print the joining message with `logs off`. Run `status` during the attempt and verify it shows `joining`. Test success, no coordinator accepting joins, repeated commissioning requests, failure to start steering, leave/reset during joining, and opening the AP during joining; each completed or stopped attempt must clear the joining indication and restore the applicable power-state or AP pattern. Verify success/failure messages and retry guidance in the shell.

## OTA (8 MB only)

- Upload a valid application image and preserve controller configuration and Zigbee network state.
- Interrupt an upload and confirm the running slot remains bootable.
- Reject corrupt or wrong-target images.
- Boot a deliberately unhealthy image and confirm rollback.
