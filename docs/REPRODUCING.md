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

### Add the JTAG2UPDI sketch

The JTAG2UPDI firmware source is maintained separately from this repository.
In Termux, download the upstream source and copy the sketch source files into
both programmer sketch folders:

```sh
git clone https://github.com/ElTangas/jtag2updi.git ~/src/jtag2updi
ROOT=/storage/emulated/0/Download/andruino
mkdir -p "$ROOT/uno/jtag2updi" "$ROOT/nano/jtag2updi"
cp -R ~/src/jtag2updi/source/. "$ROOT/uno/jtag2updi/"
cp -R ~/src/jtag2updi/source/. "$ROOT/nano/jtag2updi/"
```

The source directory contains the Arduino-recognized `jtag2updi.ino` and its
supporting files. Keep those files together. Follow the upstream instructions
for the programmer MCU, pin assignments, and disabling auto-reset if needed
for your board.

## 5. Sketch layout and board profiles

Arduino CLI expects a sketch directory and its main `.ino` to have the same
name. For example:

```text
/storage/emulated/0/Download/andruino/uno/Blink300/Blink300.ino
/storage/emulated/0/Download/andruino/nano/jtag2updi/jtag2updi.ino
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
for the application. Compilation does not include fuse programming. The helper
flashes and verifies the HEX only; it does not apply Arduino IDE upload-time
fuse writes. The tested ATtiny fuses already matched the IDE profile.

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
andruino-flash flash-sketch --platform attiny1614 --usb-device "$UNO_USB" \
  "$ROOT/attiny1614/Blink300/Blink300.ino"
```

### Nano and the ATtiny connected to it

```sh
# Flash a blink sketch to the Nano MCU.
andruino-flash flash-sketch --platform nano --usb-device "$NANO_USB" \
  "$ROOT/nano/Blink3000/Blink3000.ino"

# Put JTAG2UPDI on the Nano, then flash the ATtiny wired to that Nano.
andruino-flash flash-sketch --platform nano --usb-device "$NANO_USB" \
  "$ROOT/nano/jtag2updi/jtag2updi.ino"
andruino-flash flash-sketch --platform attiny1614 --usb-device "$NANO_USB" \
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
  --usb-device "$NANO_USB" \
  /storage/emulated/0/Download/my-sketches/Blink/Blink.ino
```

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
andruino-flash flash-platform --usb-device "$NANO_USB" attiny1614 \
  "$ROOT/attiny1614/Blink3000/Blink3000.ino.hex"
```

For batch operations, put these commands in a shell script and run it from
Termux. Review the board order: each `flash-sketch` invocation writes to the
selected board immediately.

## 8. Verify and troubleshoot

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
3,984 bytes with 715 bytes of global variables. Exact sizes can vary with
Arduino CLI/core/toolchain versions.
