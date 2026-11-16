# ATtiny1614 Node 1 AS5600 angle test

This sensor-only firmware uses the existing Uno/ATtiny1614 addressed half-duplex
bus and reads an AS5600 magnetic encoder. It does not configure or command
motor or valve outputs.

> **Validation status:** with `Wire.pins(PIN_PA1, PIN_PA2)` explicitly set,
> the AS5600 sketch flashed and returned changing angle readings on the
> connected hardware. The initial failure was the megaTinyCore default I2C
> route using PB1/PB0 instead of the wired PA1/PA2 pins. This sensor test is
> separate from the `BlinkNode1` communication baseline in
> [VERIFIED_BASELINE.md](VERIFIED_BASELINE.md).

## Wiring

Keep the Uno strict JTAG2UPDI master and existing Node 1 bus wiring:

- Uno D5 to ATtiny1614 PB3/RX, physical pin 6, through a 1 kOhm resistor.
- Uno D5 to ATtiny1614 PB2/TX, physical pin 7, through a separate 1 kOhm resistor.
- Uno D6 directly to ATtiny1614 PA0/UPDI, physical pin 10.
- ATtiny1614 VDD, physical pin 1, to its regulated supply; GND, physical pin 14,
  to common ground.

Connect the AS5600:

| AS5600 | ATtiny1614 |
|---|---|
| SDA | PA1, physical pin 11 |
| SCL | PA2, physical pin 12 |
| VCC | Supply permitted by the specific AS5600 breakout |
| GND | Common ground |

Use SDA/SCL pull-ups to a voltage supported by both the breakout and the
ATtiny1614. Keep the magnet centered and at the gap specified for the encoder
board. No motor or valve wiring is needed for this test. The ATtiny1614
megaTinyCore default I2C route is PB1/PB0, so this sketch explicitly selects
the alternate PA1/PA2 route with `Wire.pins(PIN_PA1, PIN_PA2)` before
`Wire.begin()`. The pull-ups must be connected to the correct I2C logic supply;
2.2–4.7 kΩ is a practical starting range for short wiring if the module does
not already have pull-ups.

## Angle and commands

The firmware answers `PING`, `VERSION`, `STATUS`, `ANGLE`, `READ_ENCODER`, and
`RESET_ENCODER`, plus the existing `MODE`, `SELECT`, and `UPDI` commands needed
by the master. `ANGLE` reports the AS5600's raw 12-bit angle (0–4095);
`READ_ENCODER` reports accumulated signed counts, with wraparound handled while
sampling. One full turn is 4096 counts, or approximately 0.0879 degrees/count.
An unavailable sensor returns `ANGLE -1` or `ENCODER -999999`.

## Build, flash, and test

Copy `andruino/attiny1614/AS5600Node1/` into the phone's
`/storage/emulated/0/Download/andruino/attiny1614/` directory and copy
`comprehensive_test_as5600_node1.sh` to the Termux home directory. Ensure the
Uno already has the hash-verified strict master. In Termux, run:

```sh
chmod 700 ~/comprehensive_test_as5600_node1.sh
~/comprehensive_test_as5600_node1.sh /dev/bus/usb/001/003
```

Replace the USB path with the current path from `andruino-flash devices`. The
test builds and flashes only the sensor node, verifies Node 1 communication,
reads its angle, resets the accumulated count, and asks you to turn the magnet
or shaft by hand. It verifies the accumulated count changes; it sends no motor
or valve commands. Use `--no-flash` instead of reflashing only when this
sensor-only firmware is already installed on Node 1.

## Continuous angle output

Copy `read_as5600_node1.sh` to the Termux home directory. Add `--flash` if Node
1 is not already running the AS5600 firmware; otherwise the script verifies
the firmware version before streaming:

```sh
chmod 700 ~/read_as5600_node1.sh
~/read_as5600_node1.sh --flash /dev/bus/usb/001/003
```

It prints one line per attempt to standard output: timestamp, raw angle
(`0..4095`), and degrees (`raw * 360 / 4096`). If the sensor is unavailable
(`ANGLE -1`) or a request fails, it prints a timestamped `ERROR` status and
keeps retrying instead of exiting. The default delay is 0.5 seconds between
requests; use `--interval 0.2` to change it. With `--flash`, it rediscovers
the bus and restores COMM mode before streaming. Press Ctrl+C to stop.
