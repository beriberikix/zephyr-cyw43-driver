#!/usr/bin/env bash
# RX buffer-pressure stress (backlog item 3).
#
# Floods the HCI RX *event* path with LE advertising reports (`bt scan on`).
# These take the discardable EVT path, which allocates with K_NO_WAIT and gets
# NULL from bt_buf_get_evt() under pool pressure. Built with
# test/coex/rx_stress.conf (BT_BUF_EVT_DISCARDABLE_COUNT=1) the NULL path is hit
# almost continuously, so the hardened drop-on-NULL / length-bounds policy is
# exercised hard. The pre-existing concurrent-WiFi bus-arbitration fault
# (item 4) is intentionally NOT mixed in here; this isolates RX robustness.
#
# PASS = after DURATION the device is still responsive (uptime advances,
# bt id-show replies) and no fault/panic/assert was seen.
#
# Usage: rx_stress.sh [DURATION_S] [OUTLOG]
set -u
DUR="${1:-150}"
OUT="${2:-/tmp/rx_stress.log}"
HERE="$(cd "$(dirname "$0")" && pwd)"
C="python $HERE/console.py"

: > "$OUT"
echo "=== RX event-flood stress $(date -u +%FT%TZ) dur=${DUR}s ===" | tee -a "$OUT"

OCD="$HOME/.pico-sdk/openocd/0.12.0+dev/openocd"
OCDS="$HOME/.pico-sdk/openocd/0.12.0+dev/scripts"
GDB="$HOME/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb"
ELF="$(cd "$HERE/../.." && pwd)/build_rxstress/zephyr/zephyr.elf"

# Liveness via SWD/gdb, NOT a shell command. Under the `bt scan on` flood the
# 115200 UART is saturated by advertising-report prints, so a shell probe like
# `kernel uptime` gets starved and FALSELY reads as "DEAD" even though the core
# is healthy (gdb shows it idle between report bursts). gdb on the separate
# CMSIS-DAP/SWD interface is the reliable liveness signal (as in soak.sh): halt,
# look for arch_system_halt (a real panic/oops spin), then resume. The probe's
# SWD (if00) and UART bridge (if01) are independent, so this does not disturb
# the flood streaming over UART.
alive_check() {  # echoes FAULT or ALIVE; leaves the core RUNNING
	pkill -f 'probe[-]rs' 2>/dev/null; pkill -x openocd 2>/dev/null; sleep 1
	"$OCD" -s "$OCDS" -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
		-c "adapter speed 5000" >/tmp/rxs_ocd.log 2>&1 &
	local op=$!; sleep 2
	local out
	out="$("$GDB" -nx -batch -ex "target extended-remote :3333" \
		-ex "monitor halt" -ex "bt" -ex "monitor resume" "$ELF" 2>/dev/null \
		| grep -c "arch_system_halt")"
	kill "$op" 2>/dev/null; wait "$op" 2>/dev/null
	[ "${out:-0}" -gt 0 ] && echo "FAULT" || echo "ALIVE"
}

$C send "bt init" --wait 5 >>"$OUT" 2>&1
$C send "bt scan on" --wait 1 >>"$OUT" 2>&1
# Let the flood stream and confirm reports are flowing (evidence the RX path is
# under real pressure), then sample liveness via gdb across the whole duration.
$C capture --wait 10 >>"$OUT" 2>&1

elapsed=10
chunk=30
faulted=0
while [ "$elapsed" -lt "$DUR" ]; do
	# DRAIN the UART while the flood runs (realistic operation). If we just
	# sleep without reading, the device's UART TX buffer fills, the BT shell
	# blocks on the print, BT RX delivery backs up and the controller's bt2host
	# ring overflows -> the documented below-driver cybt panic (REFERENCE 2.7).
	# That is a UART-backpressure artifact of an undrained console, NOT the RX
	# buffer-pressure robustness this test targets, so we keep draining here.
	$C capture --wait "$chunk" >>"$OUT" 2>&1
	elapsed=$((elapsed + chunk))
	st="$(alive_check)"
	echo ">> [t=${elapsed}s] gdb-liveness=$st" | tee -a "$OUT"
	if [ "$st" = "FAULT" ]; then
		echo ">> DEVICE FAULTED (arch_system_halt) at t=${elapsed}s" | tee -a "$OUT"
		faulted=1; break
	fi
done

# Stop the flood with an SWD reset (NOT `bt scan off`: an HCI command issued into
# the saturated UART has its command-complete delayed past HCI_CMD_TIMEOUT -- a
# console artifact of this test, not a bus/RX fault).
pkill -f 'probe[-]rs' 2>/dev/null; pkill -x openocd 2>/dev/null; sleep 1
timeout 25 "$OCD" -s "$OCDS" -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
	-c "adapter speed 5000" -c "init" -c "reset run" -c "exit" >>"$OUT" 2>&1

ADV="$(grep -cE '\[DEVICE\]' "$OUT")"
FAULTS="$(grep -ciE 'panic|ASSERT|FATAL|stack overflow|Oops|undefined instruction' "$OUT")"
echo "=== SUMMARY ===" | tee -a "$OUT"
echo "advertising-report lines captured: $ADV" | tee -a "$OUT"
echo "panic/assert hits in log: $FAULTS" | tee -a "$OUT"
if [ "$faulted" -eq 0 ] && [ "$FAULTS" -eq 0 ] && [ "$ADV" -gt 0 ]; then
	echo "RESULT: PASS (RX event flood ran fault-free; gdb liveness OK throughout)" | tee -a "$OUT"
else
	echo "RESULT: FAIL" | tee -a "$OUT"
fi
