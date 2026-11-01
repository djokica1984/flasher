# ATtiny UPDI Flash

An Android USB-host app for flashing an ATtiny1614 through an Arduino Uno running
JTAG2UPDI. AVRDUDE 8.3 is bundled in the APK; no Termux AVRDUDE package is used
at runtime. Android USB APIs open and initialize the Uno's CH340 adapter
(`1a86:7523`). The app passes that USB file descriptor to the bundled AVRDUDE
process, whose Android serial backend uses USB bulk/control transfers directly.
There is no PTY, socket, or Java serial read/write bridge.

## Build

Requirements: Android Studio/SDK, JDK 17, Android NDK `28.2.13676358`, CMake
`3.22.1`, and Gradle 8.13 or a compatible wrapper installation. Build the ARM64
debug APK with:

```powershell
.\gradlew.bat assembleDebug
```

The APK is written to `app/build/outputs/apk/debug/app-debug.apk`.
The bundled `arm64-v8a` AVRDUDE executable was built from `avrdude-upstream/`;
the Android CH340 USB-FD backend is in `avrdude-upstream/src/ser_posix.c`.
AVRDUDE's GPL license is in `avrdude-upstream/COPYING`. Gradle packages the
prebuilt executable from `app/src/main/jniLibs/arm64-v8a/libavrdude.so`; it
does not rebuild AVRDUDE.

## First use and CLI

Install the APK, launch it once, and grant the app Android's All files access
setting if you want to pass absolute firmware paths from Termux. Connect the Uno
to the phone with USB OTG and approve the Android USB access prompt for the CH340
device. Android controls whether this grant survives a disconnect or reboot.

Copy `tools/attiny-flash` into Termux's `~/bin/` and make it executable. The
script starts the app's foreground service and streams its result log to the
SSH session:

```sh
attiny-flash signature
attiny-flash flash /storage/emulated/0/Download/firmware.hex
```

The app's All files access permission is broad. It is requested because Android's
scoped-storage rules otherwise prevent an unattended service from opening an
arbitrary `.hex` path supplied by the Termux CLI.

The app validates the requested action and firmware path, uses AVRDUDE's
`jtag2updi` programmer with the same serial-port token and core options used by
ZFlasher (`-P COM -b 115200 -p t1614`). The USB file descriptor is inherited by
AVRDUDE and used for CH340 USBFS control and bulk ioctls; the app does not proxy
the serial stream. Flashing uses `-e -U flash:w:<firmware>:i`, matching
ZFlasher's Flash erase option, and AVRDUDE's default write verification remains
enabled. Output and result status are written to
`Download/AttinyFlash/<request-id>.log`, which the Termux CLI streams to the SSH
session.
Flashing overwrites the ATtiny1614 flash; use a known-good Intel HEX image.

## Scope and limitations

- The first build targets the connected SM-A556B (`arm64-v8a`) and CH340 adapter.
- USB permission is granted by Android to this app, not by SSH/ADB pairing.
- The Termux command is a client interface; USB access and AVRDUDE execution
  happen inside the Android app.
- The app does not bundle a firmware image. The flash command must receive a
  readable path that the app can access under Android's storage rules.
