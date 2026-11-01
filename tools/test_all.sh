#!/bin/bash
# Runs the whole nornOS test suite and prints one line per result:
#   host unit tests, then every applications/tc* in simavr for each AVR board, then on the
#   ESP32 board if its serial port exists. Exit code 1 if anything failed.
#
#   TCS="tc01 tc02"        subset of test applications (default: all tc*)
#   AVR_BOARDS="uno"       default "uno mega2560"
#   ESP_PORT=/dev/ttyUSB0  ESP32 serial port; skipped when absent. ESP_PORT= disables.
#   AVR_HW="mega2560:/dev/ttyACM0 uno:/dev/ttyACM1"
#                          real AVR boards to flash and monitor; default none
#   QEMU_ESP=~/dev/tools/qemu-esp  ESP32 emulator; the esp32 tests run there when it exists
#   KEEP_LOGS=1            keep build/test-logs/*.log of every run (always kept on failure)
#   MON_SECS=6             seconds to watch a board per test; MON_SECS_tc05=16 per test
set -u
cd "$(dirname "$0")/.."

TCS=${TCS:-$(ls -d applications/tc* | xargs -n1 basename)}
AVR_BOARDS=${AVR_BOARDS-"uno mega2560"}
ESP_PORT=${ESP_PORT-/dev/ttyUSB0}
AVR_HW=${AVR_HW-}
ESP_PYTHON=${ESP_PYTHON:-$HOME/dev/tools/esp-venv/bin/python}
LOGS=build/test-logs
mkdir -p "$LOGS"
MON_SECS=${MON_SECS:-6}
MON_SECS_tc05=${MON_SECS_tc05:-20}

mon_secs() {   # app -> seconds
    local v="MON_SECS_$1"
    echo "${!v:-$MON_SECS}"
}

failures=0
total=0

report() {   # name result detail
    total=$((total + 1))
    printf '%-24s %-5s %s\n' "$1" "$2" "$3"
    [ "$2" = PASS ] || failures=$((failures + 1))
}

clean_log() { sed 's/\x1b\[[0-9;]*m//g' | tr -d '\r' | sed 's/\.\.$//'; }

runs_on() {   # app board -> true if the app's app.mk has no ONLY_BOARDS or lists the board
    local only
    only=$(sed -n 's/^ONLY_BOARDS *= *//p' "applications/$1/app.mk" 2>/dev/null)
    [ -z "$only" ] && return 0
    case " $only " in *" $2 "*) return 0;; esac
    return 1
}

result_of() {   # app logfile -> PASS/FAIL/NONE and the detail line
    local app=$1 log=$2 line
    line=$(grep -a -o "RESULT $app [A-Z]*" "$log" | tail -1)
    if [ -z "$line" ]; then
        echo "NONE no RESULT line (crash, hang or assertion); see $log"
    else
        echo "${line##* } $(grep -a 'CHECK FAILED\|ASSERTION\|FATAL' "$log" | head -1)"
    fi
}

# ---- host unit tests
log=$LOGS/host.log
if make -s host-test > "$log" 2>&1; then
    report "host-test" PASS "$(grep -c PASS "$log") programs"
else
    report "host-test" FAIL "see $log"
fi

# ---- AVR boards in simavr. simavr has no bootloader, so it also runs the variant that
#      resets through the watchdog (SIM_EXTRA_tc05), which real boards may not survive.
SIM_EXTRA_tc05=${SIM_EXTRA_tc05:--DRTOS_CFG_AVR_RESET_VIA_WDT=1}
for board in $AVR_BOARDS; do
    for app in $TCS; do
        runs_on "$app" "$board" || continue
        v="SIM_EXTRA_$app"; extra="${!v:-}"
        log=$LOGS/$app-$board.log
        if ! make -s APP="$app" BOARD="$board" EXTRA_CFLAGS="$extra" > "$log" 2>&1; then
            report "$app $board" FAIL "build failed; see $log"; continue
        fi
        timeout 300 make -s APP="$app" BOARD="$board" EXTRA_CFLAGS="$extra" sim 2>&1 | clean_log > "$log"
        read -r res detail <<< "$(result_of "$app" "$log")"
        report "$app $board" "$res" "$detail"
    done
done

# ---- ESP32 in QEMU
QEMU_ESP=${QEMU_ESP:-$HOME/dev/tools/qemu-esp}
if [ -x "$QEMU_ESP/bin/qemu-system-xtensa" ]; then
    for app in $TCS; do
        extra="-DTC_TOLERANCE_PERMILLE=30"
        log=$LOGS/$app-esp32-qemu.log
        if ! make -s APP="$app" BOARD=esp32 EXTRA_CFLAGS="$extra" > "$log" 2>&1; then
            report "$app esp32 qemu" FAIL "build failed; see $log"; continue
        fi
        make -s APP="$app" BOARD=esp32 EXTRA_CFLAGS="$extra" QEMU_ESP="$QEMU_ESP" SIM_SECONDS="$(mon_secs "$app")" sim 2>&1 | clean_log > "$log"
        read -r res detail <<< "$(result_of "$app" "$log")"
        report "$app esp32 qemu" "$res" "$detail"
    done
else
    echo "esp32 qemu: $QEMU_ESP not present, skipped"
fi

# ---- AVR boards on real hardware
for entry in $AVR_HW; do
    board=${entry%%:*}; port=${entry#*:}
    if [ ! -e "$port" ]; then
        echo "$board: $port not present, skipped"; continue
    fi
    for app in $TCS; do
        runs_on "$app" "$board" || continue
        log=$LOGS/$app-$board-hw.log
        if ! make -s APP="$app" BOARD="$board" > "$log" 2>&1; then
            report "$app $board hw" FAIL "build failed; see $log"; continue
        fi
        if ! make -s APP="$app" BOARD="$board" PORT="$port" upload >> "$log" 2>&1; then
            report "$app $board hw" FAIL "upload failed; see $log"; continue
        fi
        "$ESP_PYTHON" tools/monitor.py "$port" 115200 "$(mon_secs "$app")" 2>&1 | clean_log > "$log"
        read -r res detail <<< "$(result_of "$app" "$log")"
        report "$app $board hw" "$res" "$detail"
    done
done

# ---- ESP32 on the real board
if [ -n "$ESP_PORT" ] && [ -e "$ESP_PORT" ]; then
    for app in $TCS; do
        log=$LOGS/$app-esp32.log
        if ! make -s APP="$app" BOARD=esp32 > "$log" 2>&1; then
            report "$app esp32" FAIL "build failed; see $log"; continue
        fi
        if ! make -s APP="$app" BOARD=esp32 PORT="$ESP_PORT" upload >> "$log" 2>&1; then
            report "$app esp32" FAIL "upload failed; see $log"; continue
        fi
        "$ESP_PYTHON" tools/monitor.py "$ESP_PORT" 115200 "$(mon_secs "$app")" 2>&1 | clean_log > "$log"
        read -r res detail <<< "$(result_of "$app" "$log")"
        report "$app esp32" "$res" "$detail"
    done
elif [ -n "$ESP_PORT" ]; then
    echo "esp32: $ESP_PORT not present, skipped"
fi

echo "----"
echo "$((total - failures)) of $total passed"
[ "${KEEP_LOGS:-0}" = 1 ] || [ $failures -gt 0 ] || rm -rf "$LOGS"
[ $failures -eq 0 ]
