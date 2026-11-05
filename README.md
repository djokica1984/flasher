# Andruino Flash

Andruino Flash is an Android USB-host app that runs bundled AVRDUDE 8.3 and
exposes its command-line options through a Termux command. The USB serial
connection is opened by Android; AVRDUDE inherits the authorized USB file
descriptor and performs serial bulk transfers directly. No Termux AVRDUDE,
PTY, socket, or Java serial-data proxy is used.

The app supports USB-serial adapters recognized by
`usb-serial-for-android` and serial programmers configured in the bundled
AVRDUDE configuration. This includes common CH340/CH341, FTDI, CP210x, PL2303,
and CDC-ACM adapters. AVRDUDE programmers that require their own USB protocol
(for example, USBasp or Atmel ICE) are not supported by the current USB-serial
file-descriptor backend. A programmer/target combination must also be
electrically and protocol-compatible with the board.

For the complete from-scratch guide—including Android installation, Termux
and Arduino CLI setup, USB selection, and Uno/Nano/ATtiny1614 flashing steps—
see [docs/REPRODUCING.md](docs/REPRODUCING.md).

## Build

Requirements: Android Studio/SDK, JDK 17, Android NDK `28.2.13676358`, CMake
`3.22.1`, and Gradle 8.13 or a compatible wrapper installation. Build the ARM64
debug APK with:

```powershell
.\gradlew.bat assembleDebug
```

The APK is written to `app/build/outputs/apk/debug/app-debug.apk`.
AVRDUDE's Android USB-serial backend is in
`avrdude-upstream/src/ser_posix.c`; the GPL license is in
`avrdude-upstream/COPYING`. Gradle packages the prebuilt executable from
`app/src/main/jniLibs/arm64-v8a/libavrdude.so`.

## First use and CLI

Install the debug APK with ADB:

```powershell
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Launch **Andruino Flash**. Grant All files access if firmware is stored in
shared storage, then authorize the USB-serial device. Android may
require authorization again after reinstalling the renamed app or when a
different USB device is connected.

Copy `tools/andruino-flash` to Termux's `~/bin/`, make it executable, and ensure
that directory is on `PATH`. If you first copied the script to shared Downloads:

```sh
cp /storage/emulated/0/Download/andruino-flash ~/bin/andruino-flash
chmod +x ~/bin/andruino-flash
```

The convenience commands retain the original JTAG2UPDI/ATtiny1614 defaults:

```sh
andruino-flash signature
andruino-flash flash /storage/emulated/0/Download/firmware.hex
```

### Build and flash Arduino sketches from Termux

`andruino-flash build` uses the Arduino CLI installed in the Termux Ubuntu
proot (`proot-distro login ubuntu -- arduino-cli`). By default it builds every
`.ino` under `/storage/emulated/0/Download/andruino`, choosing the target from
its parent platform folder:

| Folder | Build target |
|---|---|
| `uno` | Arduino Uno (`arduino:avr:uno`) |
| `nano` | Arduino Nano, ATmega328P old bootloader (`arduino:avr:nano:cpu=atmega328old`) |
| `attiny1614` | ATtiny1614, megaTinyCore (`megaTinyCore:megaavr:atxy4`) |

Each successful build places `<sketch-name>.ino.hex` beside the `.ino` file.
Build all sketches, or specify one sketch directory or `.ino` file. For a
sketch outside a recognized platform folder, pass `--platform` explicitly;
the sketch contents do not identify a unique board target.

```sh
andruino-flash build
andruino-flash build /storage/emulated/0/Download/andruino/uno/Blink1000
andruino-flash build --platform uno /storage/emulated/0/Projects/Blink/Blink.ino
```

`flash-sketch` builds the selected sketch and then flashes its generated HEX
using the selected target/programmer profile. The platform is inferred from
the sketch's parent folder when possible, or can be specified with
`--platform`. `flash-platform` flashes an already-built HEX:

```sh
# List serial adapters. Paths shown here identify USB adapters, not boards.
andruino-flash devices

# Set these to the current values from `devices` and your chosen sketch tree.
UNO_USB=/dev/bus/usb/001/008
NANO_USB=/dev/bus/usb/001/009
ROOT=/storage/emulated/0/Download/andruino

# Uno itself.
andruino-flash flash-sketch --platform uno --usb-device "$UNO_USB" \
  "$ROOT/uno/Blink1000/Blink1000.ino"

# Nano itself.
andruino-flash flash-sketch --platform nano --usb-device "$NANO_USB" \
  "$ROOT/nano/Blink1000/Blink1000.ino"

# Uno's ATtiny: flash programmer to Uno, then flash target through Uno.
andruino-flash flash-sketch --platform uno --usb-device "$UNO_USB" \
  "$ROOT/uno/jtag2updi/jtag2updi.ino"
andruino-flash flash-sketch --platform attiny1614 --usb-device "$UNO_USB" \
  "$ROOT/attiny1614/Blink1000/Blink1000.ino"

# Nano's ATtiny: flash programmer to Nano, then flash target through Nano.
andruino-flash flash-sketch --platform nano --usb-device "$NANO_USB" \
  "$ROOT/nano/jtag2updi/jtag2updi.ino"
andruino-flash flash-sketch --platform attiny1614 --usb-device "$NANO_USB" \
  "$ROOT/attiny1614/Blink1000/Blink1000.ino"

# Equivalent commands for HEX files already built in those directories.
andruino-flash flash-platform --usb-device "$UNO_USB" uno \
  "$ROOT/uno/Blink1000/Blink1000.ino.hex"
andruino-flash flash-platform --usb-device "$NANO_USB" nano \
  "$ROOT/nano/Blink1000/Blink1000.ino.hex"
andruino-flash flash-platform --usb-device "$UNO_USB" uno \
  "$ROOT/uno/jtag2updi/jtag2updi.ino.hex"
andruino-flash flash-platform --usb-device "$UNO_USB" attiny1614 \
  "$ROOT/attiny1614/Blink1000/Blink1000.ino.hex"
andruino-flash flash-platform --usb-device "$NANO_USB" nano \
  "$ROOT/nano/jtag2updi/jtag2updi.ino.hex"
andruino-flash flash-platform --usb-device "$NANO_USB" attiny1614 \
  "$ROOT/attiny1614/Blink1000/Blink1000.ino.hex"
```

### Uno control bus

The optional Uno JTAG2UPDI build supports up to three addressed ATtiny1614
nodes on a half-duplex bus. After installing the modified Uno and node firmware, issue
commands through the Uno's explicit USB path:

```sh
andruino-flash bus --usb-device "$UNO_USB" PING 1
andruino-flash bus --usb-device "$UNO_USB" STATUS 2
andruino-flash bus --usb-device "$UNO_USB" BLINK 2 200
andruino-flash bus --usb-device "$UNO_USB" PING 3
andruino-flash bus --usb-device "$UNO_USB" SELECT 1
andruino-flash bus --usb-device "$UNO_USB" MODE COMM
```

`MODE FLASH` and `MODE COMM` set the system-wide state on the Uno and every
configured node. `SELECT 1`, `SELECT 2`, or
`SELECT 3` sends the target ID to every configured node; each node records
whether it is selected without changing its mode. These
software states do **not** electrically isolate either UPDI pin or select the
physical UPDI connection. Continue manually switching UPDI to the target being
programmed. Enter system-wide `MODE FLASH`, use `SELECT 1`, `SELECT 2`, or
`SELECT 3` to record the intended target at every node, and use `MODE COMM` to
leave flashing mode. In FLASH mode the selected node's LED is solid ON and
the other nodes' LEDs are OFF; in COMM mode each node retains its configured
blink behavior.
For LEDs wired from VDD through a resistor to PA5, the output is active-low:
PA5 LOW lights the LED and PA5 HIGH turns it off.
On the standard 14-pin ATtiny1614 package, the bus uses PB3/PB2 (physical
pins 6/7), and the active-low LED output uses PA5 (physical pin 3). PB0
(physical pin 9) is the inverted electrical level of PA5 only in FLASH mode:
PB0 is HIGH while PA5 is LOW (LED lit), and PB0 is LOW while PA5 is HIGH (LED off).
Use PB0 only as a logic signal for a
high-impedance input, not directly on the UPDI bus.
Mode and selection state persist across resets in EEPROM. See the
[control-bus wiring and complete command reference](docs/REPRODUCING.md#8-uno-control-bus-for-up-to-three-attiny1614-nodes).

The Uno profile uses the Arduino bootloader at 115200 baud. The Nano profile
uses the old bootloader at 57600 baud. The ATtiny1614 profile uses JTAG2UPDI at
115200 baud. `--platform` selects the compile target and AVRDUDE protocol;
`--usb-device` selects the physical USB serial adapter the app opens. Thus an
ATtiny wired to the Nano uses platform `attiny1614` and the Nano's USB path,
not platform `nano`. The matching JTAG2UPDI sketch must already be on the
selected board. A USB path is not a permanent board identity; check
`andruino-flash devices` after reconnecting.

Sketch and HEX paths are command arguments and can be changed independently of
the target. Set `ROOT` in the examples to use a different sketch tree. For
build-all, set `ANDRUINO_SKETCH_ROOT` to change the default sketch tree.
`ANDRUINO_RESULT_DIR` changes where CLI logs are written.

The ATtiny build matches the supplied Arduino IDE profile: chip 1614, 16 MHz
internal clock, BOD 1.8 V and disabled, EEPROM retained, millis enabled,
8 ms startup, Wire master/slave, default printf/PWM, all-pin attach interrupts,
and disabled WDT timeout/window. The non-Optiboot `atxy4` profile reserves the
full 16 KB flash for the sketch, matching the PC build log. Arduino IDE's
upload log also writes the ATtiny fuses; compiling a HEX does not embed fuse
settings in it. The CLI flash command writes only flash. The connected ATtiny's
fuses were read and already match the IDE upload values, so no fuse changes
were needed.

The Arduino CLI runs in Termux's Ubuntu proot; it is not bundled into the
Android APK. AVRDUDE flashing still runs through the Android app and its direct
USB file-descriptor connection.

For other AVRDUDE targets and options, `run` passes arguments through to the
bundled AVRDUDE:

```sh
andruino-flash run -- -c arduino -b 115200 -p m328p \
  -U flash:w:/storage/emulated/0/Download/uno.hex:i -v
```

That example is for an Arduino Uno with a compatible USB-serial connection and
bootloader. A classic Nano with an older bootloader often needs `-b 57600`
instead. Use the `-c` programmer ID and `-p` part ID from the bundled
`avrdude.conf`; all other AVRDUDE command-line options, including memory
operations (`-U`), erase (`-e`), disabling verification (`-V`), verbosity, and fuse
operations, can be supplied after `run --`.

Android owns the USB port, so the app always sets AVRDUDE's port to `COM` and
rejects attempts to select a different `-P` port. The app applies the supplied
`-b` baud rate to the Android USB-serial driver before launching AVRDUDE. If
omitted, it defaults to 115200. The application ID is now
`com.example.andruinoflash`; the earlier ATtiny-only app, if installed, is a
separate Android package.
If multiple supported USB-serial adapters are connected, the app uses the first
one reported by Android; connect only the intended programmer.

The app writes output and result status to
`Download/AndruinoFlash/<request-id>.log`, which the Termux script streams to
the terminal. USB access is granted to the Android app, not to Termux or SSH.
Flashing overwrites the selected AVR memory; use a known-good firmware image.
