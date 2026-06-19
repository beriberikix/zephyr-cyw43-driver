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
| 2 | SCO/ISO separation (ISO correct; SCO routed or cleanly gated) | VERIFIED | docs/artifacts/item2_iso_smoke_20260619.log (ISO RX guarded by CONFIG_BT_ISO w/ BT_BUF_ISO_IN; SCO dropped not mis-routed; ISO build green; bt init+advertise+WiFi coex healthy. Controller lacks ISO HW -> no ISO/SCO traffic, routing verified by build+code) |
| 3 | RX robustness (read() return checked; NULL-buf drop policy; length bounds) | VERIFIED | docs/artifacts/item3_rx_stress_20260619.log — 120s forced-exhaustion (discardable=1) flood, 542 reports, device responsive throughout (uptime 17->150s), 0 faults. RESULT: PASS. |
| 4 | Threading / bus-arbitration audit (lock invariant under load; no prio inversion/stack overflow) | VERIFIED | docs/artifacts/item4_coex_arbitration_20260619.log — root-caused poll-thread priority inversion; fixed (coop -14 -> -1); realistic coex (advertise+WiFi load+HCI cmds) 4 rounds clean. Full 2h soak = SOAK row. |
| 5 | Shared WL_REG_ON/BT_REG_ON power (all init orders come up clean) | VERIFIED | docs/artifacts/item5_initorder_20260619.log — BT-only, WiFi-then-BT, BT-then-WiFi all clean; BT survives WiFi disconnect cycles (shared power not dropped). |
| 6 | Firmware blob pinned + provenance/license recorded | VERIFIED | REFERENCE.md §1 — wb43439A0_7_95_49_00_combined.h SHA-256 6b4b9a71…, cyw43-driver v1.0.4, RP (non-EULA) license, runtime versions logged. |
| SOAK | Coexistence soak passes (STA assoc + BLE connected + bidirectional load, >=2h + 5 cold boots; zero lockups/faults/disconnects; throughput & BLE latency within bounds) | TODO | |
| REF | REFERENCE.md transport+arbitration contract complete (for the future WHD port) | TODO | |

## Item 4 RESOLVED (poll-thread priority inversion) — analysis
ROOT CAUSE: the cyw43 shared-bus poll thread (sole gSPI reader; feeds the BT
host RX workqueue at coop -8 and WiFi RX) was created K_PRIO_COOP(2) == -14, the
HIGHEST cooperative priority. Under sustained host-wake activity it never blocked
and its only yield (CYW43_EVENT_POLL_HOOK k_yield()) can't drop to a lower coop
thread, so HCI command-completes were read off the bus but never delivered to the
host -> bt_hci_cmd_send_sync 10s timeout -> oops.
FIX (commit 8b82c09): poll thread -> K_PRIO_COOP(CONFIG_NUM_COOP_PRIORITIES-1)
== -1 (lowest coop). MUST stay cooperative — preemptible corrupts cyw43_ll state
(implicit mutual exclusion relies on no-preemption; tested: preemptible -> assert
/abort). At -1, k_yield() releases to BT RX WQ / WiFi RX / bt_tx, delivering
command-completes promptly while preserving the no-preempt guarantee.
VERIFIED: realistic coex (BLE advertise + WiFi ping load + repeated HCI cmds) 4
rounds, all commands succeed, WiFi stays associated, no fault. Artifact:
docs/artifacts/item4_coex_arbitration_20260619.log.
Thread audit (kernel thread list, debug build): poll 632/1024 stack (61%); a
work_q thread at 86% (888/1024) — watch in soak; no overflow observed.

### Console-saturation artifact (NOT a driver bug)
`bt scan on` in a dense RF area makes the BT shell print every advertising
report over the 115200 UART; that backs up the host RX workqueue, so an HCI
command issued mid-flood (`bt scan off`) has its command-complete delayed past
HCI_CMD_TIMEOUT. This is console throughput, not the bus — the soak workload is a
BLE peripheral (no scan-print flood). Clean HCI commands (advertise on/off) work
fine even under concurrent WiFi load (item 4 artifact). rx_stress.sh stops the
flood by SWD reset rather than an HCI command to avoid conflating this.

## Item 4 fault (historical — root-cause notes, now fixed)
Symptom: kernel oops (reason 3) in the shell thread; gdb backtrace:
  arch_system_halt(reason=3) <- z_fatal_error <- z_arm_svc, esf on shell_uart_stack.
  faulting PC = bt_hci_cmd_send_sync+164 @ hci_core.c:482 ->
  BT_ASSERT_MSG(err==0, "Controller unresponsive, command opcode 0x%04x timeout").
i.e. a synchronous HCI command (e.g. LE Set Scan Enable from `bt scan on/off`)
gets NO command-complete/status back within HCI_CMD_TIMEOUT under shared-gSPI-bus
contention.

Two independent reproductions (both on the normal hardened reference image):
  (a) `bt scan on` (BT RX flood) + `net ping ... 192.168.11.1` (WiFi TX/RX)
      concurrently -> oops within seconds.
  (b) heavy `bt scan on` advertising flood alone, then `bt scan off` -> oops on
      the scan-off command (RX flood starves the command/response path).
Confirmed PRE-EXISTING: the item-2 baseline (before the item-3 RX hardening)
faults identically, so this is NOT an RX-hardening regression.

Hypothesis / where to look (item 4):
  - Single shared gSPI bus carries WiFi (cyw43_ll) AND BT (cybt_shared_bus). The
    cooperative cyw43 poll thread + the lock discipline must serialize bus
    access so a BT HCI command write + its command-complete read are not starved
    by a WiFi transfer or by a flood of inbound BT events.
  - Look at: the poll thread priority/stack and CYW43_THREAD_ENTER/EXIT lock in
    cyw43_configport.h; cyw43_bus_pio_spi.c spi_transceive_dt usage; how
    zephyr_cyw43_bt_hci_send (shell/host TX thread) arbitrates vs the poll thread
    doing cyw43_bluetooth_hci_read; whether bt_hci command TX waits behind WiFi
    RX; whether HCI_CMD_TIMEOUT vs poll latency is the issue.
  - This is the heart of the coexistence contract for REFERENCE.md.
Repro tooling: test/coex/rx_stress.sh (flood), and the (a) case:
  bt init; bt scan on; net ping -c 5 192.168.11.1  (on build_pico2 image).

## Item 5 — shared power line (analysis)
The CYW43439 has ONE chip-enable, WL_REG_ON; there is no separate BT_REG_ON on
the Pico W / Pico 2 W (BT power is internal to the module). So WiFi and BT share
power. Driver behavior:
- WL_REG_ON is driven HIGH at WiFi driver init (POST_KERNEL) and the WiFi
  firmware is loaded then (boot log "cyw43 loaded ok"), independent of any
  association. WL_REG_ON is only driven LOW by cyw43_deinit() (not used here).
- BT firmware is loaded lazily on the first BT op: cyw43_ensure_bt_up() ->
  cyw43_ensure_up() (idempotent, brings the chip up if needed) -> cyw43_btbus_init().
- `wifi disconnect` -> cyw43_wifi_leave() only (leaves the AP); it does NOT drop
  WL_REG_ON, so BT is unaffected.
Consequence: every init order works because the chip is already powered before
either stack's firmware loads. Verified on hardware (item5 artifact):
  - BT-only (CONFIG_APP_WIFI_AUTOCONNECT=n, no association): bt init/advertise
    clean, WiFi stays DISCONNECTED.
  - WiFi-then-BT: associate, then bt init -> clean.
  - BT-then-WiFi: bt init/advertise first, then `wifi connect` -> WiFi associates
    + DHCP, BT stays up.
  - BT survives 3x WiFi disconnect (id-show works each time).
Added CONFIG_APP_WIFI_AUTOCONNECT (app/Kconfig, default y) so a BT-only / BT-first
boot is buildable (-DCONFIG_APP_WIFI_AUTOCONNECT=n).

## Build matrix (keep green)
- Combined WiFi+BT (app/prj.conf): GREEN, hardware-verified (M0.1, item 1).
- WiFi-only (`-DCONFIG_BT=n`): GREEN (links; bt_hci_drv.c excluded via
  sources_ifdef(CONFIG_BT), `select BT_HCI_SETUP if BT` doesn't fire).
- BT-only as CONFIG_WIFI=n: NOT SUPPORTED by this driver architecture — the BT
  transport (cybt_shared_bus over gSPI) is part of WIFI_ZEPHYR_CYW43, which
  `depends on WIFI`. "BT-only" here means the runtime BT-without-WiFi-association
  scenario (item 5), not a WIFI=n build. Document this constraint in REFERENCE.md.

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
UART is wired and working (done). Console driven via test/coex/console.py.

## NEXT UP (resume pointer)
M0.0, M0.1, items 1-6 VERIFIED. Remaining: REF (full transport contract in
REFERENCE.md §2) and SOAK (2h + 5 cold boots).
  -> NEXT: REF — fill REFERENCE.md §2 transport/arbitration contract (HCI-over-gSPI
     4-byte header + H4 indicators; read/write/has_work/ensure_up primitives; the
     single recursive-mutex poll-loop arbitration model incl. the poll-thread
     priority fix from item 4; init/power ordering from item 5; BD_ADDR = WiFi
     MAC+1 from item 1). Most facts already in PROGRESS items 1-5.
  -> Then SOAK (the rock-solid gate).
  -> SOAK BLOCKER (unresolved): WiFi load generation — host can't reach the Pico's
     192.168.x net and gateway ICMP times out. Need zperf (Zephyr->external iperf3
     server) or the host on the AP LAN, plus a host bleak central (bleak not yet
     installed). Also need a peripheral GATT notify characteristic in the app
     (currently only `bt advertise`/shell; the soak needs a connectable notify
     peripheral). Resolve these before the soak gate; likely an operator decision
     on test topology (ask).
Board: rebuild/flash the canonical app for resume:
  source ../.venv/bin/activate
  west build -b rpi_pico2/rp2350a/m33/w -d build_pico2 app
Re-run cold-boot + a coex stress (test/coex/rx_stress.sh, and the item-4 realistic
coex) after ANY RX/TX/poll/arbitration change.

## Artifacts (this run)
- Build (WIFI+BT, rpi_pico2): build_pico2/zephyr/zephyr.elf (+ .uf2), build succeeds clean.
- Flash proven: openocd "Verified OK" on both hello_world and the WIFI+BT app.
- gdb run-confirm of WIFI+BT app: idles in arch_cpu_idle (healthy). Logged to PROGRESS.

## Controller (CYW4343A2) capability findings (for REFERENCE.md / scope)
- HCI 5.2 (0x0b), manufacturer 0x0131 (Infineon/Cypress).
- Does NOT support LE Extended Advertising (HCI 0x2036/0x203a -> status 0x01
  "Unknown HCI Command"). Enabling features that pull in BT_EXT_ADV (e.g. ISO
  broadcaster/periodic adv) breaks even legacy `bt advertise on`. Stick to
  legacy advertising for the BLE-coex scope.
- Does NOT support LE ISO (`iso listen` -> -ENOTSUP). No CIS/BIS audio. ISO RX
  path in the driver is therefore preventive-correct only on this controller.
- bt init warns "Num of Controller's ACL packets != ACL bt_conn_tx contexts
  (8 != 3)" — controller advertises 8 ACL buffers; benign (revisit in item 3/4).

## Changelog (newest first)
- Item 6 (firmware blob pinning) done. Created REFERENCE.md §1 with the combined
  wb43439A0_7_95_49_00 blob SHA-256, provenance (cyw43-driver v1.0.4), the RP
  (non-EULA) license, and runtime-reported WiFi/BT firmware versions.
- Item 5 (shared WL_REG_ON power / init order) VERIFIED. Added
  CONFIG_APP_WIFI_AUTOCONNECT (default y) to make a BT-only / BT-first boot
  buildable. Hardware-tested all three init orders (BT-only, WiFi-then-BT,
  BT-then-WiFi) clean + BT survives WiFi disconnect cycles. Documented the
  single-WL_REG_ON shared-power model above. Artifact:
  docs/artifacts/item5_initorder_20260619.log (PSK redacted).
- Item 4 VERIFIED (8b82c09): fixed poll-thread priority inversion (coop -14 ->
  -1 lowest coop) starving the BT command path; realistic coex (advertise+WiFi
  load+HCI cmds) 4 rounds clean. Item 3 re-verified clean with the fix in place:
  rx_stress.sh now stops the flood via SWD reset (avoids the console-saturation
  artifact); 120s forced-exhaustion flood, 542 reports, 0 faults, responsive.
  Documented the console-saturation artifact + threading audit in PROGRESS.
- Item 3 RX hardening committed (1679e8f) + item 4 root fault found. RX path now
  checks read() return, bounds cyw43_len/parsed lengths, and drops on NULL-buf
  instead of NULL-deref. Event-flood stress (discardable=1, 150s, 773 reports)
  fault-free & responsive. Discovered the CRITICAL item-4 bus-arbitration fault:
  bt_hci_cmd_send_sync "Controller unresponsive" timeout under concurrent
  WiFi+BT or under heavy BT RX flood (pre-existing; baseline faults too). Full
  details + repro + hypothesis in "Item 4 fault" above. Added test/coex/
  rx_stress.sh + rx_stress.conf.
- Item 2 (SCO/ISO separation) VERIFIED. RX: ISO case now guarded by
  CONFIG_BT_ISO (BT_BUF_ISO_IN + bt_hci_iso_hdr, iso_hdr scoped locally); SCO
  given its own case that DROPS with a warning instead of being parsed as ISO
  (the old code shared one case, using ISO buf/header for SCO). Main app (no
  ISO) and ISO-unicast smoke build both green; on HW bt init + legacy advertise
  + WiFi stay healthy with ISO compiled in. Controller lacks ISO/SCO hardware
  so no such traffic flows (documented above); routing correctness is by
  build + code. Added test/coex/iso_smoke.conf.
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