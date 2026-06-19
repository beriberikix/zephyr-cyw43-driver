#!/usr/bin/env bash
# Characterization soak (WiFi+BLE coexistence) — quantify the residual
# command-timeout failure rate with the current driver fixes (poll priority +
# drain). Per cycle: SWD-reset the Pico, let it boot + auto-connect WiFi + start
# the BLE notify peripheral, drive concurrent WiFi load (Pico->8.8.8.8 ping +
# host->Pico ping), then run the bleak central (connect + subscribe + count
# notifications) for up to MAX_CONN_S or until disconnect. After each cycle a
# gdb halt classifies the end state as FAULT (in arch_system_halt) vs alive.
# Aggregates: faults, total connected time, MTBF, notification stats.
#
# Prereq: flash the soak build first:
#   west build -p always -b rpi_pico2/rp2350a/m33/w -d build_soak app \
#     -- -DEXTRA_CONF_FILE="$PWD/app/local.conf;$PWD/test/coex/soak.conf"
#   <openocd program build_soak/zephyr/zephyr.elf>
#
# Usage: soak.sh [CYCLES] [MAX_CONN_S] [PICO_IP] [BT_ADDR]
set -u
CYCLES="${1:-8}"
MAX_CONN_S="${2:-90}"
PICO_IP="${3:-192.168.4.28}"
BT_ADDR="${4:-88:A2:9E:D1:6D:A0}"
BOOT_S=11
HERE="$(cd "$(dirname "$0")" && pwd)"
ELF="$(cd "$HERE/../.." && pwd)/build_soak/zephyr/zephyr.elf"
OCD="$HOME/.pico-sdk/openocd/0.12.0+dev/openocd"
OCDS="$HOME/.pico-sdk/openocd/0.12.0+dev/scripts"
GDB="$HOME/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb"
RESULTS="$HERE/results"
mkdir -p "$RESULTS"
STAMP="$(date -u +%Y%m%d_%H%M%S)"
LOG="$RESULTS/soak_${STAMP}.log"

ocd_reset() { timeout 30 "$OCD" -s "$OCDS" -f interface/cmsis-dap.cfg \
	-f target/rp2350.cfg -c "adapter speed 5000" -c "init" -c "reset run" \
	-c "exit" >>"$LOG" 2>&1; }

# Returns "FAULT" if the cm0 core is sitting in arch_system_halt, else "ALIVE".
fault_check() {
	"$OCD" -s "$OCDS" -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
		-c "adapter speed 5000" >/tmp/soak_ocd.log 2>&1 &
	local op=$!; sleep 2
	local out
	out="$("$GDB" -nx -batch -ex "target extended-remote :3333" \
		-ex "monitor halt" -ex "bt" "$ELF" 2>/dev/null \
		| grep -c "arch_system_halt")"
	kill "$op" 2>/dev/null; wait "$op" 2>/dev/null
	[ "${out:-0}" -gt 0 ] && echo "FAULT" || echo "ALIVE"
}

: > "$LOG"
echo "=== characterization soak $STAMP  cycles=$CYCLES max_conn=${MAX_CONN_S}s ===" | tee -a "$LOG"
faults=0; total_conn=0; total_notif=0; clean_disc=0

for c in $(seq 1 "$CYCLES"); do
	echo "----- cycle $c/$CYCLES $(date -u +%FT%TZ) -----" | tee -a "$LOG"
	pkill -f 'probe[-]rs' 2>/dev/null; pkill -x openocd 2>/dev/null; sleep 1
	ocd_reset
	sleep "$BOOT_S"
	# WiFi load: Pico -> internet (fire-and-forget; runs on the device shell)
	python "$HERE/console.py" send "net ping -c 100000 -i 10 8.8.8.8" --wait 1 >>"$LOG" 2>&1
	# WiFi load: host -> Pico (background, ~5/s without root)
	( ping -i 0.2 "$PICO_IP" >/tmp/soak_hostping.txt 2>&1 ) &
	hp=$!
	# BLE central: connect + subscribe + measure until disconnect or window.
	creport="$(timeout $((MAX_CONN_S + 40)) python "$HERE/ble_central.py" \
		--addr "$BT_ADDR" --duration "$MAX_CONN_S" --scan-timeout 20 2>&1)"
	echo "$creport" >>"$LOG"
	kill "$hp" 2>/dev/null; wait "$hp" 2>/dev/null
	notif="$(echo "$creport" | grep -oE 'notifications: [0-9]+' | grep -oE '[0-9]+' | head -1)"
	cdur="$(echo "$creport"  | grep -oE 'duration: [0-9.]+' | grep -oE '[0-9.]+' | head -1)"
	disc="$(echo "$creport"  | grep -c 'unexpected disconnect: True')"
	pkill -f 'probe[-]rs' 2>/dev/null; pkill -x openocd 2>/dev/null; sleep 1
	state="$(fault_check)"
	notif="${notif:-0}"; cdur="${cdur:-0}"
	total_notif=$((total_notif + notif))
	total_conn="$(awk "BEGIN{print $total_conn + $cdur}")"
	if [ "$state" = "FAULT" ]; then
		faults=$((faults + 1))
	elif [ "$disc" -gt 0 ]; then
		clean_disc=$((clean_disc + 1))
	fi
	echo ">> cycle $c: notif=$notif conn=${cdur}s end=$state disc=$disc" | tee -a "$LOG"
done

echo "=== SUMMARY ===" | tee -a "$LOG"
echo "cycles: $CYCLES   faults: $faults   clean-disconnects: $clean_disc" | tee -a "$LOG"
echo "total connected time: ${total_conn}s   total notifications: $total_notif" | tee -a "$LOG"
if [ "$faults" -gt 0 ]; then
	awk "BEGIN{printf \"MTBF (connected): %.1f s/fault over %.1f s\n\", $total_conn/$faults, $total_conn}" | tee -a "$LOG"
else
	echo "MTBF: no faults in ${total_conn}s connected (>= ${total_conn}s)" | tee -a "$LOG"
fi
echo "artifact: $LOG" | tee -a "$LOG"
