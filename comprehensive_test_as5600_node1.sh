#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

export PATH="$HOME/bin:$PATH"

skip_flash=0
master_usb=""
while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --no-flash)
            skip_flash=1
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--no-flash] [UNO_USB_PATH]"
            exit 0
            ;;
        -*)
            echo "Unknown option: $1" >&2
            echo "Usage: $0 [--no-flash] [UNO_USB_PATH]" >&2
            exit 2
            ;;
        *)
            if [[ -n "$master_usb" ]]; then
                echo "Only one UNO USB path may be specified." >&2
                exit 2
            fi
            master_usb="$1"
            shift
            ;;
    esac
done

SKETCH_ROOT="${ANDRUINO_SKETCH_ROOT:-/storage/emulated/0/Download/andruino}"
NODE_SKETCH="$SKETCH_ROOT/attiny1614/AS5600Node1/AS5600Node1.ino"
node_available=0

if [[ -z "$master_usb" ]]; then
    master_usb="${UNO_USB:-}"
fi
if [[ -z "$master_usb" ]]; then
    mapfile -t master_paths < <(
        andruino-flash devices |
            awk '/USB_DEVICE path=/ {
                for (i = 1; i <= NF; ++i) {
                    if ($i ~ /^path=/) {
                        sub(/^path=/, "", $i)
                        print $i
                    }
                }
            }'
    )
    if [[ "${#master_paths[@]}" -ne 1 ]]; then
        echo "Expected exactly one supported USB serial adapter; found ${#master_paths[@]}." >&2
        echo "Disconnect other adapters or pass the UNO path as the first argument." >&2
        exit 1
    fi
    master_usb="${master_paths[0]}"
fi

run_bus() {
    local expected="$1"
    shift
    local output
    if ! output="$(andruino-flash bus --usb-device "$master_usb" "$@" 2>&1)"; then
        echo "FAIL: $* (command failed)" >&2
        echo "$output" >&2
        return 1
    fi
    if ! grep -Fq -- "$expected" <<<"$output"; then
        echo "FAIL: $* (missing response: $expected)" >&2
        echo "$output" >&2
        return 1
    fi
    echo "PASS: $* -> $expected"
}

read_numeric_response() {
    local command="$1"
    local pattern="$2"
    local output value
    if ! output="$(andruino-flash bus --usb-device "$master_usb" "$command" 1 2>&1)"; then
        echo "FAIL: $command 1 command failed" >&2
        echo "$output" >&2
        return 1
    fi
    value="$(grep -Eo "$pattern" <<<"$output" | tail -n 1 | awk '{print $4}')"
    if [[ ! "$value" =~ ^-?[0-9]+$ ]]; then
        echo "FAIL: could not read numeric $command response" >&2
        echo "$output" >&2
        return 1
    fi
    printf '%s\n' "$value"
}

read_angle() {
    local value
    value="$(read_numeric_response ANGLE 'NODE 1 ANGLE -?[0-9]+')"
    if (( value < 0 || value > 4095 )); then
        echo "FAIL: AS5600 raw angle is outside 0..4095: $value" >&2
        return 1
    fi
    printf '%s\n' "$value"
}

read_encoder() {
    local value
    value="$(read_numeric_response READ_ENCODER 'NODE 1 ENCODER -?[0-9]+')"
    if [[ "$value" == "-999999" ]]; then
        echo "FAIL: AS5600 is unavailable on the node." >&2
        return 1
    fi
    printf '%s\n' "$value"
}

discover_single_node() {
    local output
    if ! output="$(andruino-flash bus --usb-device "$master_usb" DISCOVER 2>&1)"; then
        echo "FAIL: DISCOVER command failed" >&2
        echo "$output" >&2
        return 1
    fi
    if ! grep -Eq 'BUS_RESPONSE OK NODES (NONE|1)$' <<<"$output"; then
        echo "FAIL: expected only node 1 on the bus" >&2
        echo "$output" >&2
        return 1
    fi
    if grep -Fq 'BUS_RESPONSE OK NODES NONE' <<<"$output"; then
        echo "Node 1 is not responding yet; continuing to the flash step."
        return 2
    fi
    echo "PASS: DISCOVER -> node 1 only"
}

flash_and_verify() {
    local output
    if ! output="$(andruino-flash flash-sketch \
        --platform attiny1614 --node 1 --usb-device "$master_usb" "$NODE_SKETCH" 2>&1)"; then
        echo "FAIL: ATtiny1614 AS5600 node flash" >&2
        echo "$output" >&2
        return 1
    fi
    echo "$output"
    if ! grep -Eq '[0-9,]+ bytes of flash verified' <<<"$output"; then
        echo "FAIL: node flash did not report AVRDUDE read-back verification" >&2
        return 1
    fi
    echo "PASS: AS5600 node 1 flash read-back verified"
}

cleanup() {
    local test_status=$?
    trap - EXIT
    if [[ "$node_available" -eq 1 ]]; then
        andruino-flash bus --usb-device "$master_usb" MODE COMM >/dev/null 2>&1 || \
            echo "CLEANUP WARN: could not restore COMM mode" >&2
        andruino-flash bus --usb-device "$master_usb" UPDI DISABLE 1 >/dev/null 2>&1 || \
            echo "CLEANUP WARN: could not disable node 1 UPDI gate" >&2
    fi
    return "$test_status"
}
trap cleanup EXIT

if [[ ! -f "$NODE_SKETCH" ]]; then
    echo "Missing AS5600 node sketch: $NODE_SKETCH" >&2
    exit 1
fi
if [[ ! -t 0 ]]; then
    echo "The rotation test requires an interactive terminal so the shaft can be turned by hand." >&2
    exit 2
fi

echo "UNO USB: $master_usb"
echo "AS5600 node sketch: $NODE_SKETCH"
echo "This test only reads the encoder; it does not command a motor or valve."

if [[ "$skip_flash" -eq 0 ]]; then
    discovery_status=0
    discover_single_node || discovery_status=$?
    if [[ "$discovery_status" -ne 0 && "$discovery_status" -ne 2 ]]; then
        exit 1
    fi
    flash_and_verify
else
    echo "Skipping node 1 reflash (--no-flash)."
fi

if ! discover_single_node; then
    echo "Node 1 did not respond after flashing. Check its power, ground, and D5 bus wiring." >&2
    exit 1
fi
node_available=1

run_bus "NODE 1 PONG" PING 1
run_bus "NODE 1 VERSION ATTINY1614-AS5600-1" VERSION 1
run_bus "NODE 1 STATUS ENCODER" STATUS 1
run_bus "NODE 1 ENCODER 0" RESET_ENCODER 1

angle_before="$(read_angle)"
encoder_before="$(read_encoder)"
angle_degrees="$(awk -v raw="$angle_before" 'BEGIN { printf "%.2f", raw * 360 / 4096 }')"
echo "PASS: initial angle=${angle_before}/4096 counts (${angle_degrees} degrees), encoder=$encoder_before"

read -r -p "Turn the magnet/shaft slowly by hand at least 30 degrees, then press Enter: "
angle_after="$(read_angle)"
encoder_after="$(read_encoder)"
encoder_delta=$((encoder_after - encoder_before))
angle_delta=$((angle_after - angle_before))
if (( angle_delta > 2048 )); then
    angle_delta=$((angle_delta - 4096))
elif (( angle_delta < -2048 )); then
    angle_delta=$((angle_delta + 4096))
fi
absolute_delta=$encoder_delta
if (( absolute_delta < 0 )); then
    absolute_delta=$((-absolute_delta))
fi

angle_degrees="$(awk -v raw="$angle_after" 'BEGIN { printf "%.2f", raw * 360 / 4096 }')"
echo "Raw angle=${angle_after}/4096 counts (${angle_degrees} degrees); raw delta=$angle_delta; accumulated delta=$encoder_delta"
if (( absolute_delta < 8 )); then
    echo "FAIL: encoder count changed by fewer than 8 counts after manual rotation." >&2
    exit 1
fi

run_bus "NODE 1 STATUS ENCODER" STATUS 1
run_bus "NODE 1 UPDI DISABLED" UPDI DISABLE 1
node_available=0
echo "PASS: AS5600 angle measurement and rotation response verified."
