# MOVE-enabled Uno strict JTAG2UPDI source

This package is based on the recovered strict source snapshot from commit
`7f03242c41dd08c5216a8ef515b590b8dcf54cbe` and includes task-specific control
bus changes: bounded `MOVE` forwarding, a 128-byte response buffer, and
discovered-node reporting. It is the Uno master required by the L298N
nine-point test. It is not the archived baseline/Nano image.
`uno-jtag2updi-strict.ino` is the sketch entry point; the implementation is in
the supporting `.cpp` files.

On Windows, build the complete package with the repository's custom builder:

```powershell
$output = Join-Path $env:TEMP "uno-jtag2updi-strict-build"
.\tools\build-uno-firmware.ps1 `
  -SourceDirectory ".\andruino\uno\uno-jtag2updi-strict" `
  -OutputDirectory $output
Get-FileHash (Join-Path $output "jtag2updi-fixed.hex") -Algorithm SHA256
```

The verified output SHA-256 is
`da81995aad8792513af0e7d5a3aa28956d7c2c1abb465b91e2f9521ce31f96cd`. Flash
that generated image to an Uno with the `uno` profile. Do not substitute the
current `andruino/uno/jtag2updi` Arduino CLI build.

The Termux `comprehensive_test_uno_node1.sh` protocol test builds this package
with `build-strict-master.sh`, Arduino AVR core `1.8.8`, and AVR-GCC
`7.3.0-atmel3.6.1-arduino7`; it refuses to flash unless the output matches the
verified hash above. The nine-point L298N script expects this image to have
already been built and installed; follow `docs/REPRODUCING.md` for those
steps. Do not use `arduino-cli compile` for this master.

The archived baseline/Nano image is separate:
`releases/uno-jtag2updi/uno-jtag2updi-strict.hex`,
SHA-256 `19889521fed85125927b623edbc3c55cfa6fcca74e4fa6aed625c4088e9a9c5d`.
Its exact source is in `releases/uno-jtag2updi/strict-source/`; use that
directory with `tools/build-uno-firmware.ps1` to reproduce the archived image.
