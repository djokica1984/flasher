# Verified build and hardware baseline

Last recorded: 2026-11-13. This is the reproducibility record for the
seven-node ATtiny1614 system. It distinguishes verified behavior from open
issues; the Nano result below is a failure, not a claim that the Nano bus is
working.

## Canonical sources

- The one shared master sketch is
  [`andruino/uno/jtag2updi/`](../andruino/uno/jtag2updi/). Compile that source
  with `--platform uno` or `--platform nano`; Nano uses
  `arduino:avr:nano:cpu=atmega328old` (old bootloader, 57,600 baud). Do not
  treat `andruino/nano/jtag2updi/` as a second source of truth.
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

### Critical source fingerprints

SHA-256 values for the tested source files:

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
| Termux CLI | `tools/andruino-flash` / `~/bin/andruino-flash` | `d0962ea5ed65ed88dace9989a9e508609de08214f0a4ed9cae4580d923aed643` |
| Seven-node test driver | `comprehensive_test.sh` / `~/comprehensive_test_nano.sh` | `ecb89411497f106ffd1e2f1cd804a6f9f0787e5551dc716cd7342243427005d8` |
| Installed debug APK | `releases/andruino-flash-debug.apk` | `7bd784f6f33aab5d48d1afab35f006c75c6347bbb658f62c2ae89c5fa97a1a5f` |
| Uno JTAG2UPDI HEX | `releases/uno-jtag2updi/jtag2updi-fixed.hex` | `19cc9817511668e7c8f416b3a98e9ec57fa4bbf329e0ced53af1ce86972f2e01` |
| Nano old-bootloader HEX | `releases/nano-jtag2updi/jtag2updi-nano-old-bootloader.hex` | `8cd615fdae4d2e67c2ce5bc51669fda72df3b801a759ead9262d0223ba23b940` |

The test driver was copied to Termux and passed `bash -n`; the remote copy's
SHA-256 matched the repository file. For a fresh build/install, follow
[`REPRODUCING.md`](REPRODUCING.md), then copy the checked-in CLI and test driver
to the Termux paths above. The release Nano HEX is the exact image used for
the recorded Nano flash verification.

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

The current test driver has SHA-256
`ecb89411497f106ffd1e2f1cd804a6f9f0787e5551dc716cd7342243427005d8` and was
synced and syntax-checked after the successful hardware run. The earlier
device-side copy's hash was not captured at run time, so the current checked-in
driver is the exact version to use for future reproductions; do not infer an
uncaptured device-side hash for the historical run.

The Uno test verifies software protocol and stored command state only. It does
not measure physical valve/motor movement, sensor readings, or electrical
isolation of UPDI.

### Nano master — flashed, bus not working

The canonical Uno master source above was compiled for Nano using
`arduino:avr:nano:cpu=atmega328old`. The resulting image was 13,106 bytes with
1,676 bytes of global memory. It was uploaded using the old bootloader and
AVRDUDE read-back verified all 13,106 bytes; the exact HEX is the release
artifact listed above.

After reconnecting the wiring as used with the Uno, the Nano appeared at
`/dev/bus/usb/001/014`. DISCOVER timed out, and PING and STATUS to nodes 1–7
all returned `ERR NO RESPONSE FROM NODE n`. The Uno-tested node firmware
therefore remains known-good with the Uno, but the Nano control bus is not
restored. The cause has not been proven; do not treat the matching source,
successful Nano flash verification, or USB enumeration as evidence that the
bus works. Use the same documented wiring and isolate the Nano D5 bus signal,
common ground, and timing against the working Uno baseline before claiming a
fix.
