# Verified build and hardware baseline

Last recorded: 2026-11-16. This is the reproducibility record for the
Uno/Nano ATtiny1614 system. It records the successful single-node Uno test and
the earlier seven-node Uno/Nano tests. It also distinguishes the exact-image
Nano test from the failed Nano-profile rebuild; those are different firmware
images.

## Canonical sources

- The archived known-good master is the strict-source image below. Its exact
  source snapshot is
  [`releases/uno-jtag2updi/strict-source/`](../releases/uno-jtag2updi/strict-source/).
  Do not use `flash-sketch` on `andruino/uno/jtag2updi/jtag2updi.ino` as a
  substitute.
- The Uno package
  [`andruino/uno/uno-jtag2updi-strict/`](../andruino/uno/uno-jtag2updi-strict/)
  has a task-specific, experimental `MOVE` forwarding extension and a 128-byte
  bus-response buffer and accurate discovered-node reporting for the
  single-node L298N test. The larger buffer is required for the detailed
  `STATUS` reply once encoder counts reach four digits. Its builder checks
  SHA-256
  `da81995aad8792513af0e7d5a3aa28956d7c2c1abb465b91e2f9521ce31f96cd`; this
  variant is not the archived known-good master or the Nano image.
  Nano must use the exact strict HEX below, uploaded with
  `arduino:avr:nano:cpu=atmega328old` (old bootloader, 57,600 baud).
  Do not treat `andruino/nano/jtag2updi/` as a Nano source of truth.
- The exact image that passed the latest Nano test is
  `releases/uno-jtag2updi/uno-jtag2updi-strict.hex`, SHA-256
  `19889521fed85125927b623edbc3c55cfa6fcca74e4fa6aed625c4088e9a9c5d`.
  It is the same 15,216-byte image previously read-back verified on the Uno;
  "strict" is only its filename, not a firmware version.
- The bus is half-duplex on the same master D5 line for either board. Only
  node 1's UPDI was physically connected to the programmer at the latest
  seven-node test; this does not imply that all seven UPDI pins were tested
  simultaneously.
- The address-specific node sketches are
  `andruino/attiny1614/BlinkNode1/BlinkNode1.ino` through `BlinkNode7`. Each
  uses the shared `AndruinoBusNode` library and its corresponding node address.
- Android USB permissions belong to the attached USB device/connection and
  may need approval again after reconnecting. USB device paths are ephemeral;
  rediscover them with `andruino-flash devices`.

### Recovered strict-image source

The complete source snapshot that reproduces the archived strict master HEX is
saved at
[`releases/uno-jtag2updi/strict-source/`](../releases/uno-jtag2updi/strict-source/)
and copied into board-specific packages at
[`andruino/uno/uno-jtag2updi-strict/`](../andruino/uno/uno-jtag2updi-strict/)
and [`andruino/nano/nano-jtag2updi-strict/`](../andruino/nano/nano-jtag2updi-strict/).
The archived snapshot—not the ordinary Arduino CLI build—reproduces the
known-good master image. Rebuilding this snapshot with
[`tools/build-uno-firmware.ps1`](../tools/build-uno-firmware.ps1), Arduino AVR
Boards core `1.8.8`, and AVR-GCC
`7.3.0-atmel3.6.1-arduino7` produced SHA-256
`19889521fed85125927b623edbc3c55cfa6fcca74e4fa6aed625c4088e9a9c5d`, exactly
matching the archived strict HEX. The custom PowerShell build is significant:
Arduino CLI builds of this source with the Termux toolchain did not reproduce
the image. The Uno package has since diverged in its control-bus command parser
to support bounded `MOVE FORWARD|REVERSE 1..512` forwarding, size the response
buffer for the detailed L298N `STATUS` reply, and report only discovered nodes;
its custom builder pins the resulting task-specific hash listed above.

The phone copy at
`/storage/emulated/0/Download/strict-source-recovery/git-7f03242/jtag2updi`
was recovered and compared against the repository snapshot: all 25 source
files match byte-for-byte. The phone directory also contains a separately
generated `jtag2updi.ino.hex`; that file is not the verified strict image and
was not used. Rebuilding the repository source set with the custom builder
again produced the expected strict-image SHA-256 above. The default source for
[`tools/build-uno-firmware.ps1`](../tools/build-uno-firmware.ps1) now points to
the UNO strict package so a default build cannot accidentally select the
newer non-strict `andruino/uno/jtag2updi` tree.

### Current source fingerprints

SHA-256 values for the current checked-out source files:

These hashes describe the current working source tree, which has changes made
after the recovered strict-image snapshot. Use the saved snapshot above to
reproduce the strict HEX.

| File | SHA-256 |
|---|---|
| `andruino/uno/jtag2updi/jtag2updi.ino` | `01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b` |
| `andruino/uno/jtag2updi/control_bus.cpp` | `0bb28911e175f3cab5ea1620d3b81cd1f54ef6ed492d49d23814c2e323e2ec04` |
| `andruino/uno/jtag2updi/JTAG2.cpp` | `dd5cd55a849ad4566c2b1045fcb756b87a97c143f8eb99cf712d32b4c1147cc1` |
| `andruino/attiny1614/libraries/AndruinoBusNode/src/attiny_bus_node.h` | `9343dd4caa87ca6de54ee2a098f430a0194b97f7e1cfe5e2ae97179d003fb8b9` |
| `app/src/main/java/com/example/andruinoflash/FlashService.kt` | `6ad90c204c9728c07c73bdcd4ad376aa43f3c808503fc75fcab6c7ca828feb7a` |

### Address-specific node sketch fingerprints

| Sketch | SHA-256 |
|---|---|
| `BlinkNode1.ino` | `16c2b6d36c3a208915cf02eb2bec4b7e049798cb3900bad4c22bc5986b15252e` |
| `BlinkNode2.ino` | `272fb1cbc1d59b2ba5f966bd901e1c6f54705a9966c8a7fbd874f1f6f60ec928` |
| `BlinkNode3.ino` | `a6ba222f9aa89c181af64412ba983014217269a0378c880ad4f6f2827b3f0be5` |
| `BlinkNode4.ino` | `2bfe73933a3c8744f5d8bfc32c23b3e499117cb12feaf9da5252341fd6c8a79a` |
| `BlinkNode5.ino` | `6f438d1a245a5d514e63d8ab70ab6fcad57296e1c7511431d5220a7bab4754bb` |
| `BlinkNode6.ino` | `9f1ebca8afa089240c3592f8b641435131adf998080f792e79914a9b0140bb7f` |
| `BlinkNode7.ino` | `a2d701fdabf8710a7e0ef15e0fb59e941ad9c1a8e8a9b39138de97b076dd1dcd` |

## Tested toolchain and app

| Component | Tested version/configuration |
|---|---|
| Phone | Samsung SM-A556B, ARM64, Android 16 |
| Android app | Andruino Flash `0.1.0`, versionCode `1`, ARM64 debug APK |
| Android build | JDK `17.0.12`, Android SDK 35, NDK `28.2.13676358`, CMake `3.22.1`, Gradle `8.13`, Kotlin `2.0.21` |
| AVRDUDE | Bundled build `8.3-20261026`, commit `9aa44d08`; config reports `8.3` |
| Termux Arduino CLI | `1.2.2`, run in Ubuntu proot |
| Arduino AVR core | `1.8.8` |
| megaTinyCore | `2.6.11` |
| Uno master | `arduino:avr:uno`, bootloader upload at 115,200 baud |
| Nano master | `arduino:avr:nano:cpu=atmega328old`, old bootloader upload at 57,600 baud |
| ATtiny1614 | `megaTinyCore:megaavr:atxy4`, 16 MHz internal clock |

## Exact reusable artifacts

| Artifact | Repository path / Termux path | SHA-256 |
|---|---|---|
| Termux CLI | `tools/andruino-flash` / `~/bin/andruino-flash` | `2110a6603d3e902358950795f9d4132b23e11bc6ecc6c27bb4c37850943cd28f` |
| Seven-node test driver | `comprehensive_test.sh` / `~/comprehensive_test_nano.sh` | `ecb89411497f106ffd1e2f1cd804a6f9f0787e5551dc716cd7342243427005d8` |
| Single-node Uno test driver | `comprehensive_test_uno_node1.sh` / `~/comprehensive_test_uno_node1.sh` | `53a3fb9fe3aee25c9058cfc53dd1d71b673995c794cbae7efde87454c4b9df3c` |
| Nine-point L298N test driver | `nine_point_l298_node1.sh` / `~/nine_point_l298_node1.sh` | `2ad2a26b6e4a400eb81e4219b15da00bbb593ac214e9797883a0f69b82037685` |
| Installed debug APK | `releases/andruino-flash-debug.apk` | `7bd784f6f33aab5d48d1afab35f006c75c6347bbb658f62c2ae89c5fa97a1a5f` |
| Known-good Uno-tested master HEX | `releases/uno-jtag2updi/uno-jtag2updi-strict.hex` | `19889521fed85125927b623edbc3c55cfa6fcca74e4fa6aed625c4088e9a9c5d` |
| Nano-profile rebuild HEX (flash verified; bus failed) | `releases/nano-jtag2updi/jtag2updi-nano-old-bootloader.hex` | `8cd615fdae4d2e67c2ce5bc51669fda72df3b801a759ead9262d0223ba23b940` |

The test driver was copied to Termux and passed `bash -n`; its remote SHA-256
matched the repository file before the Nano test. For a fresh build/install, follow
[`REPRODUCING.md`](REPRODUCING.md), then copy the checked-in CLI and test driver
to the Termux paths above. Use the known-good Uno-tested HEX above to reproduce
the Nano result; the Nano-profile rebuild is retained as a failed comparison.

## Hardware test results

### Uno master — passed

On 2026-11-13, the Uno with a CH340 adapter (VID `1a86`, PID `7523`) discovered
nodes 1–7 and all seven answered PING. The comprehensive run rebuilt and
flashed each matching `BlinkNodeN.ino`, and AVRDUDE verified 6,140 flash bytes
for every node. The run reported 175 successful checks covering STATUS,
PING/VERSION, LED/BLINK, UPDI enable/disable, valve/motor state commands,
stop, motion status, encoder/angle, and mode/selection. It completed with all
protocol/command checks passing. The STATUS reply is deliberately compact, for
example `NODE 1 STATUS COMM V=OFF M=OFF L=OFF U=ON`, to reduce serial-buffer
pressure.

The test driver has SHA-256
`ecb89411497f106ffd1e2f1cd804a6f9f0787e5551dc716cd7342243427005d8`; the
Termux copy matched this hash before the Nano run.

The Uno test verifies software protocol and stored command state only. It does
not measure physical valve/motor movement, sensor readings, or electrical
isolation of UPDI.

### Uno master with one ATtiny1614 Node 1 — passed

On 2026-11-16, `comprehensive_test_uno_node1.sh` rebuilt the strict-source Uno
master to the expected SHA-256
`19889521fed85125927b623edbc3c55cfa6fcca74e4fa6aed625c4088e9a9c5d`, flashed
it, and AVRDUDE verified all 15,216 bytes. It then discovered only Node 1,
flashed `BlinkNode1.ino`, and verified all 6,140 flash bytes. The script's
protocol/state checks passed, ending with:

```text
PASS: UNO master and single ATtiny1614 node 1 checks passed.
```

The test intentionally left the single Node 1 connected to the D5/D6 bus while
flashing the master. USB paths in the transcript are transient; use
`andruino-flash devices` to find the current path. If an old/incompatible node
responds with `ERR NODE USES 1 THROUGH 7` and prevents discovery, the verified
recovery was to flash `BlinkNode1.ino` directly with `flash-sketch` (without
`--node`) and then rerun the comprehensive script.

This is a firmware protocol/state test, not a physical sensor or actuator
test. `READ_ENCODER` returned `-999999` and `ANGLE` returned `-1`, the
`BlinkNode1` placeholder values. Valve and motor responses reflect logical
state only; no physical valve/motor movement, AS5600 readings, or UPDI
electrical isolation was validated by this run.

The separately checked-in AS5600 and L298N Node 1 sketches are extensions, not
part of this passing baseline. The AS5600 image was built and its 5,994 flash
bytes were read-back verified. Its initial reads timed out because the
megaTinyCore default I2C pins are PB1/PB0, while the sensor was wired to
PA1/PA2. After explicitly selecting the PA1/PA2 route with
`Wire.pins(PIN_PA1, PIN_PA2)`, the continuous reader returned changing valid
angles (including 2,948, 3,205, 2,955, 3,033, and 2,711 counts). The L298N
extension's nine-position cycle is recorded below.

### L298N + AS5600 Node 1 nine-position cycle — passed

On 2026-11-16, the updated strict-source Uno master was built with SHA-256
`da81995aad8792513af0e7d5a3aa28956d7c2c1abb465b91e2f9521ce31f96cd` and
AVRDUDE read-back verified all 15,648 bytes. `DISCOVER` returned only node 1;
the master mode response also reported `NODES 1`. The connected node reported
`ATTINY1614-L298N-AS5600-5`.

An initial run stopped at point 3 because the detailed `STATUS` reply grew to
96 characters at four-digit encoder counts, exceeding the master's 96-byte
buffer once space for its NUL terminator was included. Increasing the strict
master response buffer to 128 bytes fixed the failure; the same long `STATUS 1`
reply then passed.

The subsequent free-shaft cycle completed all nine unique forward stations,
issued a 500 ms valve ON/OFF pulse at each forward station, and returned
through stations 7 to 0 with the valve OFF. The largest observed station error
was 1 count (the test tolerance is 32). Final `DISCOVER` still returned only
node 1, and `STATUS`/`MOTION_STATUS` confirmed encoder count 1, `MOTION IDLE`,
motor OFF, and valve OFF. The motor was unloaded; this does not validate a
coupled mechanism or sustained operation.

That first completed cycle used the then-current shaft angle as point 0 and
reset the relative count. A subsequent run of the updated script completed
absolute-angle homing and the full forward/reverse cycle. Starting from the
previous absolute reading of 251, its 32-count forward probe established motor
direction; a 218-count forward move ended at raw angle 4095, one count from
zero and within the 8-count homing tolerance. The relative count at home was
`-708`, confirming it was not reset. The cycle then visited all nine forward
stations, returned through stations 7 to 0, and pulsed the valve at each
stopped station (17 pulses total). The largest observed station error was
5 counts. It ended at absolute angle 0 with motor and valve OFF.

Four subsequent startup attempts also passed homing, reaching raw angles 2, 1,
4094, and 4095 (1–2 counts from zero). Those runs were canceled during their
forward sweeps, so they confirm homing behavior but not additional complete
cycles. These tests used an unloaded/free shaft and do not validate a coupled
mechanism, sustained operation, or electrical isolation of UPDI.

### Nano master — passed with exact Uno-tested image

A Nano-profile rebuild of the canonical source used
`arduino:avr:nano:cpu=atmega328old`, produced 13,106 bytes with 1,676 bytes of
global memory, and passed AVRDUDE read-back verification. That image did not
communicate with the nodes: DISCOVER timed out. It is retained above as a
failed comparison.

The working image was the exact 15,216-byte `uno-jtag2updi-strict.hex` that had
previously passed AVRDUDE read-back verification on the Uno (Uno upload at
115,200 baud on 2026-11-08). Its SHA-256 was checked on the phone before use.
It was uploaded unchanged with:

```sh
andruino-flash flash-platform --usb-device /dev/bus/usb/001/014 nano \
  /storage/emulated/0/Download/uno-jtag2updi-strict.hex
```

The Nano old-bootloader profile used 57,600 baud; AVRDUDE verified all 15,216
bytes.

With the Nano at `/dev/bus/usb/001/014`, the comprehensive run then:

- discovered nodes 1–7 and passed initial PING for each;
- reflashed the matching `BlinkNode1.ino` through `BlinkNode7.ino`, with
  6,140 flash bytes read-back verified for each node;
- passed all 175 checks across STATUS, PING/VERSION, LED/BLINK, UPDI enable/
  disable, valve/motor commands, stop, motion status, encoder/angle, and
  mode/selection.

The script ended with `PASS: all seven nodes reflashed and all protocol/command
checks passed.` Afterward, a separate DISCOVER and two consecutive PING passes
for nodes 1–7 also passed. This confirms the exact Uno-tested image works on
the Nano when uploaded with Nano's bootloader rate. It does not explain why
the separate Nano-profile rebuild differs or fails; use the archived known-good
HEX for reproducibility until that difference is investigated.

As with the Uno run, the suite verifies firmware protocol and logical command
state only; it does not measure physical valve/motor movement or sensor output.
