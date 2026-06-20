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
# Settle time after reset before driving load + connecting the BLE central.
# Env-overridable: the WHD/AIROC backend associates WiFi slower (~14-21 s) and
# the BLE peripheral should be in steady state before the central connects, so
# the WHD soak uses BOOT_S=25 (georgerobotics default 9 is too early for it).
BOOT_S="${BOOT_S:-9}"
# WiFi load knobs (env-overridable). Pico->internet ping interval (ms) and
# host->Pico ping interval (s). Defaults = moderate. For the BOUNDED-load
# certification (see PROGRESS "SOAK ROOT CAUSE") use a lighter load, e.g.
# PICO_PING_MS=1000 HOST_PING_S=2.
PICO_PING_MS="${PICO_PING_MS:-60}"
HOST_PING_S="${HOST_PING_S:-0.5}"
# Reset the host BlueZ adapter each cycle: it wedges after ~6 connect/disconnect
# cycles (notif=0/conn=0). Set HOST_BT_RESET=0 to disable.
HOST_BT_RESET="${HOST_BT_RESET:-1}"
HERE="$(cd "$(dirname "$0")" && pwd)"
# Image under test. Env-overridable so the same harness drives the WHD soak
# image: ELF=.../build_whd_soak/zephyr/zephyr.elf soak.sh ...
ELF="${ELF:-$(cd "$HERE/../.." && pwd)/build_soak/zephyr/zephyr.elf}"
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
echo "=== characterization soak $STAMP  cycles=$CYCLES max_conn=${MAX_CONN_S}s load: pico_ping=${PICO_PING_MS}ms host_ping=${HOST_PING_S}s ===" | tee -a "$LOG"
faults=0; total_conn=0; total_notif=0; clean_disc=0

for c in $(seq 1 "$CYCLES"); do
	echo "----- cycle $c/$CYCLES $(date -u +%FT%TZ) -----" | tee -a "$LOG"
	# Host BlueZ adapter wedges after ~6 connect/disconnect cycles; reset it.
	if [ "$HOST_BT_RESET" = "1" ]; then
		bluetoothctl power off >/dev/null 2>&1; sleep 1
		bluetoothctl power on  >/dev/null 2>&1; sleep 1
	fi
	pkill -f 'probe[-]rs' 2>/dev/null; pkill -x openocd 2>/dev/null; sleep 1
	ocd_reset
	sleep "$BOOT_S"
	# WiFi load: Pico -> internet (fire-and-forget; runs on the device shell)
	python "$HERE/console.py" send "net ping -c 1000000 -i $PICO_PING_MS 8.8.8.8" --wait 1 >>"$LOG" 2>&1
	# WiFi load: host -> Pico (background)
	( ping -i "$HOST_PING_S" "$PICO_IP" >/tmp/soak_hostping.txt 2>&1 ) &
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
