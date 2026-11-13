#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

export PATH="$HOME/bin:$PATH"
skip_flash=0
if [[ "${1:-}" == "--no-flash" ]]; then
    skip_flash=1
    shift
fi
MASTER_USB="${1:-${NANO_USB:-${UNO_USB:-}}}"
SKETCH_ROOT="${ANDRUINO_SKETCH_ROOT:-/storage/emulated/0/Download/andruino}"
current_node=0

if [[ -z "$MASTER_USB" ]]; then
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
        echo "Disconnect other adapters or pass the master path as the first argument." >&2
        exit 1
    fi
    MASTER_USB="${master_paths[0]}"
fi

run_bus() {
    local expected="$1"
    shift
    local output
    if ! output="$(andruino-flash bus --usb-device "$MASTER_USB" "$@" 2>&1)"; then
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

cleanup() {
    local test_status=$?
    local cleanup_failed=0
    trap - EXIT

    if [[ -z "$MASTER_USB" ]]; then
        return "$test_status"
    fi

    if ! andruino-flash bus --usb-device "$MASTER_USB" MODE COMM >/dev/null 2>&1; then
        echo "CLEANUP WARN: could not restore COMM mode" >&2
        cleanup_failed=1
    fi
    if [[ "$current_node" -ge 1 && "$current_node" -le 7 ]]; then
        if ! andruino-flash bus --usb-device "$MASTER_USB" STOP "$current_node" >/dev/null 2>&1; then
            echo "CLEANUP WARN: could not stop node $current_node" >&2
            cleanup_failed=1
        fi
        if ! andruino-flash bus --usb-device "$MASTER_USB" LED "$current_node" OFF >/dev/null 2>&1; then
            echo "CLEANUP WARN: could not turn off node $current_node LED" >&2
            cleanup_failed=1
        fi
    fi
    if [[ "$cleanup_failed" -ne 0 && "$test_status" -eq 0 ]]; then
        exit 1
    fi
    return "$test_status"
}
trap cleanup EXIT

echo "Master USB: $MASTER_USB"
echo "This verifies protocol/firmware behavior; disconnected loads and sensors are not physically tested."

run_bus "OK NODES 1,2,3,4,5,6,7" DISCOVER
run_bus "OK MODE COMM;" MODE COMM
for node in {1..7}; do
    current_node="$node"
    run_bus "NODE $node PONG" PING "$node"
done

if [[ "$skip_flash" -eq 0 ]]; then
    echo "=== Reflashing address-specific BlinkNode sketches ==="
    for node in {1..7}; do
        current_node="$node"
        sketch="$SKETCH_ROOT/attiny1614/BlinkNode$node/BlinkNode$node.ino"
        if [[ ! -f "$sketch" ]]; then
            echo "Missing node $node sketch: $sketch" >&2
            exit 1
        fi
        echo "Flashing node $node from $sketch (flash-sketch selects the requested target)"
        if ! flash_output="$(
            andruino-flash flash-sketch \
                --platform attiny1614 \
                --node "$node" \
                --usb-device "$MASTER_USB" \
                "$sketch" 2>&1
        )"; then
            echo "FAIL: node $node flash command failed" >&2
            echo "$flash_output" >&2
            exit 1
        fi
        echo "$flash_output"
        if ! grep -Eq '[0-9,]+ bytes of flash verified' <<<"$flash_output"; then
            echo "FAIL: node $node flash did not report AVRDUDE read-back verification" >&2
            exit 1
        fi
        echo "PASS: node $node flash read-back verified"
    done
else
    echo "Skipping reflash phase (--no-flash)."
fi

echo "=== Checking communication and every shared-node command ==="
run_bus "OK MODE COMM;" MODE COMM
for node in {1..7}; do
    current_node="$node"
    echo "--- Node $node ---"
    run_bus "NODE $node PONG" PING "$node"
    run_bus "NODE $node VERSION " VERSION "$node"
    run_bus "NODE $node STATUS COMM V=" STATUS "$node"
    run_bus "NODE $node BLINK 200" BLINK "$node" 200
    run_bus "NODE $node LED ON" LED "$node" ON
    run_bus "NODE $node LED OFF" LED "$node" OFF
    run_bus "NODE $node UPDI DISABLED" UPDI DISABLE "$node"
    run_bus "NODE $node UPDI ENABLED" UPDI ENABLE "$node"
    run_bus "NODE $node VALVE ON" VALVE "$node" ON
    run_bus "NODE $node MOTOR FORWARD STARTED MS 60000" MOTOR "$node" FORWARD 60000
    run_bus "NODE $node MOTION MOTOR FORWARD VALVE ON" MOTION_STATUS "$node"
    run_bus "NODE $node VALVE OFF" VALVE "$node" OFF
    run_bus "NODE $node MOTOR REVERSE STARTED MS 60000" MOTOR "$node" REVERSE 60000
    run_bus "NODE $node MOTION MOTOR REVERSE VALVE OFF" MOTION_STATUS "$node"
    run_bus "NODE $node MOTOR OFF" MOTOR "$node" OFF
    run_bus "NODE $node STOPPED VALVE OFF MOTOR OFF" STOP "$node"
    run_bus "NODE $node MOTION MOTOR OFF VALVE OFF" MOTION_STATUS "$node"
    run_bus "NODE $node ENCODER -999999" READ_ENCODER "$node"
    run_bus "NODE $node ENCODER 0" RESET_ENCODER "$node"
    run_bus "NODE $node ANGLE -1" ANGLE "$node"
done

echo "=== Checking flash-mode node selection ==="
run_bus "OK MODE FLASH;" MODE FLASH
for node in {1..7}; do
    current_node="$node"
    run_bus "OK SELECT NODE $node;" SELECT "$node"
done
run_bus "OK MODE COMM;" MODE COMM
run_bus "OK SELECT NODE 1;" SELECT 1
for node in {1..7}; do
    current_node="$node"
    run_bus "NODE $node LED OFF" LED "$node" OFF
done
current_node=0

echo "PASS: all seven nodes reflashed and all protocol/command checks passed."
