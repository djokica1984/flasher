#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

export PATH="$HOME/bin:$PATH"

interval_seconds="0.5"
flash_node=0
master_usb=""

usage() {
    echo "Usage: $0 [--flash] [--interval SECONDS] [UNO_USB_PATH]"
    echo "  --flash             Build and flash AS5600Node1 before reading"
    echo "  --interval SECONDS  Delay between reads (default: 0.5)"
}

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --flash)
            flash_node=1
            shift
            ;;
        --interval)
            if [[ "$#" -lt 2 ]]; then
                echo "Missing value for --interval." >&2
                usage >&2
                exit 2
            fi
            interval_seconds="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        -*)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            if [[ -n "$master_usb" ]]; then
                echo "Only one UNO USB path may be specified." >&2
                usage >&2
                exit 2
            fi
            master_usb="$1"
            shift
            ;;
    esac
done

if [[ ! "$interval_seconds" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   ! awk -v value="$interval_seconds" 'BEGIN { exit !(value > 0) }'; then
    echo "--interval must be a positive number of seconds." >&2
    exit 2
fi

SKETCH_ROOT="${ANDRUINO_SKETCH_ROOT:-/storage/emulated/0/Download/andruino}"
NODE_SKETCH="$SKETCH_ROOT/attiny1614/AS5600Node1/AS5600Node1.ino"

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
        echo "Disconnect other adapters or pass the UNO path as the final argument." >&2
        exit 1
    fi
    master_usb="${master_paths[0]}"
fi

if [[ "$flash_node" -eq 1 ]]; then
    if [[ ! -f "$NODE_SKETCH" ]]; then
        echo "Missing AS5600 node sketch: $NODE_SKETCH" >&2
        exit 1
    fi
    echo "Flashing AS5600 Node 1 via $master_usb..." >&2
    if ! flash_output="$(andruino-flash flash-sketch \
        --platform attiny1614 --node 1 --usb-device "$master_usb" \
        "$NODE_SKETCH" 2>&1)"; then
        echo "Failed to flash AS5600 Node 1:" >&2
        echo "$flash_output" >&2
        exit 1
    fi
    echo "$flash_output" >&2
    if ! grep -Eq '[0-9,]+ bytes of flash verified' <<<"$flash_output"; then
        echo "Flash did not report AVRDUDE read-back verification." >&2
        exit 1
    fi
fi

if ! version_output="$(andruino-flash bus --usb-device "$master_usb" VERSION 1 2>&1)"; then
    echo "Could not query Node 1 firmware version:" >&2
    echo "$version_output" >&2
    exit 1
fi
if ! grep -Fq 'NODE 1 VERSION ATTINY1614-AS5600-1' <<<"$version_output"; then
    echo "Node 1 is not running the AS5600 firmware. Run this script with --flash." >&2
    echo "$version_output" >&2
    exit 1
fi

echo "Streaming Node 1 angle readings from $master_usb; press Ctrl+C to stop." >&2
echo "Output: timestamp raw-counts degrees" >&2

while true; do
    if ! response="$(andruino-flash bus --usb-device "$master_usb" ANGLE 1 2>&1)"; then
        echo "ANGLE request failed:" >&2
        echo "$response" >&2
        exit 1
    fi
    angle="$(grep -Eo 'NODE 1 ANGLE -?[0-9]+' <<<"$response" | tail -n 1 | awk '{print $4}')"
    if [[ "$angle" == "-1" ]]; then
        echo "AS5600 returned ANGLE -1; check sensor power, I2C wiring, and pull-ups." >&2
        echo "$response" >&2
        exit 1
    fi
    if [[ ! "$angle" =~ ^[0-9]+$ ]] || (( angle > 4095 )); then
        echo "Invalid AS5600 angle response; expected 0..4095:" >&2
        echo "$response" >&2
        exit 1
    fi
    degrees="$(awk -v raw="$angle" 'BEGIN { printf "%.2f", raw * 360 / 4096 }')"
    printf '%s %s %s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$angle" "$degrees"
    sleep "$interval_seconds"
done
