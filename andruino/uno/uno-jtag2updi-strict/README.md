# Uno strict JTAG2UPDI source

This package is the recovered strict source snapshot from commit
`7f03242c41dd08c5216a8ef515b590b8dcf54cbe`. `uno-jtag2updi-strict.ino` is
the Arduino sketch entry point; the implementation is in the supporting
`.cpp` files.

Build the complete package with the repository's custom builder, not Termux
Arduino CLI:

```powershell
$output = Join-Path $env:TEMP "uno-jtag2updi-strict-build"
.\tools\build-uno-firmware.ps1 `
  -SourceDirectory ".\andruino\uno\uno-jtag2updi-strict" `
  -OutputDirectory $output
Get-FileHash (Join-Path $output "jtag2updi-fixed.hex") -Algorithm SHA256
```

The verified output SHA-256 is
`19889521fed85125927b623edbc3c55cfa6fcca74e4fa6aed625c4088e9a9c5d`.
Flash that generated image to an Uno with the `uno` profile. Do not substitute
the current `andruino/uno/jtag2updi` Arduino CLI build.
