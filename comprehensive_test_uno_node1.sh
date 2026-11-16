#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

export PATH="$HOME/bin:$PATH"

skip_flash=0
master_already_flashed=0
master_usb=""
while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --no-flash)
            skip_flash=1
            shift
            ;;
        --master-already-flashed)
            master_already_flashed=1
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [--no-flash | --master-already-flashed] [UNO_USB_PATH]"
            exit 0
            ;;
        -*)
            echo "Unknown option: $1" >&2
            echo "Usage: $0 [--no-flash | --master-already-flashed] [UNO_USB_PATH]" >&2
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

if [[ "$skip_flash" -eq 1 && "$master_already_flashed" -eq 1 ]]; then
    echo "Choose either --no-flash or --master-already-flashed, not both." >&2
    exit 2
fi

SKETCH_ROOT="${ANDRUINO_SKETCH_ROOT:-/storage/emulated/0/Download/andruino}"
STRICT_MASTER_HEX="${UNO_STRICT_MASTER_HEX:-/storage/emulated/0/Download/uno-jtag2updi-strict-source-build.hex}"
STRICT_MASTER_SOURCE_DIR="${UNO_STRICT_MASTER_SOURCE_DIR:-$SKETCH_ROOT/uno/uno-jtag2updi-strict}"
STRICT_MASTER_SKETCH="$STRICT_MASTER_SOURCE_DIR/uno-jtag2updi-strict.ino"
STRICT_MASTER_BUILDER="$STRICT_MASTER_SOURCE_DIR/build-strict-master.sh"
STRICT_MASTER_SHA256="da81995aad8792513af0e7d5a3aa28956d7c2c1abb465b91e2f9521ce31f96cd"
export ANDRUINO_SKETCH_ROOT="$SKETCH_ROOT"
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
        echo "PASS: DISCOVER -> no nodes currently responding"
        return 2
    fi
    echo "PASS: DISCOVER -> node 1 only"
}

flash_and_verify() {
    local description="$1"
    shift
    local output
    if ! output="$("$@" 2>&1)"; then
        echo "FAIL: $description" >&2
        echo "$output" >&2
        return 1
    fi
    echo "$output"
    if ! grep -Eq '[0-9,]+ bytes of flash verified' <<<"$output"; then
        echo "FAIL: $description did not report AVRDUDE flash read-back verification" >&2
        return 1
    fi
    echo "PASS: $description read-back verified"
}

cleanup() {
    local test_status=$?
    local cleanup_failed=0
    trap - EXIT

    if [[ -z "$master_usb" ]]; then
        return "$test_status"
    fi

    if [[ "$node_available" -eq 1 ]]; then
        if ! andruino-flash bus --usb-device "$master_usb" MODE COMM >/dev/null 2>&1; then
            echo "CLEANUP WARN: could not restore COMM mode" >&2
            cleanup_failed=1
        fi
        if ! andruino-flash bus --usb-device "$master_usb" STOP 1 >/dev/null 2>&1; then
            echo "CLEANUP WARN: could not stop node 1" >&2
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
        exit 1
    fi
    return "$test_status"
}
trap cleanup EXIT

echo "UNO USB: $master_usb"
echo "Sketch root: $SKETCH_ROOT"
echo "This test checks firmware protocol/state, not physical actuators or sensor readings."

if [[ "$skip_flash" -eq 0 ]]; then
    node_sketch="$SKETCH_ROOT/attiny1614/BlinkNode1/BlinkNode1.ino"
    if [[ ! -f "$node_sketch" ]]; then
        echo "Missing node 1 sketch: $node_sketch" >&2
        exit 1
    fi

    if [[ "$master_already_flashed" -eq 0 ]]; then
        if [[ ! -f "$STRICT_MASTER_SKETCH" || ! -f "$STRICT_MASTER_BUILDER" ]]; then
            echo "Missing strict Uno source package or builder." >&2
            echo "Expected sketch: $STRICT_MASTER_SKETCH" >&2
            echo "Expected builder: $STRICT_MASTER_BUILDER" >&2
            exit 1
        fi
        if ! command -v sha256sum >/dev/null 2>&1; then
            echo "sha256sum is required to verify the strict-source UNO image." >&2
            exit 1
        fi
        echo "Building strict master from: $STRICT_MASTER_SKETCH"
        proot-distro login ubuntu -- bash "$STRICT_MASTER_BUILDER" \
            "$STRICT_MASTER_SOURCE_DIR" "$STRICT_MASTER_HEX"
        if [[ ! -f "$STRICT_MASTER_HEX" ]]; then
            echo "Strict-source UNO build did not produce: $STRICT_MASTER_HEX" >&2
            exit 1
        fi
        if [[ "$(sha256sum -- "$STRICT_MASTER_HEX" | awk '{print $1}')" != "$STRICT_MASTER_SHA256" ]]; then
            echo "Refusing to flash an unverified UNO master image: $STRICT_MASTER_HEX" >&2
            echo "Expected SHA-256: $STRICT_MASTER_SHA256" >&2
            exit 1
        fi
        echo "Keeping the single node 1 connected to the D5/D6 bus during master flashing."
        flash_and_verify "strict-source UNO JTAG2UPDI master flash" \
            andruino-flash flash-platform \
            --usb-device "$master_usb" \
            uno \
            "$STRICT_MASTER_HEX"

        discovery_status=0
        discover_single_node || discovery_status=$?
        if [[ "$discovery_status" -ne 0 && "$discovery_status" -ne 2 ]]; then
            echo "Discovery failed or another node is connected; refusing to continue." >&2
            exit 1
        fi
    else
        echo "Skipping repeated master flash; reflash and verify the strict-source image first."
        if ! discover_single_node; then
            echo "Expected exactly node 1 after the master reflash; refusing to continue." >&2
            exit 1
        fi
    fi

    flash_and_verify "ATtiny1614 node 1 flash" \
        andruino-flash flash-sketch \
        --platform attiny1614 \
        --node 1 \
        --usb-device "$master_usb" \
        "$node_sketch"
else
    echo "Skipping UNO and node 1 reflashing (--no-flash)."
fi

if ! discover_single_node; then
    echo "Node 1 did not respond. Check power, common ground, and D5/PB3/PB2 wiring." >&2
    exit 1
fi
node_available=1

echo "=== Node 1 communication and command checks ==="
run_bus "OK MODE COMM;" MODE COMM
run_bus "NODE 1 PONG" PING 1
run_bus "NODE 1 VERSION ANDRUINO-ATTINY1614-PB0-6" VERSION 1
run_bus "NODE 1 STATUS COMM V=OFF M=OFF" STATUS 1
run_bus "NODE 1 BLINK 200" BLINK 1 200
run_bus "NODE 1 LED ON" LED 1 ON
run_bus "NODE 1 LED OFF" LED 1 OFF
run_bus "NODE 1 UPDI ENABLED" UPDI ENABLE 1
run_bus "NODE 1 UPDI DISABLED" UPDI DISABLE 1
run_bus "NODE 1 VALVE ON" VALVE 1 ON
run_bus "NODE 1 MOTOR FORWARD STARTED MS 60000" MOTOR 1 FORWARD 60000
run_bus "NODE 1 MOTION MOTOR FORWARD VALVE ON" MOTION_STATUS 1
run_bus "NODE 1 VALVE OFF" VALVE 1 OFF
run_bus "NODE 1 MOTOR REVERSE STARTED MS 60000" MOTOR 1 REVERSE 60000
run_bus "NODE 1 MOTION MOTOR REVERSE VALVE OFF" MOTION_STATUS 1
run_bus "NODE 1 MOTOR OFF" MOTOR 1 OFF
run_bus "NODE 1 STOPPED VALVE OFF MOTOR OFF" STOP 1
run_bus "NODE 1 MOTION MOTOR OFF VALVE OFF" MOTION_STATUS 1
run_bus "NODE 1 ENCODER -999999" READ_ENCODER 1
run_bus "NODE 1 ENCODER 0" RESET_ENCODER 1
run_bus "NODE 1 ANGLE -1" ANGLE 1

echo "=== Node 1 flash/communication mode checks ==="
run_bus "OK MODE FLASH;" MODE FLASH
run_bus "OK SELECT NODE 1;" SELECT 1
run_bus "NODE 1 STATUS FLASH" STATUS 1
run_bus "OK MODE COMM;" MODE COMM
run_bus "NODE 1 LED OFF" LED 1 OFF
run_bus "NODE 1 UPDI DISABLED" UPDI DISABLE 1
node_available=0

echo "PASS: UNO master and single ATtiny1614 node 1 checks passed."
