# Verification checklist

## Automated checks

- Run native state-machine, BLE matcher, shell parsing, typed settings, GPIO advisory/exclusion, recovery erase failure, Wi-Fi mode/client inactivity tests, Zigbee pause/resume lifecycle tests, and status LED pattern tests.
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
- Reset the admin password over serial during an active pending configuration, verify immediate web login with the new password, then force rollback and verify the new password still works.
- Verify serial oversized commands are rejected and the next command still succeeds.
- Verify the `bc250>` prompt, echo, cursor editing, backspace/delete, Up/Down history, Tab completion, and Ctrl+C cancellation in an ANSI terminal; verify echo, backspace, and cancellation in a basic terminal.
- Verify `status`, `config`, BLE results, I²C results, errors, and help use readable text without JSON. Confirm `on`, `off`, `toggle`, and `force-off` aliases work.
- Edit several related settings with `set`, including quoted SSIDs/passwords and swapping GPIO roles. Verify `bc250*>`, invalid-save correction, `discard`, and persistence only after `save` and reboot.
- Verify passwords stay hidden in `config`, and newly entered `set` commands are not added to history. Reset the admin password while shell edits exist and confirm a later save preserves the new password.
- On both C5 flash layouts, connect through the chip's native USB Serial/JTAG port and verify `help`, `status`, `config get`, and `admin reset` accept input after boot and after reconnecting USB.

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
