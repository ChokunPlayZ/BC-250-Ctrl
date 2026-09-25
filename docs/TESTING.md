# Verification checklist

## Automated checks

- Run native state-machine and BLE matcher tests.
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
