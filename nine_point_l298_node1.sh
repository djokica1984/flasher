#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

export PATH="$HOME/bin:$PATH"

skip_flash=0
master_usb="${UNO_USB:-}"
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
NODE_VERSION="ATTINY1614-L298N-AS5600-5"
VALVE_PULSE_MS=500
POSITION_TOLERANCE_COUNTS=32
HOME_TOLERANCE_COUNTS=8
node_available=0

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

bus_request() {
    local output response
    if ! output="$(andruino-flash bus --usb-device "$master_usb" "$@" 2>&1)"; then
        echo "FAIL: $* (bus command failed)" >&2
        echo "$output" >&2
        return 1
    fi
    response="$(awk '/BUS_RESPONSE / { sub(/^.*BUS_RESPONSE /, ""); last=$0 } END { print last }' <<<"$output")"
    if [[ -z "$response" ]]; then
        echo "FAIL: $* (no bus response)" >&2
        echo "$output" >&2
        return 1
    fi
    if [[ "$response" == ERR* || "$response" == *" ERR "* ]]; then
        echo "FAIL: $* -> $response" >&2
        return 1
    fi
    printf '%s\n' "$response"
}

run_bus() {
    local expected="$1"
    shift
    local response
    response="$(bus_request "$@")" || return 1
    if [[ "$response" != *"$expected"* ]]; then
        echo "FAIL: $* (expected '$expected', received '$response')" >&2
        return 1
    fi
    echo "PASS: $* -> $response"
}

discover_single_node() {
    local response
    response="$(bus_request DISCOVER)" || return 1
    if [[ "$response" != "OK NODES 1" ]]; then
        echo "FAIL: expected only node 1 on the bus, received: $response" >&2
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
    echo "PASS: L298N Node 1 flash read-back verified"
}

read_status() {
    local response
    response="$(bus_request STATUS 1)" || return 1
    local pattern='^NODE 1 STATUS MOTOR (OFF|FORWARD|REVERSE) VALVE (OFF|ON) ENCODER (-?[0-9]+) UPDI (ENABLED|DISABLED) MOTION (IDLE|MOVING|FAULT) TARGET (-?[0-9]+) ANGLE (-?[0-9]+)$'
    if [[ ! "$response" =~ $pattern ]]; then
        echo "FAIL: unexpected L298N status: $response" >&2
        return 1
    fi
    STATUS_RESPONSE="$response"
    STATUS_MOTOR="${BASH_REMATCH[1]}"
    STATUS_VALVE="${BASH_REMATCH[2]}"
    STATUS_ENCODER="${BASH_REMATCH[3]}"
    STATUS_MOTION="${BASH_REMATCH[5]}"
    STATUS_TARGET="${BASH_REMATCH[6]}"
    STATUS_ANGLE="${BASH_REMATCH[7]}"
    if [[ "$STATUS_ENCODER" == "-999999" ]] ||
       (( STATUS_ANGLE < 0 || STATUS_ANGLE > 4095 )); then
        echo "FAIL: AS5600 is unavailable: $response" >&2
        return 1
    fi
}

read_motion_status() {
    local response
    response="$(bus_request MOTION_STATUS 1)" || return 1
    local pattern='^NODE 1 MOTION_STATUS (IDLE|MOVING|FAULT) ENCODER (-?[0-9]+) TARGET (-?[0-9]+) ANGLE (-?[0-9]+)$'
    if [[ ! "$response" =~ $pattern ]]; then
        echo "FAIL: unexpected motion status: $response" >&2
        return 1
    fi
    MOTION_STATUS="${BASH_REMATCH[1]}"
    MOTION_ENCODER="${BASH_REMATCH[2]}"
    MOTION_TARGET="${BASH_REMATCH[3]}"
    MOTION_ANGLE="${BASH_REMATCH[4]}"
    if [[ "$MOTION_ENCODER" == "-999999" ]] ||
       (( MOTION_ANGLE < 0 || MOTION_ANGLE > 4095 )); then
        echo "FAIL: AS5600 is unavailable: $response" >&2
        return 1
    fi
}

log_motion_status() {
    read_motion_status || return 1
    printf '%s motion=%s counts=%s target=%s angle=%s\n' \
        "$(date '+%Y-%m-%dT%H:%M:%S%z')" \
        "$MOTION_STATUS" "$MOTION_ENCODER" "$MOTION_TARGET" "$MOTION_ANGLE"
}

run_position_move() {
    local direction="$1"
    local distance="$2"
    local expected_sign="$3"
    local start_count deadline delta

    read_status || return 1
    if [[ "$STATUS_MOTOR" != "OFF" || "$STATUS_VALVE" != "OFF" ||
          "$STATUS_MOTION" != "IDLE" ]]; then
        echo "FAIL: outputs/motion are not idle before MOVE: $STATUS_RESPONSE" >&2
        return 1
    fi
    start_count="$STATUS_ENCODER"
    run_bus "MOVE STARTED $direction COUNTS $distance" \
        MOVE 1 "$direction" "$distance" || return 1

    deadline=$((SECONDS + 45))
    while (( SECONDS < deadline )); do
        log_motion_status || return 1
        if [[ "$MOTION_STATUS" == "FAULT" ]]; then
            echo "FAIL: node stopped in motion FAULT." >&2
            return 1
        fi
        if [[ "$MOTION_STATUS" == "IDLE" ]]; then
            read_status || return 1
            if [[ "$STATUS_MOTOR" != "OFF" || "$STATUS_VALVE" != "OFF" ]]; then
                echo "FAIL: node reported IDLE with an output active." >&2
                return 1
            fi
            delta=$((STATUS_ENCODER - start_count))
            if (( delta == 0 ||
                  (expected_sign != 0 && delta * expected_sign <= 0) )); then
                echo "FAIL: encoder direction did not match $direction (delta=$delta)." >&2
                return 1
            fi
            local progress=$((delta * expected_sign))
            if (( expected_sign == 0 )); then
                progress="${delta#-}"
            fi
            if (( progress - distance > POSITION_TOLERANCE_COUNTS )); then
                echo "FAIL: target overshoot exceeds ${POSITION_TOLERANCE_COUNTS} counts (requested=$distance actual=$progress)." >&2
                return 1
            fi
            MOVE_RESULT_DELTA="$delta"
            echo "PASS: MOVE $direction requested=$distance actual=$delta"
            return 0
        fi
    done
    echo "FAIL: position move exceeded host timeout." >&2
    return 1
}

move_to_encoder_target() {
    local target="$1"
    local forward_sign="$2"
    local difference distance direction expected_sign error

    read_status || return 1
    difference=$((target - STATUS_ENCODER))
    if (( difference == 0 )); then
        echo "PASS: already at target=$target angle=$STATUS_ANGLE"
        return 0
    fi
    if (( difference * forward_sign > 0 )); then
        direction="FORWARD"
        expected_sign="$forward_sign"
    else
        direction="REVERSE"
        expected_sign="$((-forward_sign))"
    fi
    distance="${difference#-}"
    if (( distance > 512 )); then
        echo "FAIL: requested correction exceeds one point interval ($distance counts)." >&2
        return 1
    fi
    run_position_move "$direction" "$distance" "$expected_sign" || return 1
    read_status || return 1
    error=$((target - STATUS_ENCODER))
    if (( error < 0 )); then error=$((-error)); fi
    if (( error > POSITION_TOLERANCE_COUNTS )); then
        echo "FAIL: stopped $error counts from station target=$target." >&2
        return 1
    fi
    echo "PASS: station target=$target actual=$STATUS_ENCODER error=$error angle=$STATUS_ANGLE"
}

home_to_absolute_zero() {
    local angle_delta distance direction expected_sign

    read_status || return 1
    if [[ "$STATUS_MOTOR" != "OFF" || "$STATUS_VALVE" != "OFF" ||
          "$STATUS_MOTION" != "IDLE" ]]; then
        echo "FAIL: cannot home unless motor, valve, and motion are idle: $STATUS_RESPONSE" >&2
        return 1
    fi

    angle_delta=$(((4096 - STATUS_ANGLE + 2048) % 4096 - 2048))
    distance="${angle_delta#-}"
    if (( distance <= HOME_TOLERANCE_COUNTS )); then
        echo "PASS: already at absolute angle $STATUS_ANGLE (home tolerance ${HOME_TOLERANCE_COUNTS} counts)"
        HOME_FORWARD_SIGN=0
        return 0
    fi

    echo "Calibrating forward direction before homing to AS5600 angle 0."
    run_position_move FORWARD 32 0 || return 1
    HOME_FORWARD_SIGN=1
    if (( MOVE_RESULT_DELTA < 0 )); then HOME_FORWARD_SIGN=-1; fi
    read_status || return 1

    while true; do
        angle_delta=$(((4096 - STATUS_ANGLE + 2048) % 4096 - 2048))
        distance="${angle_delta#-}"
        if (( distance <= HOME_TOLERANCE_COUNTS )); then
            echo "PASS: homed to absolute angle $STATUS_ANGLE (error=$distance counts)"
            return 0
        fi

        if (( distance > 512 )); then distance=512; fi
        if (( angle_delta * HOME_FORWARD_SIGN > 0 )); then
            direction="FORWARD"
            expected_sign="$HOME_FORWARD_SIGN"
        else
            direction="REVERSE"
            expected_sign="$((-HOME_FORWARD_SIGN))"
        fi
        echo "HOMING angle=$STATUS_ANGLE delta=$angle_delta step=$distance direction=$direction"
        run_position_move "$direction" "$distance" "$expected_sign" || return 1
        read_status || return 1
    done
}

pulse_valve() {
    local before
    read_status || return 1
    if [[ "$STATUS_MOTOR" != "OFF" || "$STATUS_MOTION" != "IDLE" ||
          "$STATUS_VALVE" != "OFF" ]]; then
        echo "FAIL: valve may only pulse with motor and motion idle: $STATUS_RESPONSE" >&2
        return 1
    fi
    before="$STATUS_ENCODER"
    run_bus "NODE 1 VALVE ON" VALVE 1 ON || return 1
    sleep "$(awk -v ms="$VALVE_PULSE_MS" 'BEGIN { printf "%.3f", ms / 1000 }')"
    run_bus "NODE 1 VALVE OFF" VALVE 1 OFF || return 1
    read_status || return 1
    if [[ "$STATUS_MOTOR" != "OFF" || "$STATUS_MOTION" != "IDLE" ||
          "$STATUS_VALVE" != "OFF" ]]; then
        echo "FAIL: outputs did not return to idle after valve pulse." >&2
        return 1
    fi
    if (( STATUS_ENCODER - before > 8 || before - STATUS_ENCODER > 8 )); then
        echo "FAIL: encoder moved while the motor was supposed to be idle." >&2
        return 1
    fi
    echo "PASS: valve pulsed ${VALVE_PULSE_MS} ms at stopped angle=$STATUS_ANGLE"
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
        if ! andruino-flash bus --usb-device "$master_usb" VALVE 1 OFF >/dev/null 2>&1; then
            echo "CLEANUP WARN: could not turn valve off" >&2
            cleanup_failed=1
        fi
        if ! andruino-flash bus --usb-device "$master_usb" MODE COMM >/dev/null 2>&1; then
            echo "CLEANUP WARN: could not restore COMM mode" >&2
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
echo "Node sketch: $NODE_SKETCH"
echo "Cycle: home to absolute angle 0, visit 9 stations, then return to absolute point 0."
echo "The node samples the AS5600 locally; Termux status lines are supervisory."
echo "Valve pulse: ${VALVE_PULSE_MS} ms at every forward and reverse station."
echo "Keep the physical actuator-supply cutoff within reach."

if [[ ! -f "$NODE_SKETCH" ]]; then
    echo "Missing L298N node sketch: $NODE_SKETCH" >&2
    exit 1
fi
discover_single_node
node_available=1
if [[ "$skip_flash" -eq 0 ]]; then
    flash_and_verify
    discover_single_node
else
    echo "Skipping reflash (--no-flash)."
fi
run_bus "OK MODE COMM" MODE COMM
run_bus "NODE 1 VERSION $NODE_VERSION" VERSION 1
read_status
if [[ "$STATUS_MOTOR" != "OFF" || "$STATUS_VALVE" != "OFF" ||
      "$STATUS_MOTION" != "IDLE" ]]; then
    echo "FAIL: node is not idle after initialization: $STATUS_RESPONSE" >&2
    exit 1
fi
run_bus "NODE 1 STOPPED VALVE OFF MOTOR OFF" STOP 1
echo "Homing shaft to absolute AS5600 angle 0 (tolerance ${HOME_TOLERANCE_COUNTS} counts)."
home_to_absolute_zero
point_zero_count="$STATUS_ENCODER"

echo "Starting nine-point forward sweep from absolute angle 0 (encoder count=$point_zero_count)."
pulse_valve
target=$(((4096 + 4) / 9))
echo "POINT 1/9 offset=$target"
run_position_move FORWARD "$target" 0
forward_delta="$MOVE_RESULT_DELTA"
forward_sign=1
if (( forward_delta < 0 )); then forward_sign=-1; fi
read_status || exit 1
target=$((point_zero_count + forward_sign * target))
error=$((target - STATUS_ENCODER))
if (( error < 0 )); then error=$((-error)); fi
if (( error > POSITION_TOLERANCE_COUNTS )); then
    echo "FAIL: stopped $error counts from station 1 target=$target." >&2
    exit 1
fi
echo "PASS: station 1/9 target=$target actual=$STATUS_ENCODER error=$error angle=$STATUS_ANGLE"
pulse_valve

for ((point = 2; point <= 8; point++)); do
    target=$((point_zero_count + forward_sign * ((point * 4096 + 4) / 9)))
    echo "POINT $point/9 target=$target"
    move_to_encoder_target "$target" "$forward_sign"
    pulse_valve
done

echo "Returning through the stations in reverse; pulsing the valve at each stopped station."
for ((point = 7; point >= 0; point--)); do
    target=$((point_zero_count + forward_sign * ((point * 4096 + 4) / 9)))
    echo "RETURN POINT $point/9 target=$target"
    move_to_encoder_target "$target" "$forward_sign"
    pulse_valve
done

read_status
if [[ "$STATUS_MOTOR" != "OFF" || "$STATUS_VALVE" != "OFF" ||
      "$STATUS_MOTION" != "IDLE" ]]; then
    echo "FAIL: final outputs/motion are not idle: $STATUS_RESPONSE" >&2
    exit 1
fi
run_bus "NODE 1 STOPPED VALVE OFF MOTOR OFF" STOP 1
node_available=0
echo "CYCLE_COMPLETE: homed to absolute angle 0, visited 9 stations, returned to point 0, 17 valve pulses; outputs OFF."
