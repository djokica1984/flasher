#!/usr/bin/env bash
set -euo pipefail

source_dir="${1:?Usage: build-strict-master.sh SOURCE_DIR OUTPUT_HEX}"
output_hex="${2:?Usage: build-strict-master.sh SOURCE_DIR OUTPUT_HEX}"
expected_sha256="da81995aad8792513af0e7d5a3aa28956d7c2c1abb465b91e2f9521ce31f96cd"

if [[ ! -f "$source_dir/uno-jtag2updi-strict.ino" ]]; then
    echo "Missing strict Uno sketch entry point: $source_dir/uno-jtag2updi-strict.ino" >&2
    exit 1
fi

arduino_root="${ARDUINO15_DIR:-$HOME/.arduino15}"
core="$arduino_root/packages/arduino/hardware/avr/1.8.8"
tool="$arduino_root/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7"
gcc="$tool/bin/avr-gcc"
gxx="$tool/bin/avr-g++"
objcopy="$tool/bin/avr-objcopy"

for path in "$core/cores/arduino/Arduino.h" "$gcc" "$gxx" "$objcopy"; do
    if [[ ! -f "$path" ]]; then
        echo "Missing strict build dependency: $path" >&2
        exit 1
    fi
done

output_dir="$(dirname -- "$output_hex")"
mkdir -p -- "$output_dir"
build_dir="$(mktemp -d "$output_dir/.strict-uno-build.XXXXXX")"
trap 'rm -rf -- "$build_dir"' EXIT

includes=(
    "-I$core/cores/arduino"
    "-I$core/variants/standard"
    "-I$core/libraries/SoftwareSerial/src"
    "-I$core/libraries/EEPROM/src"
    "-I$source_dir"
)
objects=()
cpp_flags=(-c -std=gnu++11 -mmcu=atmega328p -DF_CPU=16000000L -Os
    -ffunction-sections -fdata-sections -fno-exceptions)
c_flags=(-c -std=gnu11 -mmcu=atmega328p -DF_CPU=16000000L -Os
    -ffunction-sections -fdata-sections)

compile_cpp() {
    local input="$1"
    local object_name="$2"
    local object="$build_dir/$object_name"
    "$gxx" "${cpp_flags[@]}" "${includes[@]}" -o "$object" "$input"
    objects+=("$object")
}

while IFS= read -r input; do
    compile_cpp "$input" "$(basename -- "${input%.cpp}").o"
done < <(find "$source_dir" -maxdepth 1 -type f -name '*.cpp' -print | LC_ALL=C sort -f)

while IFS= read -r input; do
    compile_cpp "$input" "core-$(basename -- "${input%.cpp}").o"
done < <(find "$core/cores/arduino" -maxdepth 1 -type f -name '*.cpp' \
    ! -name 'main.cpp' -print | LC_ALL=C sort -f)

while IFS= read -r input; do
    object="$build_dir/core-$(basename -- "${input%.c}").o"
    "$gcc" "${c_flags[@]}" "${includes[@]}" -o "$object" "$input"
    objects+=("$object")
done < <(find "$core/cores/arduino" -maxdepth 1 -type f -name '*.c' -print | LC_ALL=C sort -f)

for library in SoftwareSerial EEPROM; do
    library_dir="$core/libraries/$library/src"
    if [[ -d "$library_dir" ]]; then
        while IFS= read -r input; do
            compile_cpp "$input" "lib-$(basename -- "${input%.cpp}").o"
        done < <(find "$library_dir" -maxdepth 1 -type f -name '*.cpp' -print | LC_ALL=C sort -f)
    fi
done

elf="$build_dir/jtag2updi.elf"
built_hex="$build_dir/jtag2updi-fixed.hex"
"$gxx" -mmcu=atmega328p -Os -Wl,--gc-sections -o "$elf" "${objects[@]}"
"$objcopy" -O ihex -R .eeprom "$elf" "$built_hex"

actual_sha256="$(sha256sum -- "$built_hex" | awk '{print $1}')"
if [[ "$actual_sha256" != "$expected_sha256" ]]; then
    echo "Strict Uno build hash mismatch." >&2
    echo "Expected: $expected_sha256" >&2
    echo "Actual:   $actual_sha256" >&2
    exit 1
fi

cp -- "$built_hex" "$output_hex"
echo "Built strict Uno image from $source_dir/uno-jtag2updi-strict.ino"
echo "HEX: $output_hex"
echo "SHA-256: $actual_sha256"
