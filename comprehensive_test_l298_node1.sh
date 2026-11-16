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
NODE_SKETCH="$SKETCH_ROOT/attiny1614/L298NNode1/L298NNode1.ino"
MOTOR_TEST_MS="${L298_MOTOR_TEST_MS:-250}"
VALVE_TEST_MS="${L298_VALVE_TEST_MS:-500}"
node_available=0

if [[ ! "$MOTOR_TEST_MS" =~ ^[0-9]+$ ]] || (( MOTOR_TEST_MS < 100 || MOTOR_TEST_MS > 2000 )); then
    echo "L298_MOTOR_TEST_MS must be between 100 and 2000." >&2
    exit 2
fi
if [[ ! "$VALVE_TEST_MS" =~ ^[0-9]+$ ]] || (( VALVE_TEST_MS < 100 || VALVE_TEST_MS > 2000 )); then
    echo "L298_VALVE_TEST_MS must be between 100 and 2000." >&2
    exit 2
fi

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
    if ! grep -Fq -- "BUS_RESPONSE $expected" <<<"$output"; then
        echo "FAIL: $* (missing response: $expected)" >&2
        echo "$output" >&2
        return 1
    fi
    echo "PASS: $* -> $expected"
}

read_numeric_response() {
    local kind="$1"
    local expected_pattern="$2"
    local output value
    if ! output="$(andruino-flash bus --usb-device "$master_usb" "$kind" 1 2>&1)"; then
        echo "FAIL: $kind 1 command failed" >&2
        echo "$output" >&2
        return 1
    fi
    value="$(grep -Eo "$expected_pattern" <<<"$output" | tail -n 1 | awk '{print $4}')"
    if [[ ! "$value" =~ ^-?[0-9]+$ ]]; then
        echo "FAIL: could not read numeric $kind response" >&2
        echo "$output" >&2
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

read_angle() {
    local value
    value="$(read_numeric_response ANGLE 'NODE 1 ANGLE [0-9]+')"
    if (( value < 0 || value > 4095 )); then
        echo "FAIL: AS5600 raw angle is outside 0..4095: $value" >&2
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
    if grep -Fq 'BUS_RESPONSE OK NODES NONE' <<<"$output"; then
        echo "Node 1 is not responding yet."
        return 2
    fi
    if ! grep -Eq 'BUS_RESPONSE OK NODES 1$' <<<"$output"; then
        echo "FAIL: expected only node 1 on the bus" >&2
        echo "$output" >&2
        return 1
    fi
    echo "PASS: DISCOVER -> node 1 only"
}

flash_and_verify() {
    local output
    if ! output="$(andruino-flash flash-sketch \
        --platform attiny1614 --node 1 --usb-device "$master_usb" "$NODE_SKETCH" 2>&1)"; then
        echo "FAIL: ATtiny1614 L298N node flash" >&2
        echo "$output" >&2
        return 1
    fi
    echo "$output"
    if ! grep -Eq '[0-9,]+ bytes of flash verified' <<<"$output"; then
        echo "FAIL: node flash did not report AVRDUDE read-back verification" >&2
        return 1
    fi
    echo "PASS: L298N node 1 flash read-back verified"
}

cleanup() {
    local test_status=$?
    local cleanup_failed=0
    trap - EXIT
    if [[ "$node_available" -eq 1 ]]; then
        if ! andruino-flash bus --usb-device "$master_usb" STOP 1 >/dev/null 2>&1; then
            echo "CLEANUP WARN: could not stop node 1 outputs" >&2
            cleanup_failed=1
        fi
        if ! andruino-flash bus --usb-device "$master_usb" MODE COMM >/dev/null 2>&1; then
            echo "CLEANUP WARN: could not restore COMM mode" >&2
            cleanup_failed=1
        fi
        if ! andruino-flash bus --usb-device "$master_usb" LED 1 OFF >/dev/null 2>&1; then
            echo "CLEANUP WARN: could not turn off node 1 LED" >&2
            cleanup_failed=1
        fi
        if ! andruino-flash bus --usb-device "$master_usb" UPDI DISABLE 1 >/dev/null 2>&1; then
            echo "CLEANUP WARN: could not disable node 1 UPDI gate" >&2
            cleanup_failed=1
        fi
    fi
    if [[ "$cleanup_failed" -ne 0 && "$test_status" -eq 0 ]]; then
        test_status=1
    fi
    exit "$test_status"
}
trap cleanup EXIT

echo "UNO USB: $master_usb"
echo "L298N node sketch: $NODE_SKETCH"
echo "Motor pulse: ${MOTOR_TEST_MS} ms; valve pulse: ${VALVE_TEST_MS} ms"
echo "This test actuates real hardware. Keep a physical power cutoff within reach."

if [[ ! -f "$NODE_SKETCH" ]]; then
    echo "Missing L298N node sketch: $NODE_SKETCH" >&2
    exit 1
fi
if [[ ! -t 0 ]]; then
    echo "The real-peripheral test requires an interactive terminal and operator confirmations." >&2
    exit 2
fi
read -r -p "Confirm IN1-IN4 have pull-downs, current limits are verified, and travel is clear. Type ACTUATE: " confirmation
if [[ "$confirmation" != "ACTUATE" ]]; then
    echo "Test cancelled before flashing or actuator commands."
    exit 2
fi

if [[ "$skip_flash" -eq 0 ]]; then
    discovery_status=0
    discover_single_node || discovery_status=$?
    if [[ "$discovery_status" -ne 0 && "$discovery_status" -ne 2 ]]; then
        exit 1
    fi
    flash_and_verify
    if ! discover_single_node; then
        echo "Node 1 did not respond after flashing. Check its power, ground, and D5 bus wiring." >&2
        exit 1
    fi
else
    if ! discover_single_node; then
        echo "Node 1 must be responding when --no-flash is used." >&2
        exit 1
    fi
    echo "Skipping node 1 reflash (--no-flash)."
fi
node_available=1

run_bus "OK MODE COMM;" MODE COMM
run_bus "NODE 1 PONG" PING 1
run_bus "NODE 1 VERSION ATTINY1614-L298N-AS5600-1" VERSION 1
run_bus "NODE 1 STATUS MOTOR OFF VALVE OFF ENCODER" STATUS 1

angle_before="$(read_angle)"
encoder_before="$(read_encoder)"
echo "PASS: AS5600 angle=$angle_before encoder=$encoder_before"
run_bus "NODE 1 ENCODER 0" RESET_ENCODER 1
run_bus "NODE 1 LED ON" LED 1 ON
run_bus "NODE 1 LED OFF" LED 1 OFF

read -r -p "Press Enter to pulse the valve for ${VALVE_TEST_MS} ms, or Ctrl+C to abort: "
run_bus "NODE 1 VALVE ON" VALVE 1 ON
sleep "$(awk -v ms="$VALVE_TEST_MS" 'BEGIN { printf "%.3f", ms / 1000 }')"
run_bus "NODE 1 VALVE OFF" VALVE 1 OFF
read -r -p "Did the valve physically actuate and release? [y/N] " valve_confirmed
if [[ ! "$valve_confirmed" =~ ^[Yy]$ ]]; then
    echo "FAIL: operator did not confirm physical valve actuation." >&2
    exit 1
fi

encoder_before="$(read_encoder)"
run_bus "NODE 1 MOTOR FORWARD STARTED MS $MOTOR_TEST_MS" MOTOR 1 FORWARD "$MOTOR_TEST_MS"
sleep "$(awk -v ms="$MOTOR_TEST_MS" 'BEGIN { printf "%.3f", (ms + 150) / 1000 }')"
run_bus "NODE 1 MOTOR OFF" MOTOR 1 OFF
encoder_forward="$(read_encoder)"
forward_delta=$((encoder_forward - encoder_before))
if (( forward_delta == 0 )); then
    echo "FAIL: encoder count did not change during the forward motor pulse." >&2
    exit 1
fi
echo "PASS: forward encoder delta=$forward_delta"

sleep 0.3
run_bus "NODE 1 MOTOR REVERSE STARTED MS $MOTOR_TEST_MS" MOTOR 1 REVERSE "$MOTOR_TEST_MS"
sleep "$(awk -v ms="$MOTOR_TEST_MS" 'BEGIN { printf "%.3f", (ms + 150) / 1000 }')"
run_bus "NODE 1 MOTOR OFF" MOTOR 1 OFF
encoder_reverse="$(read_encoder)"
reverse_delta=$((encoder_reverse - encoder_forward))
if (( reverse_delta == 0 )); then
    echo "FAIL: encoder count did not change during the reverse motor pulse." >&2
    exit 1
fi
if (( (forward_delta > 0 && reverse_delta > 0) || (forward_delta < 0 && reverse_delta < 0) )); then
    echo "FAIL: encoder direction did not reverse (forward=$forward_delta reverse=$reverse_delta)." >&2
    exit 1
fi
echo "PASS: reverse encoder delta=$reverse_delta"

run_bus "NODE 1 STOPPED VALVE OFF MOTOR OFF" STOP 1
run_bus "NODE 1 STATUS MOTOR OFF VALVE OFF ENCODER" STATUS 1
run_bus "NODE 1 UPDI DISABLED" UPDI DISABLE 1
node_available=0
echo "PASS: L298N motor, valve, encoder, LED, and Node 1 bus checks passed."
