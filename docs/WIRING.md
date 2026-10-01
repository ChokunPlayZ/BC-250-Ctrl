# Wiring and installation

On the NodeMCU ESP32-C5 Mini, do not assign GPIO 12 or 14: saved assignments have been reported to prevent booting. Both are blocked in every C5 firmware profile, including I²C scans. If settings already prevent startup, use the [USB/serial recovery tool](RECOVERY.md) and move the signals to other pins.

## Required parts

- A supported ESP32, S3, C3, C5, C6, C61, H2, H21, or H4 board with adequate exposed GPIOs and the correct flash profile. H boards use serial setup and native Zigbee because they have no Wi-Fi.
- Up to three high-CTR phototransistor optocouplers for PS_ON, power-button, and power sensing; PS_ON latch mode only needs the PS_ON output
- 430 Ω resistors for 3.3 V-driven optocoupler LEDs
- External roughly 10 kΩ inactive-state bias resistors for both output GPIOs
- Power-sense resistor selected for the measured LED voltage
- Reverse-protection diode across the power-sense optocoupler LED
- Optional LED and suitable series resistor
- Optional momentary switches
- Protected 5VSB-to-dev-board power connection, or a regulated 5VSB-to-3.3 V supply as required by the board

A PhotoMOS relay may replace the power-button phototransistor when a polarity-independent dry contact is required.

## Power and grounding

Power the ESP32 from the ATX purple `5VSB` rail so configuration and radio services remain available while the BC-250 is off. Confirm the dev board’s accepted input pin and voltage. The optocouplers provide signal isolation; follow the selected optocoupler’s pinout rather than assuming a package-standard orientation.

Do not connect USB power and ATX 5VSB simultaneously unless the board explicitly prevents back-feed. During development, disconnect 5VSB before attaching an unprotected USB supply, or use a proper power-OR circuit.

## PS_ON output

```text
ESP GPIO ── 430 Ω ──►| optocoupler LED ── ESP GND

ATX PS_ON# ── optocoupler collector
ATX GND    ── optocoupler emitter
```

Use a high-CTR optocoupler that can reliably pull `PS_ON#` low with the selected LED current. Connect the transistor in parallel with the BC-250’s existing isolated hold path. The configured GPIO is normally electrically inactive and is asserted only by the power state machine.

In the sensed startup methods, the ESP32 releases PS_ON after power is detected and the handoff delay expires (1 second by default). Enable **Keep PS_ON closed while board power is detected** under **Power wiring & timing** to keep the ESP32 optocoupler conducting as well. This also asserts PS_ON when the board starts externally or is already on when the power service starts. A PS_ON GPIO is required even with the motherboard-switch-only startup method. The setting defaults to off, including when upgrading older saved configurations.

Select **PS_ON latch (no power sense)** to close PS_ON on an On or Toggle command and keep it closed until an Off or another Toggle command. This mode does not read power sense or use the motherboard switch output; set those GPIOs to `-1` if unwired. Configure a controller-side physical button's short press as **Toggle** under Custom buttons, or use Zigbee On/Off. Opening PS_ON removes power immediately, so the operating system does not get a graceful shutdown. The controller starts with PS_ON open after reset or power loss and cannot detect power changes made outside the controller.

With this option enabled, normal shutdown and force-off still use the motherboard switch. PS_ON stays closed until the filtered power-sense input turns off, including if shutdown times out; it then opens after the configured sense-off filter. Sense must therefore indicate the board's running state, not merely that the PSU has voltage, or the hold could keep itself on. Startup timeout without detected board power still releases PS_ON. ESP32 reset and recovery do not maintain the hold, so keep the board's existing hold path connected.

For the recommended active-high GPIO drive, add a roughly 10 kΩ pulldown from each output GPIO to ESP ground. This keeps both optocoupler LEDs off while the ESP32 is in reset, before firmware configures its pins. If an active-low driver circuit is used instead, bias its input to the electrically inactive high level. Verify the actual dev board's reset/boot behavior with a meter before connecting the BC-250.

## Motherboard power-button output

Set **Motherboard power-switch output GPIO** in **Power wiring & timing** to the GPIO driving the second optocoupler. This is an output used to start, shut down, or force off the motherboard. Configure all physical pushbuttons separately under **Custom buttons**.

Place a second optocoupler transistor across the motherboard switch signal and its ground return. Identify the signal and return with a meter before wiring. If the header is not a ground-referenced active-low input, use an optically isolated PhotoMOS contact instead.

Never connect an ESP GPIO directly to the motherboard switch signal.

## Power-state input

Drive a third optocoupler LED from the BC-250 power-LED signal. Measure the actual voltage first and target approximately 2 mA:

| Measured signal | Starting resistor |
|---:|---:|
| 3.3 V | 1 kΩ |
| 5 V | 2.2 kΩ |
| 12 V | 5.6 kΩ |

Confirm the result against the optocoupler forward voltage, CTR, resistor power rating, and the BC-250 LED driver. Add reverse-voltage protection across the optocoupler LED.

As an alternative power-on sense point on the BC-250, use **TPMS1 pin 9 (+3 V/status)** as the signal and **pin 17 (GND)** as its return. Connect pin 9 through a **1 kΩ series resistor** to the optocoupler LED anode, and connect the LED cathode to pin 17. Do not connect the optocoupler LED directly across the header. Verify the pin voltage and that it changes with BC-250 power state before wiring; confirm the resistor against the optocoupler's forward voltage and LED current rating. The 1 kΩ value is a starting point for this nominal 3 V signal; check the resulting LED current for the selected optocoupler.

On the ESP side, connect the transistor collector to the configured GPIO with a pull-up to 3.3 V and the emitter to ESP ground. The common arrangement is therefore active-low. Firmware defaults to a 500 ms on filter and a 2 s off filter, allowing a blinking source LED to continue indicating powered state.

## Local buttons and status LED

These are optional controls on the ESP32 controller. The **status LED** below is a separate LED driven by the ESP32; it is not the BC-250 power LED used by the [power-state input](#power-state-input). The **local button** below is an ESP32 input; it is not the optocoupled [motherboard power-button output](#motherboard-power-button-output).

### Local momentary button

Connect each normally open, momentary pushbutton between its own configured ESP32 GPIO and **ESP ground**:

```text
ESP GPIO ── pushbutton ── ESP GND
```

Add every physical button, including power and auxiliary buttons, under **Custom buttons** in the web interface on Wi-Fi targets, or configure it through the serial shell on ESP32-H. Set each button's GPIO, leave **Active high** unchecked, and leave **Internal pull-up** checked. The firmware enables the GPIO's internal pull-up, so an unpressed button reads high and a press pulls it low. No external resistor is required for this local connection. If using a four-leg tactile switch, check which legs are internally joined so the GPIO and ground are on opposite sides of the switch. Assign short, double, and long press actions as desired; each button needs a different GPIO.

An active-high alternative is a switch from GPIO to **ESP 3.3 V**. For that circuit, check **active high** and uncheck **Internal pull-up** (the firmware enables an internal pull-down). Never apply 5 V to an ESP32 GPIO. Isolate any signal coming from another powered system.

### Controller status LED

For the default active-high setting, connect a discrete LED and a current-limiting resistor in series:

```text
ESP GPIO ── resistor ── LED anode (+) ──►|── LED cathode (−) ── ESP GND
```

In the web interface, set **Status LED GPIO** to this GPIO and leave **active high** checked. The LED lights when the GPIO is high. Select the resistor for the LED's forward voltage and the GPIO's allowed current; a 1 kΩ resistor is a low-current starting point for a typical red LED on 3.3 V. Do not connect an LED directly across a GPIO and ground.

If you need active-low wiring, connect `ESP 3.3 V ── resistor ── LED anode (+) ──►|── LED cathode (−) ── ESP GPIO` and uncheck **active high**. Use a GPIO separate from the buttons and other assigned functions. Leave **Status LED GPIO** at `-1` when no status LED is connected. Its patterns are:

| State | Pattern |
|---|---|
| Off | Dark |
| Starting | Fast blink |
| On | Solid |
| Stopping | Slow blink |
| Fault | Repeating triple flash |
| Zigbee joining | Two quick flashes every second until joining succeeds, fails, or stops |
| Configuration AP in Zigbee-only mode | Repeating pulse: one second on, one second off, until the AP closes or the controller reboots |
| Configuration AP in Wi-Fi mode | Repeating double pulse |

Configuration AP patterns take precedence over joining; the joining pattern takes precedence over power-state patterns. After joining ends, the normal power-state pattern returns. Joining automatically on a factory-new Zigbee network and joining through serial or a local button use the same indication.

## Optional HP Common Slot PSU I²C

Connect the PSU PIC's SDA and SCL to configured ESP32 SDA/SCL pins, and connect their signal grounds. The **ESP32 side must use 3.3 V logic**; never apply 5 V to ESP32 GPIOs. Verify the PSU/adapter's idle bus voltage before connecting: the [DPS-1200FB reverse-engineering notes](https://github.com/raplin/DPS-1200FB#connecting-i2c) describe weak pull-ups to 5 V on that model. Use an appropriate bidirectional I²C level shifter if the PSU side uses 5 V. Provide suitable external pull-ups on the 3.3 V side, typically 2.2–4.7 kΩ from each line to 3.3 V. Firmware enables weak internal pull-ups as a fallback; these do not replace proper pull-ups or voltage translation. These I²C connections are not optically isolated, so check grounding and the exact PSU connector pinout before wiring. Leave the feature disabled until the connections are verified.

The [DPS-1200FB Common Slot pinout measured by slundell](https://github.com/slundell/dps_charger#connection) lists connector contact 30 as signal ground, 31 as SCL, and 32 as SDA. For an ESP32 configured with SDA GPIO 1 and SCL GPIO 2, that means contact 32 to GPIO 1, contact 31 to GPIO 2, and contact 30 to ESP32 ground. The connector has contacts on both sides; verify numbering and orientation before applying power. This source documents a DPS-1200FB, so confirm the DPS-460EB connector electrically rather than assuming every model is identical.

The PIC's 7-bit address is usually `0x5F` when address pins A0–A2 are left high, or `0x58` when all three are low. Other combinations use `0x59`–`0x5E`. The EEPROM often has an address eight lower (`0x50`–`0x57`), but the observed DPS-460EB scan found `0x57` and `0x58`; address ACK alone does not establish which device is which. Some models answer only while the PSU is running. The firmware polls read-only registers and reports unavailable data if a transaction or reply checksum fails. It probes the EEPROM range and accepts checksum-verified FRU identification independently of telemetry, retrying once a minute. Manufacturer, product, part number, revision, serial/CT number, board part number, and rated capacity appear when the EEPROM provides valid fields. Unsupported or corrupt FRU data appears as an identification error; it does not suppress PIC telemetry. The firmware only changes the EEPROM read pointer and never writes EEPROM contents. There is no known I²C on/off command; switching an HP PSU's output requires a separate connection to its enable signal. The [reference sketch](https://github.com/ButtSimpleIdeas/DPS-1200-I2C/blob/master/dps1200_read_volts_fan/dps1200_read_volts_fan.ino) reports temperature in Fahrenheit; this firmware converts it to Celsius. The fan value is exposed as a raw reading because its RPM calibration has not been confirmed across models. The DPS-460EB's PIC protocol and EEPROM contents have not yet been verified on hardware.

On this PSU, telemetry is available only while the PSU is on (PS_ON bridged). An unavailable reading while it is off is expected.

The [DPS-1200/750 reverse-engineering project](https://github.com/ButtSimpleIdeas/DPS-1200-I2C/blob/master/Readme.md) found a proprietary PIC command format rather than standard PMBus; this is the telemetry format implemented here. The [older Common Slot article](http://colintd.blogspot.com/2016/10/hacking-hp-common-slot-power-supplies.html) calls contacts 31/32 PMBus when discussing the physical connector. [Linux also lists a Delta "DPS-460" PMBus device](https://github.com/torvalds/linux/blob/master/drivers/hwmon/pmbus/pmbus.c), but that name alone does not establish whether it covers a DPS-460EB. A protocol mismatch can prevent telemetry *after* a device responds at an I²C address; it cannot by itself explain SCL held low before the first scan probe. Confirm the bus and device address before attempting another command format.

### Diagnosing I²C timeouts

Run `i2c scan <SDA> <SCL>` in the serial shell, or **Scan I²C bus** in the web UI. For SDA GPIO 1 and SCL GPIO 2, use `i2c scan 1 2`. Both are valid chip GPIOs on C5/C6; C5's conservative pin list gives GPIO 2 an advisory rather than blocking it. Check the board's actual labels and ensure neither pin is assigned to another controller role. A complete scan tries all 112 addresses from `0x08` to `0x77`. Ordinary NACKs continue; one address-specific timeout also permits the sweep to continue if both lines return high. If SCL is already low, the scan stops before the first probe and reports 0 of 112 addresses. A held-low line during probing or the three-second time limit also stops it early, with the UI reporting how many addresses were tried and any devices found so far. `0x08` in an older timeout means the first probe failed; it is not evidence that the PSU uses that address. A compatible PSU may show a PIC at `0x58`–`0x5F` and a paired EEPROM at `0x50`–`0x57`; an EEPROM alone does not confirm the PIC is responding.

After enabling monitoring and saving/rebooting, `status` and the web PSU status display the last sampling error. The firmware allows 20 ms of clock stretching (subject to the chip driver's limit), keeps the register command and reply together under a bus lock, and clears/retries the entire pair once after a timeout. Failed samples are retried at the configured polling interval.

- `SDA ...=low` or `SCL ...=low` at failure: check signal ground, swapped or shorted wires, pull-ups, level shifting, and whether the PSU is powered. Measure both lines at idle; each should be high on the ESP32 side. Disconnect the PSU to help isolate which side is holding a line low.
- If the scan completes with no devices when the PSU is disconnected but reports SCL low when it is attached, measure GPIO SCL against ESP32 ground with the PSU connected but no scan running. Check the shared ground and connector contact, then check idle voltage on both sides of any level shifter. A clock line held low cannot be fixed by scanning more addresses; do not connect the ESP32 until the PSU-side voltage is known to be safe for its GPIOs.
- Both lines high with a timeout: check the actual header pins, pull-up strength, wiring length/noise, and PSU compatibility; a single GPIO snapshot cannot prove correct timing.
- `ESP_ERR_INVALID_STATE` on a register write: in ESP-IDF 5.5.4 this can mean the transaction ended before completion, including a NACK during the command. A scan ACK at the same address proves only that the device answered its address. Check the register protocol and signal integrity; capture SDA/SCL with a logic analyzer to distinguish a data-byte NACK from a timing fault.
- `ESP_ERR_INVALID_RESPONSE`: the device did not acknowledge a transfer. Check the selected PIC address and PSU power.
- `reply checksum failed`: communication completed but the reply was invalid. Check signal quality and whether the PSU implements this protocol.
- `I2C bus busy`: another scan or client held the shared bus for too long. Retry; this message does not diagnose the electrical wiring.

On ESP-IDF 5.5.4/C5, a timeout can also produce `i2c.common: GPIO 1 is not usable, maybe conflict with others` for both SDA/SCL. The [driver's bus-clear path](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_i2c/i2c_master.c) configures the pins again, and [pin configuration](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_i2c/i2c_common.c) warns about its own existing GPIO reservations. Warnings during recovery therefore do not establish that those pins are forbidden. If they appear at initial bus creation before any failed transfer, investigate an actual peripheral conflict instead.

## Bring-up order

1. Flash and boot with every GPIO role disabled.
2. Verify the setup AP and recovery sequence.
3. Connect and validate power sensing only.
4. Configure one output, test it against an optocoupler loopback fixture, then connect it to the BC-250.
5. Add the second output and test all selected sequences.
6. Only then enable BLE/Zigbee automation.
