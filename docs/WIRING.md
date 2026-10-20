# Wiring and installation

This guide covers the electrical connections. Choose a [power method and GPIO assignments](CONFIGURATION.md) before wiring. On C5, GPIO 12 and 14 are blocked for every role; see [GPIO selection](CONFIGURATION.md#gpio-selection) and [recovery](RECOVERY.md) if an older configuration used them.

## Required parts

- A supported ESP32 board with enough exposed GPIOs and the correct [flash profile](BUILD.md#supported-profiles)
- Output interfaces: high-CTR phototransistor optocouplers, PhotoMOS contacts, or [normally open relay contacts](#relay-outputs) for PS_ON and the motherboard switch; PS_ON latch mode only needs PS_ON
- Power sensing for sensed methods: an optocoupler or a [verified 3.3 V GPIO connection](#direct-33-v-power-sense)
- 430 Ω resistors for any 3.3 V-driven output optocoupler LEDs
- External roughly 10 kΩ inactive-state bias resistors for both output GPIOs
- For optocoupled sensing, a resistor selected for the measured signal voltage and a reverse-protection diode across the optocoupler LED
- Optional LED and suitable series resistor
- Optional momentary switches
- Protected 5VSB-to-dev-board power connection, or a regulated 5VSB-to-3.3 V supply as required by the board

A PhotoMOS or mechanical relay may replace the power-button phototransistor when a polarity-independent dry contact is required. An integrated ESP32 two-channel relay board already contains the relay drivers; see the [board example](#common-esp32-two-channel-relay-board).

## Power and grounding

Power the ESP32 from the ATX purple `5VSB` rail so configuration and radio services remain available while the BC-250 is off. Confirm the board's accepted input pin and voltage. On a relay board, use the documented 5 V input and ensure 5VSB can supply the ESP32 and both energized relay coils. A main switched PSU rail would turn the controller off with the BC-250.

Optocouplers and dry relay contacts isolate their signal paths. Direct GPIO sensing requires **ESP ground and BC-250 ground to be connected**; a shared non-isolated 5VSB supply also joins the grounds. Do not join the BC-250 status/3 V rail to the ESP32's 3.3 V supply pin. Follow each component's pinout rather than assuming a package or terminal order.

Do not connect USB power and ATX 5VSB simultaneously unless the board explicitly prevents back-feed. During development, disconnect 5VSB before attaching an unprotected USB supply, or use a proper power-OR circuit.

## PS_ON output

```text
ESP GPIO ── 430 Ω ──►| optocoupler LED ── ESP GND

ATX PS_ON# ── optocoupler collector
ATX GND    ── optocoupler emitter
```

Use a high-CTR optocoupler that can reliably pull `PS_ON#` low with the selected LED current. If an independent hold path is already installed, connect the transistor in parallel with it. The configured GPIO is normally electrically inactive and is asserted only by the power state machine.

In the sensed startup methods, the ESP32 releases PS_ON after power is detected and the handoff delay expires (1 second by default). Enable **Keep PS_ON closed while board power is detected** under **Power wiring & timing** to keep the ESP32 optocoupler conducting or its PS_ON relay energized. This also asserts PS_ON when the board starts externally or is already on when the power service starts. A PS_ON GPIO is required even with the motherboard-switch-only startup method. The setting defaults to off, including when upgrading older saved configurations. **Enable it when this controller is the PSU's only PS_ON hold path**, or the PSU will turn off after handoff.

With **Keep PS_ON closed** enabled, normal shutdown and force-off still use the motherboard switch. PS_ON stays closed until the filtered power-sense input turns off, including if shutdown times out. Sense must therefore indicate the board's running state, not merely that the PSU has voltage, or the hold could keep itself on. Startup timeout without detected board power still releases PS_ON. ESP32 reset and recovery do not maintain the hold. Preserve any independent hold path already installed; without one, resetting the ESP32 interrupts PSU power.

For **PS_ON latch (no power sense)**, PS_ON remains closed until an Off or Toggle command. This mode does not read power sense or use the motherboard switch output. Opening PS_ON removes power immediately without a graceful OS shutdown. See [power methods](CONFIGURATION.md#power-methods) for required GPIOs and control behavior.

For the recommended active-high GPIO drive, add a roughly 10 kΩ pulldown from each output GPIO to ESP ground. This keeps both optocoupler LEDs off while the ESP32 is in reset, before firmware configures its pins. If an active-low driver circuit is used instead, bias its input to the electrically inactive high level. Verify the actual dev board's reset/boot behavior with a meter before connecting the BC-250.

## Motherboard power-button output

Set **Motherboard power-switch output GPIO** in **Power wiring & timing** to the GPIO driving the second optocoupler or relay. This is an output used to start, shut down, or force off the motherboard. Configure all physical pushbuttons separately under **Custom buttons**.

Place a second optocoupler transistor across the motherboard switch signal and its ground return. Identify the signal and return with a meter before wiring. If the header is not a ground-referenced active-low input, use a PhotoMOS or dry relay contact instead. A mechanical relay connects across the same two switch points as described below.

Never connect an ESP GPIO directly to the motherboard switch signal.

## Relay outputs

A relay replaces an output optocoupler without changing the power sequence. Use **COM** and **NO** (normally open), leaving **NC** unused, so an unpowered relay leaves the signal open:

```text
Relay 1 COM ── ATX PS_ON# (green)
Relay 1 NO  ── ATX GND (black)

Relay 2 COM ── BC-250 power-switch signal
Relay 2 NO  ── BC-250 power-switch return
```

The contacts only short the two connected points; do not connect an ESP supply rail to either contact pair. Use separate contact pairs for PS_ON and the motherboard switch. Determine COM/NO from the board labels and a continuity test, not the terminals' physical order. Only low-voltage signals belong on these contacts in this project.

Connect a GPIO to a **3.3 V-compatible relay driver input**, never directly to a bare coil. A separate relay module needs its specified coil supply, transistor driver, and flyback protection; check its input voltage and grounding requirements. An integrated ESP32 relay board already supplies those drivers.

Set each output's **active high** checkbox to match the driver: checked if GPIO high closes COM–NO, unchecked if GPIO low closes it. Add or verify an external roughly 10 kΩ inactive-state bias on each driver input: pulldown for active-high, pull-up to **ESP 3.3 V** for active-low. Firmware cannot prevent a floating input from energizing a relay before it boots. With contact wires disconnected, confirm both contacts stay open through power-up, reset, flashing, and recovery, then verify the commanded pulse and hold with a meter.

### Common ESP32 two-channel relay board

The common ESP32-WROOM two-channel boards sold as **ESP32_Relay_X2 / LC Technology** commonly put relay 1 on **GPIO 16** and relay 2 on **GPIO 17**. The [BC-250 remote-control project's pin map](https://github.com/PetteriLah/BC-250-PC-Remote-Control/blob/main/pins.h) uses 16/17 with GPIO 4 for TPMS1 sensing. Treat this as a wiring example: confirm the pin map and relay polarity on your exact revision. Active-low variants are documented by the [PS-250 controller project](https://github.com/leafyjerk/PS-250-Remote-Power-Controller#hardware-notes-important).

Use the low-voltage DC version, powered from 5VSB through its documented 5 V input. This guide does not cover boards with an AC mains supply. For a classic ESP32 with verified 4 MB flash, select **`esp32_4mb`**; verify the chip and capacity before flashing. These boards use Wi-Fi/BLE and have no native Zigbee. Boards without USB need a **3.3 V logic** UART adapter on TX0/RX0/GND; follow the board's IO0/EN download procedure and the USB back-feed precautions above.

Wire the relay contacts as above, and TPMS1 as in [direct sensing](#direct-33-v-power-sense). Under **Power wiring & timing**, set:

| Setting | Example value |
|---|---|
| PS_ON GPIO | `16` (relay 1) |
| Motherboard power-switch output GPIO | `17` (relay 2) |
| PS_ON / motherboard switch output active high | Both unchecked for a verified active-low board; checked for active-high drivers |
| Power sense GPIO | `4`, connected to TPMS1 pin 9 |
| Power sense active high | Checked |
| Power sense internal pull-up | Unchecked; this enables the internal pulldown |
| Power method | **PS_ON, then motherboard switch** |
| Keep PS_ON closed while board power is detected | Checked when relay 1 is the only PS_ON hold path |
| Controller status LED GPIO | `-1` unless a separate compatible LED is verified |

For an active-low variant, verify or add GPIO 16 → 3.3 V and GPIO 17 → 3.3 V pull-ups (roughly 10 kΩ each). Active-high variants need pulldowns instead. Set these polarities before attaching the contact wires. All roles remain disabled by default; choosing an ESP32 build does not assign relay pins automatically.

An optional local switch from GPIO 22 to ESP ground can be assigned under **Custom buttons** with Active high off, Internal pull-up on, and short press **Toggle**, if GPIO 22 is free on your board.

## Power-state input

**An optocoupler is recommended for power sensing**, because it isolates the sense signal from the ESP32 input. Feeding the TPMS1 3.3 V status signal directly into an ESP32 GPIO is also usable when the voltage levels are verified and the grounds are shared. Both options feed the same filtered power-state input; no different power method is needed.

### Optocoupled power sense (recommended)

Drive a third optocoupler LED from the BC-250 power-LED signal. Measure the actual voltage first and target approximately 2 mA:

| Measured signal | Starting resistor |
|---:|---:|
| 3.3 V | 1 kΩ |
| 5 V | 2.2 kΩ |
| 12 V | 5.6 kΩ |

Confirm the result against the optocoupler forward voltage, CTR, resistor power rating, and the BC-250 LED driver. Add reverse-voltage protection across the optocoupler LED.

As an alternative power-on sense point on the BC-250, use **TPMS1 pin 9 (+3 V/status)** as the signal and **pin 17 (GND)** as its return. Connect pin 9 through a **1 kΩ series resistor** to the optocoupler LED anode, and connect the LED cathode to pin 17. Do not connect the optocoupler LED directly across the header. Verify the pin voltage and that it changes with BC-250 power state before wiring; confirm the resistor against the optocoupler's forward voltage and LED current rating. The 1 kΩ value is a starting point for this nominal 3 V signal; check the resulting LED current for the selected optocoupler.

On the ESP side, connect the transistor collector to the configured GPIO with a pull-up to 3.3 V and the emitter to ESP ground. Set **Power sense active high** off and **Power sense internal pull-up** on (the defaults). Firmware defaults to a 500 ms on filter and a 2 s off filter, allowing a blinking source LED to continue indicating powered state.

### Direct 3.3 V power sense

For a shared-ground installation, the BC-250 **TPMS1 pin 9 (+3 V/status)** can drive an ESP32 input directly, without a sense optocoupler. Verify header orientation and measure it relative to **TPMS1 pin 17 (GND)** while the board is off and running. Use pin 9 only after confirming a valid logic high when running and low when off; **pin 15 is 3VSB and is unsuitable for running-state detection**. The [original project's TPMS1 pin map](https://github.com/PetteriLah/BC-250-PC-Remote-Control/blob/main/pins.h) identifies these pins.

```text
BC-250 TPMS1 pin 9 (+3 V/status) ── 1 kΩ series ── ESP sense GPIO (e.g. GPIO 4)
BC-250 TPMS1 pin 17 (GND)        ───────────────── ESP GND
```

The series resistor provides limited fault-current protection; **it does not convert voltage**. Only use a signal within the selected ESP32's GPIO voltage limits and with valid input-high/low levels. For classic ESP32, the input-high minimum is 0.75 × VDD (about 2.48 V at 3.3 V); check the [Espressif module datasheet](https://documentation.espressif.com/esp32_wroom-32_datasheet_en.pdf). Never connect 5 V, 12 V, PS_ON, or an unverified power-LED drive directly to a GPIO. Use an optocoupler or a suitable level interface when the source is incompatible. If BC-250 power can remain on while ESP power is removed, use an interface with power-off isolation to prevent back-powering the ESP through its input.

Set **Power sense active high** on and **Power sense internal pull-up** off. Off selects the ESP's internal **pulldown**, so a disconnected sense wire reads off. Leaving the default pull-up on can falsely report power when the board is off. This firmware requires a pull-capable input; classic ESP32 GPIO 34–39 remain unsupported. The default sense filters apply to direct sensing too.

For serial configuration (substitute your verified GPIO), use:

```text
set power_sense.gpio 4
set power_sense.active_high on
set power_sense.pull_up off
```

Complete the other required settings, then `save` and check `status` with the BC-250 off and running before enabling automation. Existing API clients can set `pins.power_sense.active_high` to `true` and `pins.power_sense.pull_up` to `false`.

## Local buttons and status LED

These are optional controls on the ESP32 controller. The **status LED** below is a separate LED driven by the ESP32; it is not the BC-250 power LED used by the [power-state input](#power-state-input). The **local button** below is an ESP32 input; it is separate from the [motherboard power-button output](#motherboard-power-button-output).

### Local momentary button

Connect each normally open, momentary pushbutton between its own configured ESP32 GPIO and **ESP ground**:

```text
ESP GPIO ── pushbutton ── ESP GND
```

Add every physical button, including power and auxiliary buttons, under **Custom buttons** in the web interface on Wi-Fi targets, or configure it through the serial shell on ESP32-H. Set each button's GPIO, leave **Active high** unchecked, and leave **Internal pull-up** checked. The firmware enables the GPIO's internal pull-up, so an unpressed button reads high and a press pulls it low. No external resistor is required for this local connection. If using a four-leg tactile switch, check which legs are internally joined so the GPIO and ground are on opposite sides of the switch. Assign short, double, and long press actions as desired; each button needs a different GPIO.

An active-high alternative is a switch from GPIO to **ESP 3.3 V**. For that circuit, check **active high** and uncheck **Internal pull-up** (the firmware enables an internal pull-down). Never apply 5 V to an ESP32 GPIO. External signals require verified voltage levels and a shared ground as in [direct sensing](#direct-33-v-power-sense), or isolation.

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
4. Configure PS_ON or the motherboard switch output and its polarity. Test optocoupler conduction or relay COM–NO continuity, including through reset, before connecting it to the BC-250. Enable PS_ON hold if this output is the only hold path.
5. Add any second output required by the selected method and test its sequence.
6. Only then enable BLE/Zigbee automation.
