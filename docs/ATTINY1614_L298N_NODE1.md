# ATtiny1614 Node 1 with L298N and AS5600

This node uses an ATtiny1614 at 5 V with the Uno D5 half-duplex bus, an L298N
dual H-bridge, an AS5600 I2C encoder, and the existing active-low LED on
physical pin 3. Its firmware supports the Uno's `MODE`/`SELECT` flashing
protocol, `VALVE`, timed `MOTOR`, encoder commands, and local encoder-counted
`MOVE` operations.

> **Validation status:** the firmware and test remain experimental. On
> 2026-11-16, the nine-position cycle completed with the free-shaft motor,
> AS5600, and valve; all station errors were at most 1 encoder count, and the
> outputs were verified OFF at the end. This does not validate a coupled or
> loaded mechanism, sustained operation, or electrical isolation of UPDI. The
> test intentionally actuates real hardware; satisfy every safety precondition
> below before running it. It is not part of the passing `BlinkNode1` baseline.

## ATtiny1614 to L298N signal wiring

Viewed from above with the package notch at the top:

```text
                 notch
           +-------------+
     VDD  1|             |14 GND
     PA4  2|             |13 PA3
     PA5  3|             |12 PA2
     PA6  4|             |11 PA1
     PA7  5|             |10 PA0 / UPDI
     PB3  6|             | 9 PB0
     PB2  7|             | 8 PB1
           +-------------+
```

| ATtiny1614 signal | Physical pin | L298N input |
|---|---:|---|
| PA6 | 4 | IN1, motor channel A |
| PA7 | 5 | IN2, motor channel A |
| PA4 | 2 | IN3, valve channel B |
| PB1 | 8 | IN4, valve channel B |

Keep the existing status LED on PA5, physical pin 3. Keep the L298N ENA and ENB
jumpers fitted as requested; do not connect those enable pins to the ATtiny.
With the enable jumpers fitted, the channels are always enabled, so add a 10 kΩ
pulldown from **each** IN1–IN4 input to L298N GND unless the exact module
schematic confirms equivalent pulldowns. These keep the bridge inputs low while
the ATtiny is reset or being programmed.

Connect L298N OUT1/OUT2 to the motor and OUT3/OUT4 to the valve coil. `FORWARD`
is defined by the motor lead polarity on OUT1/OUT2; swap those two motor leads
if the physical direction is opposite. The firmware limits each timed `MOTOR`
command to 2 seconds and automatically turns the valve output off after
5 seconds. `MOVE FORWARD|REVERSE <counts>` instead samples the encoder on the
ATtiny and stops at the requested relative count distance. A position move is
limited to 512 counts and stops on encoder loss, a 3-second encoder stall, or
a 15-second move timeout, reporting `FAULT` on an abort. With ENA/ENB jumpers
fitted, motion is at full supply voltage; encoder feedback cannot guarantee
precise stopping if the motor is too fast for the firmware's sample/stop
latency. The encoder is sampled locally every 5 ms, matching the sensor-only
firmware's known-good sampling cadence.

## Power and bus wiring

- ATtiny VDD, physical pin 1: regulated 5 V. GND, physical pin 14: common ground.
- Fit a 100 nF bypass capacitor directly between ATtiny pins 1 and 14.
- Uno D5 to PB3/RX, physical pin 6, through its own 1 kΩ resistor.
- Uno D5 to PB2/TX, physical pin 7, through a separate 1 kΩ resistor.
- The verified single-node setup connects Uno D6 directly to PA0/UPDI,
  physical pin 10. PB0, physical pin 9, is a firmware selection/status output;
  it does not switch or electrically isolate the direct UPDI connection. Do
  not treat protocol `UPDI ENABLE`/`DISABLE` responses as evidence of physical
  isolation.
- Connect the L298N logic ground and actuator-supply negative to the common
  ATtiny/Uno ground.
- The two L298N channels share one motor-supply input (`Vs`, often labelled
  `+12V`). Both the motor and valve therefore receive that same supply voltage.
  At the reported 22 V, confirm that both loads and the exact module are rated
  for the rail; a 24 V load may not deliver its full rated performance.
- For common L298N modules, when `Vs` is above 12 V, remove the `5V-EN`
  regulator jumper and feed the module's logic 5 V terminal from a regulated
  5 V rail. Check the exact module markings/schematic; never parallel its
  onboard regulator output with an external 5 V source.

Use the module's flyback diodes for both inductive loads only if the exact board
has them fitted and rated for the load. Otherwise add correctly rated clamp
diodes as specified by the L298N/module documentation. Use a fuse and an
accessible physical supply cutoff.

## AS5600 wiring

| AS5600 signal | ATtiny1614 |
|---|---|
| VCC | Regulated 5 V, if that specific module permits 5 V |
| GND | Common ground |
| SDA | PA1, physical pin 11 |
| SCL | PA2, physical pin 12 |

Use 4.7 kΩ SDA/SCL pullups to 5 V if the module does not already contain them.
Keep the magnet centered and at the gap specified for the particular AS5600
board. `ANGLE` returns raw counts 0–4095; `READ_ENCODER` returns signed
accumulated counts and `RESET_ENCODER` establishes a new zero.
The megaTinyCore default I2C route is PB1/PB0; the firmware explicitly selects
the wired PA1/PA2 route with `Wire.pins(PIN_PA1, PIN_PA2)` before `Wire.begin()`.

## Current and safe bring-up

The motor and valve current estimates are not substitutes for their ratings.
Verify both loads against the L298N chip **and the actual module's**
thermal/current limits before actuation. Generic module current claims are not
a substitute for its schematic or a datasheet.

To measure the L298N channel current through its sense path, first identify the
module's SENSE A/SENSE B resistor and its value from the board schematic or
component marking. With a meter in voltage mode, measure across that resistor
while the respective channel is active and calculate `I = V/R`. Do not guess
the resistor value. If the module does not expose a known sense resistor, use a
current-limited bench supply and a properly fused ammeter **in series** with
the supply; start with the valve off and the motor unloaded. Never put a meter
in current mode across the supply or motor terminals. Do not lock/stall the
motor to measure stall current; use the exact motor datasheet or a suitable
current-limited test method.

The real-peripheral tests require:

1. IN1–IN4 pulldowns installed or confirmed on the exact module.
2. The module's 5 V regulator/jumper configured for the actual `Vs` voltage.
3. Motor/valve current limits verified and a physical cutoff within reach.
4. The motor able to move freely in both directions without reaching a hard
   stop, pinching anything, or driving a coupled mechanism unexpectedly.

Keep D5/D6 and the single node connected to the Uno. Run from Termux after
installing the sketch, script, and existing `andruino-flash` tool:

```sh
andruino-flash devices
UNO_USB=/dev/bus/usb/001/003
~/comprehensive_test_l298_node1.sh "$UNO_USB"
```

The test flashes Node 1, verifies communication and the AS5600, tests the LED,
requests an operator-confirmed 500 ms valve pulse, then runs 250 ms forward and
reverse motor pulses. It checks that the encoder changes and that the count
direction reverses. It requires an interactive `ACTUATE` confirmation and asks
whether the valve physically actuated. The firmware replies and encoder values
cannot independently prove that the valve moved; the operator must observe it.
If a command or SSH link fails, use the physical supply cutoff: remote `STOP`
is not an emergency stop.

## Nine-position motor/valve cycle

The separate `nine_point_l298_node1.sh` test requires the task-specific strict
Uno master build with bounded `MOVE` forwarding; the archived known-good master
does not forward that command. It flashes node firmware version
`ATTINY1614-L298N-AS5600-5` by default. It defines the current shaft angle as
point 0 and visits only the nine unique AS5600 positions: 0, 455, 910, 1365,
1820, 2276, 2731, 3186, and 3641 counts relative to the start. The first move
to point 1 also establishes the encoder's forward sign. The valve pulses ON
for 500 ms and OFF at each stopped station. After point 8, the motor returns
in reverse through points 7 to 0 with the valve OFF; it does not make an extra
full revolution to duplicate point 0. The test stops on encoder errors,
direction mismatch, motion timeout, or position error greater than 32 counts.
Like the Nano point-cycle test, it polls the compact `MOTION_STATUS` response
while moving and verifies full motor/valve `STATUS` only when stopped. It logs
timestamped motion state, accumulated counts, target, and raw angle.

The ATtiny performs the fast position loop locally; Termux's several-second bus
round trip is used only for supervision and logging. Run it from an interactive
Termux shell after installing the updated sketch and script:

```sh
~/nine_point_l298_node1.sh /dev/bus/usb/001/003
```

The script requires startup confirmation of the IN1–IN4 pulldowns, the
22 V/5 V module power configuration, the free shaft, the reachable physical
cutoff, and a previously observed 500 ms valve pulse. There are no operator
prompts during the station cycle. It is not a substitute for checking the
exact L298N module's ratings or confirming the load current. Do not run it if
the module's 5 V regulator configuration is uncertain at 22 V.
