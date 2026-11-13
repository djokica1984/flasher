# Verified build and hardware baseline

Last recorded: 2026-11-13. This is the reproducibility record for the
seven-node ATtiny1614 system. It records both the successful exact-image Nano
test and the failed Nano-profile rebuild; those are different firmware images.

## Canonical sources

- The Uno master source is
  [`andruino/uno/jtag2updi/`](../andruino/uno/jtag2updi/). Do not compile or
  flash `jtag2updi.ino` for Nano. Nano must use the exact strict HEX below,
  uploaded with `arduino:avr:nano:cpu=atmega328old` (old bootloader, 57,600 baud).
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

### Current tracked source fingerprints

SHA-256 values for the current checked-out source files:

The original compile-time source hashes for the archived
`uno-jtag2updi-strict.hex` were not captured. Its exact binary is preserved and
identified by SHA-256 below; the source hashes in this section describe the
current repository tree and are not asserted to recreate that historical image.

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
