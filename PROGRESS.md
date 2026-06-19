# Coexistence Hardening — Progress / Loop State

Source of truth for `/loop`. Resume must require nothing but this file + repo state.

## Environment (confirmed during M0.0)
- Board: rpi_pico2/rp2350a/m33/w
- Zephyr revision (pinned SHA): 44af4534166bad6cec2f5a6eb226021260b9c49d (v4.4.0-5745-g44af4534166)
- georgerobotics cyw43-driver (vendored at drivers/wifi/zephyr_cyw43/src/cyw43-driver): v1.0.4 (1d2227bca200e13c1d6a630032d5f6d7fca69fef)
- Combined BT firmware blob: wb43439A0_7_95_49_00_combined.h present (non-EULA combined blob; provenance to record in item 6)
- Working flash command (openocd RPi fork, PROVEN): see "Flash recipe" below
- gdb (Zephyr SDK): ~/zephyr-sdk-1.0.1/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb
- Console device (UART bridge on the probe): /dev/serial/by-id/usb-Raspberry_Pi_Debug_Probe__CMSIS-DAP__E665485457925026-if01 @ 115200 (= /dev/ttyACM0)
  - NOTE: UART round-trip not yet proven — hello_world build had `# CONFIG_UART_CONSOLE is not set`, so it never drove UART. Round-trip verification deferred to the driver shell app (M0.1).
- Test AP SSID/PSK: in app/local.conf (not committed)

### Flash recipe (PROVEN on hello_world 2026-06-19)
```
OCD=~/.pico-sdk/openocd/0.12.0+dev/openocd
OCDS=~/.pico-sdk/openocd/0.12.0+dev/scripts
pkill -f 'probe[-]rs'; pkill -x openocd; pkill -f '[g]db'   # PROBE GUARD (see below)
$OCD -s $OCDS -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
  -c "adapter speed 5000" -c "program build/zephyr/zephyr.elf verify reset exit"
```
gdb run-confirm: start `$OCD -s $OCDS -f interface/cmsis-dap.cfg -f target/rp2350.cfg` in bg,
then `arm-zephyr-eabi-gdb -nx -batch -ex "target extended-remote :3333" -ex "monitor reset run"
-ex "monitor halt" -ex "bt" build/zephyr/zephyr.elf`.

### PROBE GUARD — critical loop gotcha
`pkill -f probe-rs` SELF-MATCHES the Bash tool's own command line (it contains the literal
string "probe-rs") and kills the shell (exit 144). Use a bracket-regex so the pattern text
itself does not contain the match: `pkill -f 'probe[-]rs'; pkill -x openocd; pkill -f '[g]db'`.

## Status legend: TODO | IN-PROGRESS | VERIFIED(<artifact path>)

| # | Item | Status | Artifact |
|---|------|--------|----------|
| M0.0 | Reliable single-command flash + a console the loop can drive (shell round-trip); image confirmed running via gdb | VERIFIED | docs/artifacts/m0.0_console_20260619.log (flash Verified OK + gdb idle + `kernel version`->`Zephyr version 4.4.99`) |
| M0.1 | Driver app builds+flashes+boots on Pico 2 W with WIFI+BT in one image; `wifi connect` associates AND `bt init`+advertise succeed (rebase to 4.4 + add rpi_pico2 overlay + reconcile cyw43 GPIO binding) | VERIFIED | docs/artifacts/m0.1_wifi_bt_20260619.log (STA COMPLETED, DHCP 192.168.11.20; bt init ok, id 88:A2:9E:D1:6D:A0; advertising started) |
| 1 | HCI setup / BD_ADDR (stable correct public addr across 10 cold boots) | VERIFIED | docs/artifacts/item1_bdaddr_10boots_20260619.log (10/10 boots = 88:A2:9E:D1:6D:A0, distinct=1, STABLE; setup hook verified =WiFi MAC+1 all 10) |
| 2 | SCO/ISO separation (ISO correct; SCO routed or cleanly gated) | TODO | |
| 3 | RX robustness (read() return checked; NULL-buf drop policy; length bounds) | TODO | |
| 4 | Threading / bus-arbitration audit (lock invariant under load; no prio inversion/stack overflow) | TODO | |
| 5 | Shared WL_REG_ON/BT_REG_ON power (all init orders come up clean) | TODO | |
| 6 | Firmware blob pinned + provenance/license recorded | TODO | |
| SOAK | Coexistence soak passes (STA assoc + BLE connected + bidirectional load, >=2h + 5 cold boots; zero lockups/faults/disconnects; throughput & BLE latency within bounds) | TODO | |
| REF | REFERENCE.md transport+arbitration contract complete (for the future WHD port) | TODO | |

## DONE when
M0.0 + M0.1 + items 1-6 + SOAK + REF all VERIFIED, WiFi-only and BT-only builds still green,
checkpatch clean.

## Bring-up findings (carried in from manual debugging — don't re-discover)
- Probe: Raspberry Pi Debug Probe, CMSIS-DAPv2, VID:PID 2e8a:000c, serial E665485457925026.
- Board target: rpi_pico2/rp2350a/m33/w. openocd sees cores rp2350.cm0 / rp2350.cm1.
- RPi openocd fork present: ~/.pico-sdk/openocd/0.12.0+dev (openocd + scripts/, has rp2350.cfg).
- probe-rs flashes successfully over SWD (`probe-rs run/download --chip RP235x`); SWD path proven.
- "could not claim interface: Resource busy" hit when a prior probe-rs/openocd/gdb still held the
  probe -> must kill stale holders; one tool at a time.
- RTT was silent: `segger` module not in west.yml name-allowlist -> CONFIG_USE_SEGGER_RTT ignored,
  console stayed on UART. UART path needs the probe "U" connector wired (TX->GP1, RX->GP0, GND).
- hello_world banner is one-shot; verify console via shell round-trip on the real driver app.
- Setup already removed dts/bindings/gpio/infineon,cyw43-gpio.yaml (now provided by upstream 4.4);
  cyw43 GPIO driver must be reconciled to upstream's binding schema in M0.1.
- Open question for M0.0: does the Zephyr RP2350 image run after openocd flash (gdb `bt`)?

## Console: RESOLVED (UART wired, working)
Operator wired the probe "U" connector to Pico UART0 (probe TX->GP1, RX->GP0, GND).
Round-trip confirmed: `python test/coex/console.py send "kernel version"` -> "Zephyr
version 4.4.99". Boot banner, CYW43 fw load, BT fw download, and the uart:~$ shell all
visible. The loop drives the shell via test/coex/console.py (send/capture). No reflash
needed between console calls; the app stays up. Reset+capture recipe is in the changelog.

## Console blocker (historical — M0.0/M0.1 gate, now resolved)
The WIFI+BT app boots and idles healthy (gdb backtrace: arch_cpu_idle -> idle ->
z_thread_entry; no fault), but UART produces NO output on boot or on shell input.
The app's console is uart0 (GP0/GP1) with CONFIG_UART_CONSOLE=y, so the silence is
the probe "U" connector not being wired to the Pico UART0. Two ways forward:
  (A) Operator wires the probe "U" connector to Pico UART0: probe TX -> GP1 (Pico RX),
      probe RX -> GP0 (Pico TX), GND <-> GND. Then `python test/coex/console.py send
      "kernel version"` should reply. One-time, simplest.
  (B) RTT over the existing SWD cable (no wiring): add `segger` to west.yml allowlist,
      west update, enable CONFIG_USE_SEGGER_RTT + SHELL_BACKEND_RTT + console on RTT,
      reflash, drive via `openocd ... -c "rtt setup/start"` + TCP socket. More setup.
DECISION (operator, this run): go with (A) UART wiring.
Operator action required — wire the Debug Probe "U" connector to Pico 2 W UART0:
  probe TX  -> Pico GP1  (UART0 RX, physical pin 2)
  probe RX  -> Pico GP0  (UART0 TX, physical pin 1)
  probe GND <-> Pico GND (e.g. physical pin 3)
RESUME after wiring: run `/loop`. First check is the console round-trip:
  source ../.venv/bin/activate
  python test/coex/console.py send "kernel version"   # expect a Zephyr version reply
If it replies -> M0.0 console VERIFIED; proceed to M0.1 functional test
  (`wifi connect`, `bt init`, `bt advertise on`) then backlog item 1.
If still silent after wiring -> recheck TX/RX orientation (swap GP0/GP1) before falling back to RTT path (B).
The app is already flashed (build_pico2/zephyr/zephyr.elf) and boots healthy; no reflash needed to test the console.

## Artifacts (this run)
- Build (WIFI+BT, rpi_pico2): build_pico2/zephyr/zephyr.elf (+ .uf2), build succeeds clean.
- Flash proven: openocd "Verified OK" on both hello_world and the WIFI+BT app.
- gdb run-confirm of WIFI+BT app: idles in arch_cpu_idle (healthy). Logged to PROGRESS.

## Changelog (newest first)
- Item 1 (HCI setup / BD_ADDR) VERIFIED. Implemented CONFIG_BT_HCI_SETUP
  (select BT_HCI_SETUP if BT in the module Kconfig): the setup() hook reads the
  controller BD_ADDR via HCI Read_BD_ADDR and verifies it equals WiFi MAC + 1
  (computed from cyw43_state.mac), erroring on a zero/broadcast address. 10-cold-boot
  harness test/coex/cold_boot_btaddr.sh: all 10 boots report 88:A2:9E:D1:6D:A0
  (stable), hook logs "verified (= WiFi MAC + 1)" each boot.
  Artifact: docs/artifacts/item1_bdaddr_10boots_20260619.log.
- M0.0 + M0.1 VERIFIED on hardware. UART wired by operator; console round-trip works.
  WiFi STA associates to "819 Paramount" (DHCP 192.168.11.20); `bt init` ok (public
  addr 88:A2:9E:D1:6D:A0 = WiFi MAC 88:A2:9E:D1:6D:9F + 1, manuf 0x0131, HCI 5.2);
  `bt advertise on` -> "Advertising started"; WiFi+BT concurrent, uptime healthy.
  Artifacts: docs/artifacts/m0.0_console_20260619.log, m0.1_wifi_bt_20260619.log.
  Note for item 1: bt_hci_core warns "Num of Controller's ACL packets != ACL
  bt_conn_tx contexts (8 != 3)" at bt init — benign but worth revisiting.
- M0.1 build reconcile (Zephyr 4.4 drift): added app/boards/rpi_pico2_rp2350a_m33_w.overlay
  (georgerobotics infineon,cyw43 over raspberrypi,pico-spi-pio; deletes upstream AIROC
  airoc-wifi node; reuses board pinctrl); prj.conf BT_PERIPHERAL/BT_CENTRAL (BT_CONN);
  zephyr_cyw43_drv.c wifi_mgmt_ops callbacks gained `struct net_if *iface` (4.4 API);
  NET_ERR->LOG_ERR; bt_hci_drv.c modernized to device-based bt_hci API (open() w/o recv,
  struct bt_hci_driver_data common, bt_hci_recv(), TX type from buf->data[0], drop
  bt_buf_get_type); cyw43_configport.h includes pico/platform/panic.h for the BT glue.
  Result: WIFI+BT image builds, flashes, boots & idles healthy on rpi_pico2/w.
- M0.0 bring-up: settled openocd flash recipe (Verified OK), gdb run-confirm, recorded
  Zephyr/cyw43 SHAs + BT blob. Found probe-guard self-match gotcha (bracket-regex fix).
  Added test/coex/console.py (pyserial shell driver).