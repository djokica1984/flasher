# Reproducing the Andruino Flash setup

This guide covers a fresh checkout through compiling and flashing firmware to:

1. An Arduino Uno itself.
2. An Arduino Nano itself (ATmega328P, old bootloader).
3. An ATtiny1614 connected to an Uno running JTAG2UPDI.
4. An ATtiny1614 connected to a Nano running JTAG2UPDI.

The tested phone was a Samsung SM-A556B (ARM64). Commands assume Android
shared storage is `/storage/emulated/0/Download`, Termux is installed, and the
phone supports USB host/OTG. Substitute your own storage paths and currently
enumerated USB paths as needed.

## What runs where

- **Android app:** bundles AVRDUDE and uses Android USB-host APIs to open the
  selected USB-serial adapter. AVRDUDE receives the authorized USB file
  descriptor directly; it does not communicate through a Termux AVRDUDE,
  socket, PTY, or Java serial-data relay.
- **Termux:** provides the `andruino-flash` command and forwards operations to
  the app's foreground service.
- **Ubuntu proot in Termux:** runs Arduino CLI and the board cores to compile
  sketches. Arduino CLI is not inside the APK.
- **USB adapter path:** identifies the serial adapter Android has opened; it
  is not the selected AVR target. `--platform` chooses the target and matching
  compile/programming profile.

The app's native AVRDUDE backend is currently ARM64-only. It supports
USB-to-serial adapters recognized by `usb-serial-for-android` and does not
currently support programmers that require their own USB protocol, such as a
USBasp or Atmel ICE.

## Hardware prerequisites

- Android phone with USB host/OTG support.
- Powered USB-C OTG hub or adapter and data-capable USB cables.
- Uno and/or Nano with a supported USB-serial interface. The tested boards
  used CH340 devices (VID `1a86`, PID `7523`).
- For ATtiny1614, the selected Uno or Nano must first be programmed with
  JTAG2UPDI. Connect the ATtiny1614 to that programmer according to the
  JTAG2UPDI wiring instructions, with compatible target/programmer voltages
  and a common ground.

The JTAG2UPDI project warns that logic levels must be compatible and that the
UPDI pin on modern tinyAVR devices must not be assumed to tolerate high-voltage
programming. Read the upstream project's wiring and safety notes before
connecting hardware:
[ElTangas/jtag2updi](https://github.com/ElTangas/jtag2updi).

## 1. Build and install the Android app

This step is only needed if installing from source; if the debug APK is
already installed, continue to Termux setup.

On a Windows build computer, install:

- Android Studio and Android SDK Platform 35.
- Android NDK `28.2.13676358`.
- CMake `3.22.1`.
- 64-bit JDK 17.
- Android SDK Platform Tools (`adb`).

Clone and build from PowerShell:

```powershell
git clone https://github.com/djokica1984/flasher.git
Set-Location flasher
.\gradlew.bat :app:assembleDebug
```

The ARM64 debug APK is:

```text
app\build\outputs\apk\debug\app-debug.apk
```

Enable Developer Options and USB debugging on the phone, connect it to the PC,
approve the phone's ADB authorization dialog, and install:

```powershell
adb devices -l
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

Disconnect the phone from the PC, attach the USB OTG hub and serial adapter(s),
then launch **Andruino Flash**. Tap **Grant all-files access** and enable the
permission in Android Settings. This allows the Termux command to access
firmware under shared storage and write operation logs. Tap **Authorize USB
device** for a connected adapter.

Android USB authorization is managed per attached USB device/connection. A
different adapter, a reconnect, or an Android permission reset can cause a new
authorization prompt. Approve it when it appears during a CLI command. Do not
assume one approval permanently authorizes every adapter.

## 2. Install Termux and prepare storage

Install Termux from F-Droid or the official Termux GitHub releases. Use one
installation source consistently; do not mix Termux or Termux add-ons from
different sources. In Termux:

```sh
pkg update
pkg upgrade
pkg install proot-distro git coreutils findutils gawk
termux-setup-storage
mkdir -p ~/bin
```

Approve Termux's Android storage permission. Confirm the shared Download
directory is available:

```sh
ls -ld ~/storage/downloads
ls /storage/emulated/0/Download
```

Install Ubuntu in proot and its prerequisites:

```sh
proot-distro install ubuntu
proot-distro login ubuntu -- bash -lc \
  'apt update && apt install -y ca-certificates curl'
```

## 3. Install Arduino CLI and board packages

Install Arduino CLI `1.2.2` in Ubuntu proot (the version used in the tested
phone environment):

```sh
proot-distro login ubuntu -- bash -lc \
  'curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=/usr/local/bin sh -s 1.2.2'
```

Add the megaTinyCore index used by this setup and install the board cores:

```sh
proot-distro login ubuntu -- bash -lc '
  arduino-cli version
  arduino-cli config init
  arduino-cli config add board_manager.additional_urls \
    https://drazzy.com/package_drazzy.com_index.json
  arduino-cli core update-index
  arduino-cli core install arduino:avr@1.8.8
  arduino-cli core install megaTinyCore:megaavr@2.6.11
  arduino-cli core list
'
```

Verified core versions are Arduino AVR `1.8.8` and megaTinyCore `2.6.11`.
Package indexes can change; if the pinned megaTinyCore version is no longer
available, install the available compatible version, record it, and compare
the compiled output with the reference build results below.

Confirm the CLI is available in the proot:

```sh
proot-distro login ubuntu -- arduino-cli version
proot-distro login ubuntu -- arduino-cli core list
```

The build helper invokes Arduino CLI with
`proot-distro login ubuntu -- arduino-cli`. Keep the `.ino` source and generated
HEX files in storage that both Termux and the proot can access.

## 4. Install the Termux command

From the PC checkout, push the helper to shared Downloads:

```powershell
adb push tools/andruino-flash /sdcard/Download/andruino-flash
```

In Termux, install it in `~/bin` and add that directory to the shell `PATH`:

```sh
cp ~/storage/downloads/andruino-flash ~/bin/andruino-flash
chmod 700 ~/bin/andruino-flash
grep -q 'HOME/bin' ~/.bashrc || echo 'export PATH="$HOME/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

Optionally copy the sample sketches committed in this repository:

```powershell
adb push andruino /sdcard/Download/
```

The committed samples include Blink300 and Blink3000 for Uno, Nano, and
ATtiny1614. The earlier Blink1000/Blink200 examples are part of the existing
phone sketch tree but are not required for this guide.

### Shared JTAG2UPDI firmware

The canonical JTAG2UPDI source, including the control-bus extension, is
`$ROOT/uno/jtag2updi`; use it for the Uno. **Do not build or flash that source
for Nano.** For Nano, use only the hash-verified `uno-jtag2updi-strict.hex`
artifact below. The CLI rejects Nano JTAG2UPDI source builds and refuses Nano
JTAG2UPDI HEX files unless their hash matches that tested artifact. Other Nano
sketches use `arduino:avr:nano:cpu=atmega328old` and the old-bootloader upload
rate.
The same D5 control-bus and D6 UPDI wiring passed the seven-node test with the
Uno-tested master HEX uploaded to a Nano at the old-bootloader rate. A separate
Nano-profile rebuild failed bus communication; use the archived known-good HEX
and see the [verified baseline](VERIFIED_BASELINE.md).

## 5. Sketch layout and board profiles

Arduino CLI expects a sketch directory and its main `.ino` to have the same
name. For example:

```text
/storage/emulated/0/Download/andruino/uno/Blink300/Blink300.ino
/storage/emulated/0/Download/andruino/uno/jtag2updi/jtag2updi.ino
/storage/emulated/0/Download/andruino/attiny1614/Blink3000/Blink3000.ino
```

The helper's target profiles:

| `--platform` | Arduino CLI FQBN | AVRDUDE configuration |
|---|---|---|
| `uno` | `arduino:avr:uno` | `-c arduino -b 115200 -p m328p` |
| `nano` | `arduino:avr:nano:cpu=atmega328old` | `-c arduino -b 57600 -p m328p` |
| `attiny1614` | `megaTinyCore:megaavr:atxy4` | `-c jtag2updi -b 115200 -p t1614` |

For ATtiny1614, the helper also sets the Arduino IDE-matched build options:

```text
chip=1614,clock=16internal,bodvoltage=1v8,bodmode=disabled,
eesave=enable,millis=enabled,startuptime=8,wiremode=mors,
printf=default,PWMmux=default,attach=allenabled,
WDTtimeout=disabled,WDTwindow=disabled
```

This uses the non-Optiboot `atxy4` profile and reserves the full 16 KB flash
for the application. The helper programs the megaTinyCore 16 MHz internal-clock
fuses together with the flash image. Do not omit this fuse step: AVRDUDE flash-only
writes will leave a new ATtiny running at its factory oscillator setting, while
the UART code in the HEX is compiled for 16 MHz, producing invalid 2400-baud
communication. The ATtiny1614 fuse image is: fuse0=`0x00`, fuse1=`0x00`,
fuse2=`0x01`, fuse5=`0xC6`, fuse6=`0x04`, fuse7=`0x00`, fuse8=`0x00`.

## 6. Identify the USB adapters

Connect the adapters to the phone in USB host mode and run:

```sh
andruino-flash devices
```

Example:

```text
USB_DEVICE path=/dev/bus/usb/001/008 VID=1a86 PID=7523 ...
USB_DEVICE path=/dev/bus/usb/001/009 VID=1a86 PID=7523 ...
```

Record the current paths in shell variables; refresh them after reconnecting:

```sh
UNO_USB=/dev/bus/usb/001/008
NANO_USB=/dev/bus/usb/001/009
ROOT=/storage/emulated/0/Download/andruino
```

The path belongs to the USB adapter, not an Arduino board identity. Both
tested boards use identical CH340 VID/PID values, so VID/PID alone cannot tell
Uno from Nano. Verify a suspected board with a read-only AVR signature request
at the correct bootloader baud:

```sh
andruino-flash run --usb-device "$UNO_USB" -- \
  -c arduino -b 115200 -p m328p -U signature:r:-:h
andruino-flash run --usb-device "$NANO_USB" -- \
  -c arduino -b 57600 -p m328p -U signature:r:-:h
```

An ATmega328P should report `0x1e,0x95,0x0f`. If more than one supported
adapter is attached, always pass `--usb-device`; the app rejects ambiguous
selection rather than guessing. Android may request authorization for each
adapter.

## 7. Build and flash each device

`--platform` is the target board/profile. `--usb-device` is the adapter the
app opens. These are deliberately independent:

- To program the Uno MCU, select `--platform uno` and the Uno USB path.
- To program the Nano MCU, select `--platform nano` and the Nano USB path.
- To program an ATtiny connected to an Uno, first flash JTAG2UPDI onto the
  Uno, then select `--platform attiny1614` with the Uno USB path.
- To program an ATtiny connected to a Nano, first flash JTAG2UPDI onto the
  Nano, then select `--platform attiny1614` with the Nano USB path.

### Uno and the ATtiny connected to it

```sh
# Flash a blink sketch to the Uno MCU.
andruino-flash flash-sketch --platform uno --usb-device "$UNO_USB" \
  "$ROOT/uno/Blink300/Blink300.ino"

# Put JTAG2UPDI on the Uno, then flash the ATtiny wired to that Uno.
andruino-flash flash-sketch --platform uno --usb-device "$UNO_USB" \
  "$ROOT/uno/jtag2updi/jtag2updi.ino"
andruino-flash flash-sketch --platform attiny1614 --node 1 --usb-device "$UNO_USB" \
  "$ROOT/attiny1614/Blink300/Blink300.ino"
```

### Nano and the ATtiny connected to it

```sh
# Flash a blink sketch to the Nano MCU.
andruino-flash flash-sketch --platform nano --usb-device "$NANO_USB" \
  "$ROOT/nano/Blink3000/Blink3000.ino"

# Put the exact Uno-tested JTAG2UPDI image on the Nano using its old-bootloader profile.
# First copy releases/uno-jtag2updi/uno-jtag2updi-strict.hex to this path.
MASTER_HEX=/storage/emulated/0/Download/uno-jtag2updi-strict.hex
echo "19889521fed85125927b623edbc3c55cfa6fcca74e4fa6aed625c4088e9a9c5d  $MASTER_HEX" | sha256sum -c -
andruino-flash flash-platform --usb-device "$NANO_USB" nano "$MASTER_HEX"
andruino-flash flash-sketch --platform attiny1614 --node 1 --usb-device "$NANO_USB" \
  "$ROOT/attiny1614/Blink3000/Blink3000.ino"
```

**Important:** flashing a Blink sketch onto an Uno or Nano replaces the
JTAG2UPDI firmware. Flash that board's `jtag2updi.ino` again before using it
to program its ATtiny.

`flash-sketch` compiles the given source and flashes the resulting HEX beside
it. For example, a sketch can be outside the named platform folders as long as
the source filename matches its sketch directory; pass `--platform`
explicitly:

```sh
andruino-flash flash-sketch --platform attiny1614 \
  --node 1 \
  --usb-device "$NANO_USB" \
  /storage/emulated/0/Download/my-sketches/Blink/Blink.ino
```

For an ATtiny1614, `--node N` puts the master in FLASH mode and selects node
`N` before programming. Without `--node`, select the target first with
`andruino-flash bus ... MODE FLASH` and `SELECT N`; the app verifies that the
requested node matches the master's saved selection before enabling its UPDI
gate. This prevents accidentally programming a different node.

To compile without flashing:

```sh
andruino-flash build --platform attiny1614 \
  /storage/emulated/0/Download/my-sketches/Blink/Blink.ino
```

To flash a prebuilt HEX instead, specify the same target and adapter:

```sh
andruino-flash flash-platform --usb-device "$UNO_USB" uno \
  "$ROOT/uno/Blink300/Blink300.ino.hex"
andruino-flash flash-platform --usb-device "$NANO_USB" nano \
  "$ROOT/nano/Blink3000/Blink3000.ino.hex"
andruino-flash flash-platform --node 1 --usb-device "$NANO_USB" attiny1614 \
  "$ROOT/attiny1614/Blink3000/Blink3000.ino.hex"
```

For batch operations, put these commands in a shell script and run it from
Termux. Review the board order: each `flash-sketch` invocation writes to the
selected board immediately.

## 8. Uno/Nano control bus for seven ATtiny1614 nodes

The verified bus firmware supports either the **Uno or classic Nano
JTAG2UPDI master** and up to seven ATtiny1614 nodes. The full test passed on
both boards when the exact Uno-tested master HEX was used on Nano with the
old-bootloader upload profile. A separate Nano-profile rebuild did not
communicate; use the archived known-good HEX and see the
[verified baseline](VERIFIED_BASELINE.md). Connect master D5 to each node's PB3/RX signal through its own
1 kOhm series resistor; connect each node's PB2/TX signal to the same bus
through its own 1 kOhm series resistor. Use the pinout for your exact ATtiny
package or breakout board to locate PB3 and PB2; do not rely on package pin
numbers from an example. Connect all grounds together.
For an indicator LED wired from VDD through a current-limiting resistor to
PA5, the output is active-low: PA5 LOW lights the LED, and PA5 HIGH turns it
off. In FLASH mode the selected node LED is solid ON and the other is OFF.
On the standard 14-pin ATtiny1614 package, the node bus uses PB3/RX and PB2/TX
(physical pins 6/7), and the active-low LED uses PA5 (physical pin 3). PB0
(physical pin 9) is the inverted electrical level of PA5 only in FLASH mode:
PB0 is HIGH while PA5 is LOW (LED lit), and PB0 is LOW while PA5 is HIGH (LED off).
Treat PB0 as a logic output; do not
connect it directly to the UPDI bus.
`STATUS` reports mode plus valve (`V`), motor (`M`), LED (`L`), and UPDI (`U`)
states in a compact form; for example, `NODE 1 STATUS COMM V=OFF M=OFF
L=OFF U=ON`. The LED value confirms the node's logic but does not measure the
physical pin voltage.
The updated node firmware identifies itself as
`ANDRUINO-ATTINY1614-BUS-4` with the `VERSION` command.
Install node firmware from `attiny1614/BlinkNode1` through `BlinkNode7`. Each
sketch uses the shared
`attiny1614/libraries/AndruinoBusNode` library; keep the `libraries` directory
under `$ROOT/attiny1614` so the Termux build helper can find it.

The master uses D5 at 2400 baud and the Android app sends control
requests to the master's USB serial adapter at 19200 baud. Because JTAG2UPDI
provides its own `main()` instead of Arduino's normal core startup, the master
initializes the Arduino millisecond timer and enables interrupts only while
processing a bus command. This supports SoftwareSerial reception without
leaving periodic interrupts enabled during UPDI programming. Build and install
the modified Uno programmer and all seven nodes:

```sh
andruino-flash flash-sketch --platform uno --usb-device "$UNO_USB" \
  "$ROOT/uno/jtag2updi/jtag2updi.ino"
andruino-flash flash-sketch --platform attiny1614 --usb-device "$UNO_USB" \
  "$ROOT/attiny1614/BlinkNode1/BlinkNode1.ino"
andruino-flash flash-sketch --platform attiny1614 --usb-device "$UNO_USB" \
  "$ROOT/attiny1614/BlinkNode2/BlinkNode2.ino"
andruino-flash flash-sketch --platform attiny1614 --usb-device "$UNO_USB" \
  "$ROOT/attiny1614/BlinkNode3/BlinkNode3.ino"
andruino-flash flash-sketch --platform attiny1614 --usb-device "$UNO_USB" \
  "$ROOT/attiny1614/BlinkNode4/BlinkNode4.ino"
andruino-flash flash-sketch --platform attiny1614 --usb-device "$UNO_USB" \
  "$ROOT/attiny1614/BlinkNode5/BlinkNode5.ino"
andruino-flash flash-sketch --platform attiny1614 --usb-device "$UNO_USB" \
  "$ROOT/attiny1614/BlinkNode6/BlinkNode6.ino"
andruino-flash flash-sketch --platform attiny1614 --usb-device "$UNO_USB" \
  "$ROOT/attiny1614/BlinkNode7/BlinkNode7.ino"
```

Use the same commands with the USB path for whichever master is connected:

```sh
MASTER_USB="$UNO_USB"   # set to "$NANO_USB" when using the Nano
andruino-flash bus --usb-device "$MASTER_USB" PING 1
andruino-flash bus --usb-device "$MASTER_USB" STATUS 2
andruino-flash bus --usb-device "$MASTER_USB" VERSION 1
andruino-flash bus --usb-device "$MASTER_USB" BLINK 2 200
andruino-flash bus --usb-device "$MASTER_USB" BLINK 1 1000
andruino-flash bus --usb-device "$MASTER_USB" PING 3
andruino-flash bus --usb-device "$MASTER_USB" SELECT 1
andruino-flash bus --usb-device "$MASTER_USB" SELECT 3
andruino-flash bus --usb-device "$MASTER_USB" MODE COMM
andruino-flash bus --usb-device "$MASTER_USB" MODE FLASH
```

`MODE FLASH` sets the master and all installed nodes to system-wide FLASH mode;
`MODE COMM` sets them back to communication mode. These mode commands
are sent individually to each addressed node to avoid multiple simultaneous
replies on the half-duplex bus. `SELECT 1` through `SELECT 7` informs every
installed node of the chosen target. Each node records whether its own
address matches the selection, while all remain in the current system mode.
Install and flash firmware for each connected node before using system-mode
or selection commands: the master iterates over active node addresses 1–7, so
an absent active node causes those commands to report an error. For example:

```sh
# Enter system-wide flash mode, then select node 1.
andruino-flash bus --usb-device "$MASTER_USB" MODE FLASH
andruino-flash bus --usb-device "$MASTER_USB" SELECT 1
andruino-flash bus --usb-device "$MASTER_USB" STATUS 1
andruino-flash bus --usb-device "$MASTER_USB" STATUS 2
andruino-flash bus --usb-device "$MASTER_USB" STATUS 3

# After manually moving UPDI to a target, select that node before flashing.
andruino-flash bus --usb-device "$MASTER_USB" SELECT 2
andruino-flash bus --usb-device "$MASTER_USB" SELECT 3

# Exit flash mode only when the system should resume normal operation.
andruino-flash bus --usb-device "$MASTER_USB" MODE COMM
```

In FLASH mode, all nodes should report `MODE FLASH`; the selected
node reports `SELECTION SELECTED`, and the others report
`SELECTION NOT_SELECTED`. The selected node's LED is solid ON and the other
nodes' LEDs are OFF. In COMM mode, each node's LED follows its configured blink
intervals. `PING`, `STATUS`, `VERSION`, and mode/selection control remain
available while in FLASH mode; blink changes are only accepted while the
addressed node is in COMM mode.

The master and nodes persist system mode and selection in EEPROM, so programming
or resetting a selected node does not silently put it back in COMM mode or
forget which target was selected. Each command prints the master response as
`BUS_RESPONSE ...`; absent nodes or malformed commands return an error. The
master retries each addressed bus request up to three times and ignores stale or
other-node response lines. The Android service allows up to twenty seconds for a
command response. It validates each response against the issued command;
unrelated or stale lines are discarded and cannot produce a false success.

**These are software states only, not UPDI electrical isolation or a hardware
target selector.** This implementation does not make it safe to leave both
UPDI pins connected to a shared UPDI line. Continue manually switching the
UPDI connection to the target being programmed. EEPROM persistence depends on
the device's EEPROM-preserve setting during chip erase; the supplied ATtiny
profile enables EEPROM preservation.

### Full seven-node communication and reflash test

With the address-specific `BlinkNode1` through `BlinkNode7` sketches present
under `$ROOT/attiny1614`, install the updated CLI and copy
`comprehensive_test.sh` to Termux as `~/comprehensive_test_nano.sh`. Then run:

```sh
chmod 700 ~/comprehensive_test_nano.sh
NANO_USB=/dev/bus/usb/001/011
~/comprehensive_test_nano.sh "$NANO_USB"
```

Replace the example USB path with the current path from `andruino-flash devices`.
The script discovers all seven nodes, verifies initial PING,
rebuilds and reflashes each address-specific sketch with AVRDUDE read-back
verification, then tests status/version, LED/blink, UPDI gating, virtual
valve/motor commands, stop, motion status, and encoder responses. It restores
COMM mode and turns the LEDs off afterward. Keep actuator loads disconnected:
the generic `BlinkNodeN` firmware only verifies logical command responses and
does not validate physical motors, valves, or sensors. The test runs once; it
flashes each node once. Use `--no-flash` for later communication-only reruns.

To rerun only communications and command checks after a failed test, without
reflashing the nodes, pass `--no-flash` before the USB path:

```sh
~/comprehensive_test_nano.sh --no-flash "$NANO_USB"
```

The checked-in [verified baseline](VERIFIED_BASELINE.md) records the exact
source and release hashes, tool versions, and the latest tested results for
both master boards.

## 9. Verify and troubleshoot

A successful compile prints `Built ...` and the HEX path. A successful flash
ends with `RESULT success` and AVRDUDE output such as:

```text
688 bytes of flash verified
```

The phone-side operation log is written under
`/storage/emulated/0/Download/AndruinoFlash/` by default. Change its location
with `ANDRUINO_RESULT_DIR`.

Common problems:

- **No supported serial adapter:** check USB OTG/host mode, cable/hub power,
  USB connection, and the Android permission; then run `andruino-flash devices`.
- **Multiple adapters error:** pass `--usb-device` with the current path from
  `devices`. Never assume that a previous `/dev/bus/usb/...` path still refers
  to the same adapter after reconnecting.
- **`programmer is not responding`:** verify adapter selection, target
  platform, Nano bootloader baud, and whether the selected board is currently
  running its bootloader or JTAG2UPDI firmware as required.
- **ATtiny sign-on or UPDI error:** confirm common ground, target power,
  compatible logic levels, UPDI wiring, and JTAG2UPDI firmware on the selected
  programmer. Try `andruino-flash signature --usb-device "$UNO_USB"` (or
  `$NANO_USB`) before writing flash.
- **Compiled and verified, but device behavior is wrong:** verification only
  proves flash contents match the HEX. Check sketch pin selection, wiring,
  clock/fuse configuration, and LED polarity separately.
- **`tail: cannot open ...log` appears briefly:** the Termux CLI starts
  listening just before Android creates the log. If AVRDUDE output follows and
  the command ends in `RESULT success`, this is a startup timing message.
- **AVRDUDE config-version warning:** if the write and verify complete and
  `RESULT success` is reported, this warning did not prevent that operation.

## Reference versions and build results

The tested environment used:

| Component | Version/profile |
|---|---|
| Android | Android 16, ARM64 phone |
| AVRDUDE | Bundled AVRDUDE 8.3 build |
| Arduino CLI | 1.2.2 |
| Arduino AVR Boards | 1.8.8 |
| megaTinyCore | 2.6.11 |
| Uno | `arduino:avr:uno`, 115200 baud |
| Nano | `arduino:avr:nano:cpu=atmega328old`, 57600 baud |
| ATtiny1614 | `megaTinyCore:megaavr:atxy4`, 16 MHz internal |

Reference Blink builds were about 922-924 bytes for Uno/Nano and 686-688 bytes
for ATtiny1614, with 9-10 bytes of global variables. JTAG2UPDI builds were
3,984 bytes with 715 bytes of global variables in an earlier, pre-control-bus
build. A separate control-bus Nano-profile rebuild was 13,106 bytes with 1,676
bytes of global memory, but it did not communicate with the nodes. The
15,216-byte Uno-tested master HEX did pass the full Nano test when uploaded at
the Nano old-bootloader rate. See the verified baseline for exact hashes and
the complete test record.
