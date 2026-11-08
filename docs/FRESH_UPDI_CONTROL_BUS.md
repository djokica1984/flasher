# Fresh ATtiny1614 UPDI and control-bus design

## Purpose

This design removes three sources of ambiguity:

1. UPDI is isolated from the control bus unless AVRDUDE has explicitly enabled it.
2. The Uno and both ATtiny node sketches share one explicitly defined serial pin route.
3. The Android app restores the UPDI isolation gate to the safe disabled state after every flash attempt.

This repository is intentionally using the two-node protocol only: node addresses 1 and 2.

## Required wiring

| Uno | ATtiny1614 | Notes |
|---|---|---|
| 5 V | Physical pin 1, VDD | Both targets share power |
| GND | Physical pin 14, GND | Common ground is mandatory |
| D5 | Physical pin 6, PB3/RX | Uno shared bus, individual 1 kOhm resistor |
| D5 | Physical pin 7, PB2/TX | Uno shared bus, individual 1 kOhm resistor |
| D6 | UPDI | Through the isolation switch circuit |
| D4 | PMOS gate, physical pin 9/PB1 circuit input | HIGH is OFF/disabled, LOW is ON/enabled |

The UPDI isolation circuit must use a PMOS suitable for the target voltage between the Uno D6 line and target UPDI. Its gate is controlled by Uno D4. A normal logic HIGH on D4 disables UPDI; LOW enables it. Do not connect the PMOS gate directly to target power without the required gate resistor or threshold matching.

Uno D4 is also connected to each node only if one shared gate controls all nodes in the permanent wiring. The Uno must never enable UPDI while ordinary node bus traffic is running.

## Firmware behavior

### ATtiny1614

- Node 1 uses address 1 in `BlinkNode1.ino`.
- Node 2 uses address 2 in `BlinkNode2.ino`.
- `PB3`, physical pin 6, is RX.
- `PB2`, physical pin 7, is TX.
- `PB1`, physical pin 9, is reserved for the external gate signal and is never driven by firmware.
- The node UART route is explicitly selected to alternate USART0 routing for PB2/PB3.
- TX is released to input/high impedance when idle.
- Control responses use `NODE <address> ...`.
- Supported commands are `PING`, `VERSION`, `STATUS`, `MODE COMM`, `MODE FLASH`, `SELECT 1/2`, and `BLINK 200/1000`.
- In `COMM` mode, the blink LED is controlled by its normal configured blink period.
- In `FLASH` mode, only the selected node LED is on.

### Uno

- Uno D5 is the shared 2400-baud half-duplex node bus.
- Uno D4 is the safe UPDI-PMOS gate output.
- D4 starts HIGH, so UPDI is disabled.
- `@UPDI ENABLE` drives D4 LOW.
- `@UPDI DISABLE` drives D4 HIGH.
- `@DISCOVER`, `@PING 1`, etc. operate independently of UPDI isolation.

The gate should remain enabled for as short a time as possible. If an external reset or power failure occurs, the Uno code initializes D4 HIGH.

### Android

Before AVRDUDE starts a JTAG2UPDI operation, the app:

1. Restores the Uno persisted COMM/FLASH mode and selection.
2. Sends `@UPDI ENABLE` at the Uno control rate.
3. Changes serial parameters back to AVRDUDE's baud.
4. Runs native bundled AVRDUDE.
5. Sends `@UPDI DISABLE`.
6. Restores the Uno persisted control-bus state.

The disable operation is also attempted from the app's error cleanup path. Manual bus commands accepted by the app include `UPDI ENABLE` and `UPDI DISABLE`.

## Protocol

Host-to-Uno commands have `@` as the first character and use newline termination.

```text
@UPDI ENABLE
@UPDI DISABLE
@PING 1
@PING 2
@DISCOVER
@VERSION 1
@MODE COMM
@MODE FLASH
@SELECT 1
@RESTORE
```

Node-to-Uno responses are one line, for example:

```text
NODE 1 PONG
NODE 2 PONG
NODE 1 VERSION ANDRUINO-ATTINY1614-FRESH-1
```

## Test procedure

1. Disconnect USB from the Uno's serial connection if the Uno must be flashed.
2. Flash the Uno JTAG2UPDI firmware, then reconnect USB.
3. Send `UPDI DISABLE` and verify `OK UPDI DISABLED`.
4. Send `DISCOVER` and verify that both active nodes are found after node firmware is installed.
5. Send `PING 1` and `PING 2`; both must return PONG.
6. Flash Node 1 with direct UPDI using Uno D6/UPDI and verify it at 100%.
7. Send `UPDI DISABLE`, then verify `PING 1`.
8. Repeat for Node 2.
9. Run `MODE COMM`, `SELECT 1`, and `SELECT 2` and verify each selected node.

Do not disconnect D4, D5, or D6 continuously during normal operation. The isolation transistor should provide safe separation, rather than requiring users to unplug wiring between flashing and communication.

## Troubleshooting

If UPDI flashing succeeds but control ping fails:

- Confirm physical pins 6 and 7 are PB3 and PB2, not package pin names from another ATtiny family.
- Confirm D5 reaches each RX/TX node pin through separate 1 kOhm resistors.
- Confirm common ground.
- Confirm Uno D4 is HIGH when control bus commands are sent.
- Confirm the PMOS does not pull UPDI, PB2, or PB3 unexpectedly low.
- Check that both node sketches use alternate USART routing and the fresh node protocol version.
- Do not mix this node protocol with firmware generated from a different commit generation.
