# ATtiny1614 Node 1 wiring

This description applies to one complete node with valve, bidirectional motor,
AS5600 magnetic encoder, UPDI isolation, and the Uno control bus.

## Package reference

Viewed from above with the notch toward you:

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

## Power and grounding

- Uno 5 V to ATtiny physical pin 1, VDD.
- Uno GND to ATtiny physical pin 14, GND.
- Place a 100 nF ceramic capacitor between pins 1 and 14, close to the ATtiny.
- The UNO, ATtiny, AS5600, DRV8871, valve MOSFET, and external load grounds
  must be connected together.
- Do not power the valve or motor from physical pin 1. Use a supply rated for
  the actual load. Connect its positive supply only to the load/MOSFET or H-
  bridge side of the design.

## UPDI isolation

- Uno D6 to AO3401 source.
- AO3401 drain to ATtiny physical pin 10/PA0/UPDI.
- ATtiny physical pin 9/PB0 to AO3401 gate.
- Put approximately 100 kOhm from physical pin 9/PB0 to GND.
- PB0 LOW enables UPDI; PB0 HIGH disables UPDI.

The source/drain arrangement must allow an enabled PMOS path in either physical
orientation. Confirm the actual component package pinout (Gate, Source, Drain)
and continuity with a meter with power removed. Do not assume D6 and UPDI are
interchangeable through the transistor.

## Uno control bus

Uno D5 is the shared half-duplex line.

```text
Uno D5 --- 1 kOhm --- ATtiny physical pin 6 / PB3 / RX
Uno D5 --- 1 kOhm --- ATtiny physical pin 7 / PB2 / TX
```

There are no pull-up or pull-down resistors on this bus line. The Uno firmware
implements the idle pull-up. PB2's TX output is disabled while the node is
waiting for a command.

## Existing status indicator

- Physical pin 3/PA5 is active-low status LED.
- LED cathode to physical pin 3.
- LED anode to 1 kOhm resistor to the 5 V supply.

## Valve NMOS driver

Recommended ATtiny output: physical pin 2/PA4.

```text
physical pin 2/PA4 --- 100 ohm to 1 kOhm --- valve MOSFET gate
valve MOSFET gate --- 100 kOhm --- GND
valve MOSFET source --- GND
valve MOSFET drain --- valve negative/load terminal
valve positive/load supply --- valve positive terminal
```

Use a logic-level NMOS rated for the valve coil voltage and current. Add a
flyback diode across a DC inductive valve coil, cathode toward the positive
supply. Do not energize the valve until the supply voltage, MOSFET package,
and coil polarity are verified.

## DRV8871 bidirectional motor driver

| Function | ATtiny pin | Physical pin | DRV8871 connection |
|---|---|---:|---|
| IN1 | PA6 | 4 | IN1 |
| IN2 | PA7 | 5 | IN2 |
| Sleep/disable | PB1 | 8 | nSleep, pulled high only when intentionally enabled |

Additional DRV8871 connections:

- Motor outputs to the motor.
- VM to the correctly rated motor supply through the board's recommended bulk
  and ceramic capacitors.
- Power and logic grounds common.
- Follow the board/data-sheet installation for VM decoupling and current-limit
  configuration; do not infer them from this table.

Firmware starts and ends both bridge inputs LOW. `nSleep` is LOW when stopped
and HIGH only while moving.

## AS5600 magnetic encoder

Default ATtiny hardwire SDA/SCL pins for megaTinyCore are PA1/PA2:

| Function | ATtiny pin | Physical pin | AS5600 connection |
|---|---|---:|---|
| SDA | PA1 | 11 | SDA |
| SCL | PA2 | 12 | SCL |
| Ground | --- | 14 | GND |
| Encoder supply | --- | --- | VCC, module-specific voltage rating |

Use 4.7 kOhm SDA and SCL pull-ups to the AS5600 supply rail unless the chosen
module already has pull-ups. Supply the module only from a voltage allowed by
its specification. Decouple its VCC with 100 nF close to the module.

Physical pins 13/PA3 and 8/PB1 are allocated respectively as spare and DRV8871
sleep. Physical pin 8 is unavailable as a spare when sleep control is used.

## Addressed node protocol

The Uno host syntax remains `COMMAND <node> ...`. Node requests are translated
on the bus to:

```text
PING
STATUS
VERSION
STOP
VALVE ON|OFF
MOTOR OFF
MOTOR FORWARD <milliseconds>
MOTOR REVERSE <milliseconds>
READ_ENCODER
RESET_ENCODER
ANGLE
```

Examples through Android/Termux:

```sh
andruino-flash bus --usb-device "$UNO_USB" PING 1
andruino-flash bus --usb-device "$UNO_USB" STATUS 1
andruino-flash bus --usb-device "$UNO_USB" VALVE 1 ON
andruino-flash bus --usb-device "$UNO_USB" VALVE 1 OFF
andruino-flash bus --usb-device "$UNO_USB" MOTOR 1 FORWARD 1000
andruino-flash bus --usb-device "$UNO_USB" MOTOR 1 REVERSE 1000
andruino-flash bus --usb-device "$UNO_USB" MOTOR 1 OFF
andruino-flash bus --usb-device "$UNO_USB" READ_ENCODER 1
andruino-flash bus --usb-device "$UNO_USB" ANGLE 1
andruino-flash bus --usb-device "$UNO_USB" RESET_ENCODER 1
andruino-flash bus --usb-device "$UNO_USB" STOP 1
```

The angle is the AS5600 raw reading in the range 0..4095. The encoder position
is accumulated as wrapped signed counts and therefore is resettable with
`RESET_ENCODER`.

## Safe bring-up sequence

1. Disconnect valve and motor power, leaving common grounds connected.
2. Verify Uno D5, PB2/PB3 resistors, PB0 gate, VDD/GND, and all load grounds.
3. Flash only the Uno bus firmware and verify `PING 1`.
4. Verify `STATUS 1`, `ANGLE 1`, and `READ_ENCODER 1` before enabling outputs.
5. Verify AS5600 angle changes while rotating the magnetic target.
6. Test `VALVE 1 OFF`, `MOTOR 1 OFF`, then the valve alone with an isolated
   current-limited supply.
7. Test the DRV8871 only with the motor and VM supply secured.
8. Use STOP and power isolation as the final safety controls; USB loss is not
   a hardware emergency stop.
