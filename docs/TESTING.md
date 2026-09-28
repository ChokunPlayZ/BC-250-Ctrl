# Verification checklist

## Automated checks

- Run native state-machine, BLE matcher, shell parsing, and typed settings tests.
- Build C5/C6 in both 4 MB and 8 MB layouts.
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
- Reject duplicate, reserved, USB, flash, and strapping pins without advanced override.
- Exercise short, double, and long press actions on every configured button.
- Save a valid pending configuration and verify promotion after 30 seconds.
- Force repeated boot failure and verify rollback to the previous active configuration and recovery AP.
- Corrupt the NVS blob and verify safe defaults with all external roles disabled.
- Verify triple-reset recovery without configured buttons.
- Verify factory reset separately from Zigbee network reset.
- Exercise serial status/configuration queries, power commands, BLE/I²C scans, Zigbee commands, and the setup AP command in each radio profile.
- Reset the admin password over serial during an active pending configuration, verify immediate web login with the new password, then force rollback and verify the new password still works.
- Verify serial oversized commands are rejected and the next command still succeeds.
- Verify the `bc250>` prompt, echo, cursor editing, backspace/delete, Up/Down history, Tab completion, and Ctrl+C cancellation in an ANSI terminal; verify echo, backspace, and cancellation in a basic terminal.
- Verify `status`, `config`, BLE results, I²C results, errors, and help use readable text without JSON. Confirm `on`, `off`, `toggle`, and `force-off` aliases work.
- Edit several related settings with `set`, including quoted SSIDs/passwords and swapping GPIO roles. Verify `bc250*>`, invalid-save correction, `discard`, and persistence only after `save` and reboot.
- Verify passwords stay hidden in `config`, and newly entered `set` commands are not added to history. Reset the admin password while shell edits exist and confirm a later save preserves the new password.
- On both C5 flash layouts, connect through the chip's native USB Serial/JTAG port and verify `help`, `status`, `config get`, and `admin reset` accept input after boot and after reconnecting USB.

## Radio interoperability

- Confirm Wi-Fi-only, Zigbee-only, and hybrid profiles.
- In hybrid mode, sustain web traffic, BLE scanning, and Zigbee commands concurrently.
- Learn each BLE matcher form. An absent-to-present transition must request power within two seconds of receiving an advertisement; disappearance must never request shutdown.
- Confirm rotating-private-address devices produce an unreliable-match warning in deployment documentation or are matched by stable advertisement content.
- Join both Home Assistant ZHA and Zigbee2MQTT as a router.
- Verify On, Off, and Toggle control and ensure the reported On/Off attribute follows the optocoupled power sense, not the last command.
- Exercise commissioning, rejoin, leave/reset, coordinator restart, and router recovery.

## OTA (8 MB only)

- Upload a valid application image and preserve controller configuration and Zigbee network state.
- Interrupt an upload and confirm the running slot remains bootable.
- Reject corrupt or wrong-target images.
- Boot a deliberately unhealthy image and confirm rollback.
