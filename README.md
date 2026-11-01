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
