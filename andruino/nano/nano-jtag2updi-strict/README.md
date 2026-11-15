# Nano strict JTAG2UPDI source reference

This package is the same recovered strict source snapshot as
`andruino/uno/uno-jtag2updi-strict`. `nano-jtag2updi-strict.ino` is the
Arduino sketch entry point; the implementation is in the supporting `.cpp`
files.

The verified workflow builds the strict image from the Uno package with
`tools/build-uno-firmware.ps1`, then uploads that exact image to a classic Nano
with the `nano` profile (old bootloader, 57,600 baud). Do not build this source
with the Nano Arduino CLI profile or substitute a separately generated
`jtag2updi.ino.hex`; those images are not the tested strict image.

The verified strict image SHA-256 is
`19889521fed85125927b623edbc3c55cfa6fcca74e4fa6aed625c4088e9a9c5d`.
