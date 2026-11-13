param(
    [string]$ArduinoRoot = "$env:LOCALAPPDATA\Arduino15",
    [string]$OutputDirectory = "$PSScriptRoot\..\releases\uno-jtag2updi",
    [string]$SourceDirectory = "$PSScriptRoot\..\andruino\uno\jtag2updi"
)

$ErrorActionPreference = 'Stop'
$core = Get-ChildItem "$ArduinoRoot\packages\arduino\hardware\avr" -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'cores\arduino\Arduino.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
$tool = Get-ChildItem "$ArduinoRoot\packages\arduino\tools\avr-gcc" -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'bin\avr-g++.exe') } |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $core -or -not $tool) { throw 'Arduino AVR core and avr-gcc are required; install Arduino AVR boards support.' }

$root = (Resolve-Path -LiteralPath $SourceDirectory).Path
$corePath = $core.FullName
$toolPath = $tool.FullName
$gcc = Join-Path $toolPath 'bin\avr-g++.exe'
$cc = Join-Path $toolPath 'bin\avr-gcc.exe'
$objcopy = Join-Path $toolPath 'bin\avr-objcopy.exe'
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$incs = @("-I$corePath\cores\arduino", "-I$corePath\variants\standard", "-I$corePath\libraries\SoftwareSerial\src", "-I$corePath\libraries\EEPROM\src", "-I$root")
$objects = @()
Get-ChildItem $root -Filter '*.cpp' | ForEach-Object {
    $object = Join-Path $OutputDirectory ($_.BaseName + '.o')
    & $gcc @('-c', '-std=gnu++11', '-mmcu=atmega328p', '-DF_CPU=16000000L', '-Os', '-ffunction-sections', '-fdata-sections', '-fno-exceptions') $incs '-o' $object $_.FullName
    if ($LASTEXITCODE) { throw "Compile failed: $($_.Name)" }
    $objects += $object
}
Get-ChildItem "$corePath\cores\arduino" -Filter '*.cpp' | Where-Object Name -ne 'main.cpp' | ForEach-Object {
    $object = Join-Path $OutputDirectory ('core-' + $_.BaseName + '.o')
    & $gcc @('-c', '-std=gnu++11', '-mmcu=atmega328p', '-DF_CPU=16000000L', '-Os', '-ffunction-sections', '-fdata-sections', '-fno-exceptions') $incs '-o' $object $_.FullName
    if ($LASTEXITCODE) { throw "Compile failed: $($_.Name)" }
    $objects += $object
}
Get-ChildItem "$corePath\cores\arduino" -Filter '*.c' | ForEach-Object {
    $object = Join-Path $OutputDirectory ('core-' + $_.BaseName + '.o')
    & $cc @('-c', '-std=gnu11', '-mmcu=atmega328p', '-DF_CPU=16000000L', '-Os', '-ffunction-sections', '-fdata-sections') $incs '-o' $object $_.FullName
    if ($LASTEXITCODE) { throw "Compile failed: $($_.Name)" }
    $objects += $object
}
foreach ($library in @("$corePath\libraries\SoftwareSerial\src", "$corePath\libraries\EEPROM\src")) {
    Get-ChildItem $library -Filter '*.cpp' -ErrorAction SilentlyContinue | ForEach-Object {
        $object = Join-Path $OutputDirectory ('lib-' + $_.BaseName + '.o')
        & $gcc @('-c', '-std=gnu++11', '-mmcu=atmega328p', '-DF_CPU=16000000L', '-Os', '-ffunction-sections', '-fdata-sections', '-fno-exceptions') $incs '-o' $object $_.FullName
        if ($LASTEXITCODE) { throw "Compile failed: $($_.Name)" }
        $objects += $object
    }
}
$elf = Join-Path $OutputDirectory 'jtag2updi.elf'
$hex = Join-Path $OutputDirectory 'jtag2updi-fixed.hex'
& $gcc @('-mmcu=atmega328p', '-Os', '-Wl,--gc-sections', '-o', $elf) $objects
if ($LASTEXITCODE) { throw 'Link failed' }
& $objcopy '-O' ihex '-R' .eeprom $elf $hex
if ($LASTEXITCODE) { throw 'HEX conversion failed' }
Get-Item $hex
Get-FileHash $hex -Algorithm SHA256
