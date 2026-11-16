# Andruino complete system regeneration guide

This document describes the Uno/Nano JTAG2UPDI control-bus architecture and the
ATtiny1614 sketches.

> **Current reproduction path:** use [REPRODUCING.md](REPRODUCING.md) and
> [VERIFIED_BASELINE.md](VERIFIED_BASELINE.md). There are two verified master
> images: the archived baseline/Nano image (`19889521...`) and the
> MOVE-enabled Uno image (`da81995...`) used by the single-node and L298N
> tests. They are not interchangeable. The ordinary Arduino CLI build of
> `andruino/uno/jtag2updi/jtag2updi.ino` is neither image.
> The seven-node results below are historical and do not validate physical
> actuators, sensors, or UPDI isolation.

## 1. System components

The system has three independent communication layers:

1. **AVR flashing**
   - Android app hosts native AVRDUDE.
   - Android USB-host APIs provide the USB serial file descriptor.
   - AVRDUDE uses `-P COM` with the Android USB transport.
   - AVRDUDE talks to Uno or Nano firmware that implements a programmer protocol.

2. **Uno/Nano JTAG2UPDI programmer firmware**
   - Either master is flashed with the same extended JTAG2UPDI implementation.
   - D6 is used by JTAG2UPDI for UPDI signaling.
   - The Nano old-bootloader profile changes the master upload baud; it does not change the firmware source or bus wiring.
   - D5 is added as a half-duplex 2400-baud node control bus.

3. **ATtiny1614 node firmware**
   - Node firmware listens for addressed text commands on PB3/RX.
   - It transmits responses from PB2/TX.
   - Logical address 1 through 7 is compiled into each node image.
   - PB0 reports the selected/LED flash state through a separate output. The LED is PA5, physical pin 3.

### Repository layout

```text
andruino/
  uno/
    uno-jtag2updi-strict/
      uno-jtag2updi-strict.ino  MOVE-enabled Uno master source
    jtag2updi/
      jtag2updi.ino              Other/legacy Arduino CLI build; not verified master
      control_bus.cpp            D5 node control protocol
      control_bus.h
      JTAG2.cpp                  Modified to recognize control-bus commands
      [other upstream JTAG2UPDI files]
    Blink300/
      Blink300.ino
    Blink3000/
      Blink3000.ino
  nano/
    Blink300/
      Blink300.ino
    Blink3000/
      Blink3000.ino
  attiny1614/
    Blink300/
      Blink300.ino
    Blink3000/
      Blink3000.ino
    BlinkNode1/
      BlinkNode1.ino
    BlinkNode2/
      BlinkNode2.ino
    BlinkNode3/
      BlinkNode3.ino
    BlinkNode4/
      BlinkNode4.ino
    BlinkNode5/
      BlinkNode5.ino
    BlinkNode6/
      BlinkNode6.ino
    BlinkNode7/
      BlinkNode7.ino
    libraries/
      AndruinoBusNode/
        library.properties
        src/attiny_bus_node.h
tools/andruino-flash              Termux build and service wrapper
docs/REPRODUCING.md               Installation and flashing procedure
```

The strict-source master requires its custom builder and SHA-256 check; do not
build it with the generic `andruino-flash flash-sketch` path. See
`REPRODUCING.md` for the Uno build/flash steps and the exact Nano upload
profile.

## 2. Programming and control hardware

### ATtiny1614 physical pins used by the firmware

| Function | MegaTinyCore symbol | Physical DIP pin |
|---|---|---:|
| Supply | VDD | 1 |
| LED | PA5 | 3 |
| Node RX / bus receive | PB3 | 6 |
| Node TX / bus transmit | PB2 | 7 |
| Flash selection output | PB0 | 9 |
| UPDI | PA0 | 10 |
| Ground | GND | 14 |

### Direct UPDI flashing

At either master:

```text
Master D6  -> ATtiny PA0/UPDI, physical pin 10
Master 5V  -> ATtiny VDD, physical pin 1
Master GND -> ATtiny GND, physical pin 14
```

Connect only one ATtiny UPDI target at a time. The verified single-node
procedure keeps Node 1 connected during master flashing; use that documented
script rather than treating `MODE`, `SELECT`, or `UPDI` responses as proof of
electrical isolation.

### Shared node control bus

```text
Master D5 -> Node PB3, physical pin 6, through separate 1 kOhm resistor
Master D5 -> Node PB2, physical pin 7, through separate 1 kOhm resistor
Master GND -> Node GND, physical pin 14
Master 5V  -> Node VDD, physical pin 1
PB0 on each node remains available as the selection/output signal
```

Repeat the two 1 kOhm bus branches for each additional node. All nodes share master D5 and a common ground. The bus is half duplex: the master sends addressed commands, then releases D5, allowing one addressed node to transmit a response.

## 3. Shared Uno/Nano JTAG2UPDI firmware

The archived baseline image has SHA-256
`19889521fed85125927b623edbc3c55cfa6fcca74e4fa6aed625c4088e9a9c5d`; use it
for the historical Nano result and baseline multi-node procedure. The current
Uno package `andruino/uno/uno-jtag2updi-strict` adds bounded `MOVE` forwarding
and a larger bus-response buffer for the L298N test. Its builder checks
SHA-256 `da81995aad8792513af0e7d5a3aa28956d7c2c1abb465b91e2f9521ce31f96cd`;
use this image on the Uno only. It is not the Nano HEX. The separate
`andruino/uno/jtag2updi` Arduino CLI build is neither verified image.

### Required control-bus integration

`control_bus.cpp` must define:

```cpp
const uint8_t BUS_PIN = 5;
const uint8_t FIRST_NODE = 1;
const uint8_t LAST_NODE = 7;
const uint16_t BUS_BAUD = 2400;
const uint16_t RESPONSE_TIMEOUT_MS = 700;
const uint8_t REQUEST_ATTEMPTS = 3;
const uint8_t RETRY_DELAY_MS = 20;
```

Both master profiles use `SoftwareSerial(BUS_PIN, BUS_PIN)` on D5. In setup, initialize the control bus after the normal JTAG2UPDI links have been initialized:

```cpp
JICE_io::init();
UPDI_io::init();
ControlBus::init();
```

When AVRDUDE sends the first byte, JTAG2UPDI normally expects a binary programmer packet. A line beginning with `@` instead dispatches control-bus commands:

```cpp
if (firstByte == '@') {
  ControlBus::handleHostCommand();
  return false;
}
```

This is the modification to the shared master receive loop in `JTAG2.cpp`.

### Host protocol

Android sends the host command in this form at 19200 baud:

```text
@PING 1
@DISCOVER
@MODE COMM
@MODE FLASH
@SELECT 2
@STATUS 1
@VERSION 2
@BLINK 1 200
```

The master replies with exactly one ASCII line and CR/LF termination. Examples:

```text
NODE 1 PONG
OK NODES 1,2
OK MODE COMM; NODES 1,2,3,4,5,6,7 MODE COMM; SELECT NONE
```

`DISCOVER` is the only authoritative active-node result. The `MODE COMM` response text is intentionally formatted for the complete system and can print all seven logical node numbers even when discovery has not found them.

### Master D5 command transmission

Every node command has the form `<address> <command>`:

```text
1 PING
2 MODE COMM
1 SELECT 2
2 BLINK 200
```

Before writing, D5 is released during the receive window. During a transmission, it is driven high and SoftwareSerial prints the command. The master waits for a response beginning with `NODE <address> `. A response for another address is ignored. It permits three attempts with a 20 ms retry delay.

## 4. ATtiny1614 node firmware library

Place this header here:

```text
andruino/attiny1614/libraries/AndruinoBusNode/src/attiny_bus_node.h
```

A node sketch must contain only this wrapper and the correct address:

```cpp
// ATtiny1614 node 1
#define NODE_ADDRESS 1
#include <attiny_bus_node.h>
```

Use `NODE_ADDRESS 2` or `NODE_ADDRESS 3` for the corresponding boards. Do not accidentally compile Node 1 firmware for two physical boards.

The library performs the following initialization:

```cpp
pinMode(PIN_PA5, OUTPUT);       // active-low LED
pinMode(PIN_PB0, OUTPUT);       // external flash-selection output
Serial.begin(2400);             // PB2/PB3 hardware USART
releaseBus();                   // starts with PB2 RX/release state
```

PA5 is inverted because the Node LED implementation considers HIGH off and LOW on.

### Node command parser

Each received line is interpreted as:

```text
ADDRESS COMMAND [ARGUMENT]
```

The address must equal `NODE_ADDRESS`, because each physical node receives the same shared bus traffic but only one node can process a line.

Supported commands are:

| Command | Example | Behavior |
|---|---|---|
| PING | `2 PING` | Returns `NODE 2 PONG` |
| VERSION | `1 VERSION` | Returns firmware/version identifier |
| STATUS | `1 STATUS` | Returns compact mode, valve, motor, LED, and UPDI state fields |
| MODE COMM | `2 MODE COMM` | Communication mode and persistent EEPROM state |
| MODE FLASH | `2 MODE FLASH` | UPDI-flashing mode and persistent EEPROM state |
| SELECT | `1 SELECT 2` | Selects a logical node address from 1 through 7 |
| BLINK | `2 BLINK 200` | Sets allowed COMM blink interval to 200 or 1000 ms |

Before transmitting, the node delays 10 ms. It temporarily enables the hardware USART TX, prints the response, flushes it, disables TX, and returns PB2 to input mode. This prevents simultaneous node transmission.

### Persistent state

The node stores its mode and selected address in EEPROM addresses 0 and 1, protected by a magic byte at address 2:

```cpp
EEPROM_MODE_ADDRESS     = 0
EEPROM_SELECTED_ADDRESS = 1
EEPROM_MAGIC_ADDRESS    = 2
EEPROM_MAGIC             = 0xA7
```

The node returns to communication mode with selection 0 when EEPROM does not contain valid magic state.

### LED behavior

- COMM mode: PA5 toggles independently using 200 ms or 1000 ms intervals.
- FLASH mode: PA5 is on only when `selectedNode == NODE_ADDRESS`.
- PB0 is the inverse of this flash-selection state so external logic can identify the selected node.

## 5. Standalone blink sketches

Simple Uno blink at 300 ms:

```cpp
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(300);
  digitalWrite(LED_BUILTIN, LOW);
  delay(300);
}
```

Simple ATtiny1614 PA5 blink at 300 ms:

```cpp
void setup() {
  pinMode(PIN_PA5, OUTPUT);
}

void loop() {
  digitalWrite(PIN_PA5, HIGH);
  delay(300);
  digitalWrite(PIN_PA5, LOW);
  delay(300);
}
```

Change `300` to `3000` for the three-second blink variants. Directory names and `.ino` names must match; for example, `Blink3000/Blink3000.ino`.

## 6. Arduino CLI build profiles

Use Arduino CLI 1.2.2 and the verified package versions:

```text
arduino:avr:uno 1.8.8
megaTinyCore:megaavr:2.6.11
```

Do not use the generic Arduino CLI `build` command to create either verified
JTAG2UPDI master image. Use the archived source/HEX for the baseline and Nano,
or the hash-checking `build-strict-master.sh` for the MOVE-enabled Uno image,
as described in sections 3 and 7. Arduino CLI remains the build tool for the
ordinary Uno, Nano, and ATtiny sketches; a successful compile of
`andruino/uno/jtag2updi` does not produce a verified master.

Uno uses `arduino:avr:uno` and 115200 baud for its standard bootloader.
Classic Nano uses `arduino:avr:nano:cpu=atmega328old` and 57600 baud for its
old bootloader. These are upload-profile differences; the sketch and D5/D6
wiring are the same.

The ATtiny FQBN must include the exact tested options:

```text
megaTinyCore:megaavr:atxy4
```

Board options:

```text
chip=1614,clock=16internal,bodvoltage=1v8,bodmode=disabled,
eesave=enable,millis=enabled,startuptime=8,wiremode=mors,
printf=default,PWMmux=default,attach=allenabled,
WDTtimeout=disabled,WDTwindow=disabled
```

For the shell build helper, use:

```sh
andruino-flash build --platform attiny1614 /storage/emulated/0/Download/andruino/attiny1614/BlinkNode1/BlinkNode1.ino
```

## 7. Master flashing and validity requirement

Build and flash the task-specific MOVE-enabled strict-source master. The
single-node Uno protocol script performs this build, checks its SHA-256,
flashes the master, and verifies read-back:

```sh
./comprehensive_test_uno_node1.sh
```

For manual flashing, use the hash-matched HEX for the intended test. The
archived baseline image (`19889521...`) is stored at
`releases/uno-jtag2updi/uno-jtag2updi-strict.hex`; the L298N and single-node
Uno tests require the MOVE-enabled image (`da81995...`) built from
`andruino/uno/uno-jtag2updi-strict`. Never pass an `.ino` file to
`flash-platform`.

A valid master result must include all of the following:

```text
Writing | ################################################## | 100%
Reading | ################################################## | 100%
bytes of flash verified
Avrdude done. Thank you.
RESULT success
```

A config-file warning from AVRDUDE is not enough by itself to declare success. Writing and reading must reach 100% and the completion line must be present.

The verified single-node script leaves Node 1 connected to D5/D6 during master
flashing. D6 is the direct UPDI line to PA0; the normal communication checks
use D5.

## 8. Direct ATtiny flashing

Flash one ATtiny Node 1 image at a time:

```sh
andruino-flash flash-sketch \
  --platform attiny1614 \
  --usb-device "$UNO_USB" \
  /storage/emulated/0/Download/andruino/attiny1614/BlinkNode1/BlinkNode1.ino
```

Change the final path to `BlinkNode2` through `BlinkNode7` for each
corresponding physical node. For manual multi-node flashing, connect D6 to only
the selected target's UPDI and physically disconnect/isolate all other target
UPDI pins. The `UPDI ENABLE`/`DISABLE` protocol commands do not operate or
verify an electrical gate. The optional gate designs in older wiring notes are
unverified; do not rely on them without measuring isolation.

```sh
for n in 1 2 3 4 5 6 7; do
  echo "Connect D6 to only node $n UPDI and press Enter"
  read
  andruino-flash flash-sketch --platform attiny1614 --usb-device "$UNO_USB" \
    "/storage/emulated/0/Download/andruino/attiny1614/BlinkNode$n/BlinkNode$n.ino"
done
```

Before automated gate-controlled flashing can be trusted, test `UPDI ENABLE n` and `UPDI DISABLE n` for every node independently and verify with a multimeter or AVRDUDE that non-selected gates are electrically isolated.

## 9. Manual multi-node bring-up

For the exact, verified single-node Uno setup, use the
`comprehensive_test_uno_node1.sh` procedure in `REPRODUCING.md`; it
rebuilds/flashes the strict master and keeps the single node connected. The
manual steps below are for expanding or diagnosing a multi-node bus and were
not part of that single-node test.

1. Flash the strict-source Uno master using the checked builder and HEX
   procedure in section 7; do not flash the generic `jtag2updi.ino` build.
2. Connect only Node 1's D5 control-bus branches, power, and ground. For direct
   target programming, connect D6 to only the selected node's PA0/UPDI.
3. Reset/power-cycle Node 1.
4. Run:

   ```sh
   andruino-flash bus --usb-device "$UNO_USB" MODE COMM
   andruino-flash bus --usb-device "$UNO_USB" DISCOVER
   andruino-flash bus --usb-device "$UNO_USB" PING 1
   ```

5. Confirm Node 1 returns `NODE 1 PONG`.
6. Disconnect Node 1's D5 branch and connect only Node 2 in the same documented position.
7. Flash BlinkNode2 with UPDI, then repeat `MODE COMM`, `DISCOVER`, and `PING 2`.
8. Only after each node passes isolated discovery should both be connected to the same D5 bus.
9. Run a final discovery:

   ```sh
   andruino-flash bus --usb-device "$UNO_USB" DISCOVER
   ```

   The expected healthy result is `OK NODES 1,2` for the two-node setup.

## 10. Diagnostic interpretation

- `Writing 100%`, `Reading 100%`, and `Flash verified` proves a correct Uno or ATtiny flash operation.
- `OK NODES 1` proves Node 1 received `PING` and transmitted a valid response.
- `PING 2` no-response proves the Node 2 path failed at receive, transmit, address, timing, or wiring.
- `MODE COMM` should not be interpreted as a discovery test. It requests mode changes and returns the Uno's broader expected-state text.
- Successful direct UPDI programming does not prove that the node control library is running.
- Different clock/fuse settings, swapped PB2/PB3 wires, missing series resistors, missing ground, and physical-board faults remain the first suspects when isolated Node 2 discovery fails.

## 11. Historical circular test

The historical script cycled Node 1 and Node 2 through BlinkNode1, BlinkNode2, and BlinkNode3 while changing logical selection. Its prerequisites were:

```text
Both physical nodes were flashed with their address-aware node firmware.
D5 had two node control branches.
Node PB3/RX and PB2/TX were wired through separate 1 kOhm resistors.
UPDI D6 was isolated to one target for each flashing operation.
A successful DISCOVER reported the two target addresses.
PING 1 and PING 2 both returned PONG.
```

A repeat is valid only after isolated and combined discovery are reliable. A flashing success by itself must not be used as evidence that the control bus is repaired.
