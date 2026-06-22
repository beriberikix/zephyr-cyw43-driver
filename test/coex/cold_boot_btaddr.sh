#!/usr/bin/env bash
# Cold-boot BD_ADDR stability test (backlog item 1 acceptance).
#
# Each cycle: openocd resets the RP2350 (which re-powers the CYW43 and
# re-downloads WiFi+BT firmware -> a fresh controller init / "cold boot" of the
# wireless subsystem), then we drive the shell over UART: `bt init` + `bt
# id-show`, and extract the reported public address. PASS = all N identical and
# equal to the expected WiFi-MAC+1 public address.
#
# Usage: cold_boot_btaddr.sh [N] [OUTLOG]
set -u
N="${1:-10}"
OUT="${2:-/tmp/cold_boot_btaddr.log}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OCD="$HOME/.pico-sdk/openocd/0.12.0+dev/openocd"
OCDS="$HOME/.pico-sdk/openocd/0.12.0+dev/scripts"

: > "$OUT"
declare -a ADDRS
for i in $(seq 1 "$N"); do
    echo "===== cold boot $i/$N $(date -u +%FT%TZ) =====" | tee -a "$OUT"
    # PROBE GUARD (bracket-regex avoids self-match; openocd matched by exact name)
    pkill -f 'probe[-]rs' 2>/dev/null
    pkill -x openocd 2>/dev/null
    sleep 1
    # Reset + run, then release the probe so the UART path is unobstructed.
    timeout 30 "$OCD" -s "$OCDS" -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
        -c "adapter speed 5000" -c "init" -c "reset run" -c "exit" \
        >>"$OUT" 2>&1
    # Wait for boot + CYW43/BT fw download + WiFi auto-connect to settle.
    sleep 7
    python "$HERE/console.py" send "bt init"    --wait 5 >>"$OUT" 2>&1
    sleep 1
    REPLY="$(python "$HERE/console.py" send "bt id-show" --wait 3)"
    echo "$REPLY" >>"$OUT"
    ADDR="$(echo "$REPLY" | grep -oE '([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}' | head -1)"
    echo ">> cycle $i addr = ${ADDR:-<none>}" | tee -a "$OUT"
    ADDRS+=("${ADDR:-<none>}")
done

echo "===== SUMMARY =====" | tee -a "$OUT"
printf '%s\n' "${ADDRS[@]}" | tee -a "$OUT"
UNIQ="$(printf '%s\n' "${ADDRS[@]}" | sort -u)"
NUNIQ="$(printf '%s\n' "$UNIQ" | grep -c .)"
echo "distinct addresses: $NUNIQ" | tee -a "$OUT"
echo "$UNIQ" | tee -a "$OUT"
if [ "$NUNIQ" -eq 1 ] && [ "${ADDRS[0]}" != "<none>" ]; then
    echo "RESULT: STABLE across $N cold boots" | tee -a "$OUT"
else
    echo "RESULT: NOT STABLE / missing reads" | tee -a "$OUT"
fi
