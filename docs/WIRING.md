# Wiring and installation

This guide covers the electrical connections. Choose a [power method and GPIO assignments](CONFIGURATION.md) before wiring. On C5, GPIO 12 and 14 are blocked for every role; see [GPIO selection](CONFIGURATION.md#gpio-selection) and [recovery](RECOVERY.md) if an older configuration used them.

## Required parts

- A supported ESP32 board with enough exposed GPIOs and the correct [flash profile](BUILD.md#supported-profiles)
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

With **Keep PS_ON closed** enabled, normal shutdown and force-off still use the motherboard switch. PS_ON stays closed until the filtered power-sense input turns off, including if shutdown times out. Sense must therefore indicate the board's running state, not merely that the PSU has voltage, or the hold could keep itself on. Startup timeout without detected board power still releases PS_ON. ESP32 reset and recovery do not maintain the hold, so keep the board's existing hold path connected.

For **PS_ON latch (no power sense)**, PS_ON remains closed until an Off or Toggle command. This mode does not read power sense or use the motherboard switch output. Opening PS_ON removes power immediately without a graceful OS shutdown. See [power methods](CONFIGURATION.md#power-methods) for required GPIOs and control behavior.

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

The PSU PIC and identification EEPROM are optional, read-only I²C sources. Check bus voltage, level shifting, connector orientation, addresses, and fault diagnostics in the [PSU monitoring guide](PSU.md) before connecting them to ESP32 GPIOs.

## Bring-up order

1. Flash and boot with every GPIO role disabled.
2. Verify the setup AP and recovery sequence on Wi-Fi capable targets, or the serial shell on H targets.
3. For a sensed method, connect and validate power sensing first. Latch mode has no sense input.
4. Configure PS_ON or the motherboard switch output, test it against an optocoupler loopback fixture, then connect it to the BC-250.
5. Add any second output required by the selected method and test its sequence.
6. Only then enable BLE/Zigbee automation.
