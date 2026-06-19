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

$C send "bt init" --wait 5 >>"$OUT" 2>&1
UP0="$($C send "kernel uptime" --wait 2 | grep -oE 'Uptime: [0-9]+' | grep -oE '[0-9]+')"
echo ">> uptime before: ${UP0:-?} ms" | tee -a "$OUT"

$C send "bt scan on" --wait 1 >>"$OUT" 2>&1

# Capture the flood in chunks, checking liveness between chunks.
elapsed=0
chunk=30
faulted=0
while [ "$elapsed" -lt "$DUR" ]; do
	$C capture --wait "$chunk" >>"$OUT" 2>&1
	elapsed=$((elapsed + chunk))
	UPN="$($C send "kernel uptime" --wait 2 | grep -oE 'Uptime: [0-9]+' | grep -oE '[0-9]+')"
	echo ">> [t=${elapsed}s] uptime=${UPN:-DEAD} ms" | tee -a "$OUT"
	if [ -z "${UPN:-}" ]; then echo ">> DEVICE UNRESPONSIVE at t=${elapsed}s" | tee -a "$OUT"; faulted=1; break; fi
done

$C send "bt scan off" --wait 2 >>"$OUT" 2>&1
UP1="$($C send "kernel uptime" --wait 2 | grep -oE 'Uptime: [0-9]+' | grep -oE '[0-9]+')"
ID="$($C send "bt id-show" --wait 2 | grep -oE '([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}' | head -1)"
echo ">> uptime after: ${UP1:-DEAD} ms; bt id-show: ${ID:-<none>}" | tee -a "$OUT"

ADV="$(grep -cE '\[DEVICE\]' "$OUT")"
FAULTS="$(grep -ciE 'fault|panic|ASSERT|FATAL|stack overflow|Oops|undefined instruction' "$OUT")"
echo "=== SUMMARY ===" | tee -a "$OUT"
echo "advertising-report lines captured: $ADV" | tee -a "$OUT"
echo "fault/panic/assert hits: $FAULTS" | tee -a "$OUT"
if [ "$faulted" -eq 0 ] && [ -n "${UP1:-}" ] && [ -n "${UP0:-}" ] && \
   [ "$UP1" -gt "$UP0" ] && [ -n "${ID:-}" ] && [ "$FAULTS" -eq 0 ]; then
	echo "RESULT: PASS (responsive after event flood, zero faults)" | tee -a "$OUT"
else
	echo "RESULT: FAIL" | tee -a "$OUT"
fi
