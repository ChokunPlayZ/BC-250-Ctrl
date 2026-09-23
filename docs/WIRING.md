# Wiring and installation

## Required parts

- ESP32-C5 or ESP32-C6 development board with adequate exposed GPIOs
- Three high-CTR phototransistor optocouplers for PS_ON, power-button, and power sensing
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

For the recommended active-high GPIO drive, add a roughly 10 kΩ pulldown from each output GPIO to ESP ground. This keeps both optocoupler LEDs off while the ESP32 is in reset, before firmware configures its pins. If an active-low driver circuit is used instead, bias its input to the electrically inactive high level. Verify the actual dev board's reset/boot behavior with a meter before connecting the BC-250.

## Motherboard power-button output

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

On the ESP side, connect the transistor collector to the configured GPIO with a pull-up to 3.3 V and the emitter to ESP ground. The common arrangement is therefore active-low. Firmware defaults to a 500 ms on filter and a 2 s off filter, allowing a blinking source LED to continue indicating powered state.

## Local buttons and status LED

Local momentary buttons may connect from GPIO to ESP ground with the internal pull-up enabled and active-low selected. Any signal entering from another powered system should be isolated.

Connect the optional status LED through a suitable current-limiting resistor. Both active-high and active-low wiring are supported. Patterns are:

| State | Pattern |
|---|---|
| Off | Dark |
| Starting | Fast blink |
| On | Solid |
| Stopping | Slow blink |
| Fault | Repeating triple flash |
| Configuration AP | Repeating double pulse |

## Bring-up order

1. Flash and boot with every GPIO role disabled.
2. Verify the setup AP and recovery sequence.
3. Connect and validate power sensing only.
4. Configure one output, test it against an optocoupler loopback fixture, then connect it to the BC-250.
5. Add the second output and test all selected sequences.
6. Only then enable BLE/Zigbee automation.
