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
EXTENDED (W0): `pkill -f '[g]db'` ALSO self-kills any shell whose command text contains the
literal "gdb" — e.g. a gdb binary path like `arm-zephyr-eabi-gdb` or a `GDB=...gdb` var — because
`-f` matches the whole command line, not just the bracket pattern. When the script itself invokes
gdb, do NOT `pkill` gdb: run gdb with `-batch` (it exits on its own) and guard only `pkill -x openocd`.

## Status legend: TODO | IN-PROGRESS | VERIFIED(<artifact path>)

| # | Item | Status | Artifact |
|---|------|--------|----------|
| M0.0 | Reliable single-command flash + a console the loop can drive (shell round-trip); image confirmed running via gdb | VERIFIED | docs/artifacts/m0.0_console_20260619.log (flash Verified OK + gdb idle + `kernel version`->`Zephyr version 4.4.99`) |
| M0.1 | Driver app builds+flashes+boots on Pico 2 W with WIFI+BT in one image; `wifi connect` associates AND `bt init`+advertise succeed (rebase to 4.4 + add rpi_pico2 overlay + reconcile cyw43 GPIO binding) | VERIFIED | docs/artifacts/m0.1_wifi_bt_20260619.log (STA COMPLETED, DHCP 192.168.11.20; bt init ok, id 88:A2:9E:D1:6D:A0; advertising started) |
| 1 | HCI setup / BD_ADDR (stable correct public addr across 10 cold boots) | VERIFIED | docs/artifacts/item1_bdaddr_10boots_20260619.log (10/10 boots = 88:A2:9E:D1:6D:A0, distinct=1, STABLE; setup hook verified =WiFi MAC+1 all 10) |
| 2 | SCO/ISO separation (ISO correct; SCO routed or cleanly gated) | VERIFIED | docs/artifacts/item2_iso_smoke_20260619.log (ISO RX guarded by CONFIG_BT_ISO w/ BT_BUF_ISO_IN; SCO dropped not mis-routed; ISO build green; bt init+advertise+WiFi coex healthy. Controller lacks ISO HW -> no ISO/SCO traffic, routing verified by build+code) |
| 3 | RX robustness (read() return checked; NULL-buf drop policy; length bounds) | VERIFIED (host RX-buffer handling) — but heavy BT flood now hits the §2.7 below-driver cybt overflow | docs/artifacts/item3_rx_stress_20260619.log — original: 120s flood (~4.5 rpt/s), 542 reports, uptime 17->150s, 0 faults, PASS (the host NULL-drop/bounds code is correct + unchanged). RE-CHECK (gdb-liveness, denser RF ~7.5 rpt/s): test/coex/results/rxstress_coop2_drained_20260619.log — faults at ~70s in the POLL-thread cybt_hci_read path = the SAME below-driver gSPI corruption as the soak (REFERENCE §2.7), load-proportional, NOT a host-buffer defect. Not poll-priority-dependent (-2 and -10 both). Improved rx_stress.sh to gdb-liveness + documented UART-drain. |
| 4 | Threading / bus-arbitration audit (lock invariant under load; no prio inversion/stack overflow) | VERIFIED | docs/artifacts/item4_coex_arbitration_20260619.log — root-caused poll-thread priority inversion; fixed (coop -14 -> -1); realistic coex (advertise+WiFi load+HCI cmds) 4 rounds clean. Full 2h soak = SOAK row. |
| 5 | Shared WL_REG_ON/BT_REG_ON power (all init orders come up clean) | VERIFIED | docs/artifacts/item5_initorder_20260619.log — BT-only, WiFi-then-BT, BT-then-WiFi all clean; BT survives WiFi disconnect cycles (shared power not dropped). |
| 6 | Firmware blob pinned + provenance/license recorded | VERIFIED | REFERENCE.md §1 — wb43439A0_7_95_49_00_combined.h SHA-256 6b4b9a71…, cyw43-driver v1.0.4, RP (non-EULA) license, runtime versions logged. |
| SOAK | Coexistence soak passes (STA assoc + BLE connected + bidirectional load; zero lockups/faults/disconnects) | ACCEPTED w/ DOCUMENTED RESIDUAL (operator accept+document; gate's "zero faults / 2h continuous" NOT met) | SHIPPED CODE (coop -2, a17aecd): soak_bounded_8boot_coop2_20260619.log — 8 SWD-reset cold boots × 90s bounded load (dev ping 1/s + host ping 0.5/s + notify 9.6/s): 0 faults, 0 disconnects, 6073 notif/630s. Sustained: soak_bounded_long_20260619.log — a single link streamed clean ~19min (11180 notif, 0 stalls) then FAULTED at 1162s. So the §2.7 below-driver gSPI cybt corruption is a LOW-RATE PROBABILISTIC fault, mitigated by the BT-TX bus lock (8df7566) but NOT eliminated; MTBF ~tens of min sustained, worse under heavy load. Same root cause hits a heavy BT scan flood (item 3 re-check). Root cause + envelope + transport-fix recommendation in REFERENCE.md §2.7. See "SOAK ROOT CAUSE — gSPI corruption". |
| REF | REFERENCE.md transport+arbitration contract complete (for the future WHD port) | VERIFIED | REFERENCE.md §2 — HCI-over-gSPI framing, transport primitives, single-lock poll arbitration + poll-priority rule, init/power ordering, BD_ADDR derivation, controller caps. |

---

## WHD PORT (resume pointer) — branch `whd-port`

The AIROC/WHD transport port. Plan: `~/.claude/plans/humble-nibbling-church.md`.
Contract checklist: `docs/whd-contract.md`. One `/loop` iteration = advance the next
non-VERIFIED milestone to VERIFIED with a committed hardware log. **Never mark VERIFIED
without a real Pico 2 W log.** Stop condition (DONE): **W6 VERIFIED** (clean 2 h soak).

Reframing (take as given): there is no AIROC gSPI BT HCI transport upstream — the WiFi
side moves to WHD (`hal_infineon`, upstream `zephyr/drivers/wifi/infineon/` reference),
the BT side **retains `cybt_shared_bus`** but is re-arbitrated onto the WHD lock and (W6)
its F1-overflow-aware backplane read. A build-time `CONFIG_CYW43_TRANSPORT_{GEORGEROBOTICS,WHD}`
choice keeps the proven stack as default/fallback; the whole matrix stays green throughout.

| # | Milestone | Status | Artifact |
|---|-----------|--------|----------|
| W0 | Transport switch (Kconfig choice + guarded CMake WHD branch) + §1 EULA-blob lift + this section + blob fetch; build matrix green + georgerobotics boot smoke | VERIFIED | docs/artifacts/w0_kconfig_matrix_20260619.log — georgerobotics {wifi+bt, wifi-only} + WHD wifi-only all link green; georgerobotics boots to idle (gdb, not arch_system_halt); 43439A0.bin (249KB)+clm fetched. WHD+BT deferred to W3 (BT host needs the HCI device → `undefined reference __device_dts_ord_82`). |
| W1 | WHD WiFi-only scan (whd_init/attach/wifi_on/scan over PIO-SPI Zephyr device; resolves R1) | VERIFIED | docs/artifacts/w1_whd_scan_20260619.log — upstream AIROC driver scanned real APs (jberi_hil, funrun, 819 Paramount…) over the RP2350 PIO-SPI, "Scan request done". R1 RESOLVED: WHD's whd_bus_spi_transfer works via spi_transceive_dt (SPI_HALF_DUPLEX, spi-data-irq-shared GP24). Matrix green: WHD 13.72% / geo wifi+bt 14.16% / geo wifi-only 12.14%. |
| W2 | WHD associate + DHCP via Zephyr net L2 (mirror airoc_wifi.c) | VERIFIED | docs/artifacts/w2_whd_assoc_dhcp_20260619.log — WHD 3.3.3.26653, STA assoc to funrun (State COMPLETED, WPA2-PSK, RSSI -48), DHCP 192.168.4.28, ping 8.8.8.8 3/3 0% loss. (W1's early-boot join failure was transient; clean reset associates.) |
| W3 | BT-only bring-up over WHD-arbitrated bus; SEAM-1 primitives; BD_ADDR=MAC+1 | VERIFIED | docs/artifacts/w3_whd_bt_bringup_20260619.log + test/coex/results/w3_whd_bdaddr_10boots_20260619.log — whd_bt_glue.c runs cybt over WHD backplane; bt init OK (BD_ADDR 88:A2:9E:D1:6D:A0 = MAC+1, HCI 5.2 Infineon), advertising started, BD_ADDR STABLE across 10 cold boots. Matrix green WHD+BT 15.57%. DEFERRED to W4/W6: BT bus lock is BT-local (not yet shared with WHD WLAN thread) — a concurrent whd_wifi_join failed once during bt init (contention signal); BT RX is a 4ms poll (no host-wake IRQ hook). |
| W4 | Coexistence parity — unchanged soak.sh/rx_stress.sh/ble_central.py; BT survives wifi disconnect | FUNCTIONALLY VERIFIED, soak-gate IN-PROGRESS | docs/artifacts/w4_whd_ble_notifications_pass_20260620.log — WiFi associated + BLE central connected: 9.9 notif/s, 495 notifications, 0 stalls, 0 disconnect, PASS (= georgerobotics 9.6/s envelope), 0 faults; BT advertises at -61 dBm (= geo). Coex WORKS. NOT a clean multi-cycle gate yet: BLE connect is intermittent (connect-during-WiFi-association fragility + host BlueZ wedging + SWD-reset doesn't power-cycle the CYW43) — see docs/artifacts/w4_soak_reproducibility_20260620.log. The earlier "BLE not discoverable / -92 dBm" was a SYMPTOM of the §2.7 assert + stack overflows, all now fixed. |
| W5 | Init/power-order matrix (BT-only / WiFi→BT / BT→WiFi) + WiFi-only WHD build | VERIFIED | docs/artifacts/w5_whd_initorder_20260619.log — ROOT CAUSE of the WiFi+BT fault was a STACK OVERFLOW (app defaults 2048/2560 too small for WHD+cybt+coex). Fixed in whd.conf (HW_STACK_PROTECTION + MAIN/SHELL/SYSWQ=4096). With the fix: BT-only / WiFi→BT / BT→WiFi all bring up clean (BD_ADDR verified, ALIVE), BT survives wifi disconnect, WiFi-only green. The stack overflow was DISTINCT from the BLE link-quality issue. |
| W6 | §2.7 fix: BT ring-index read via WHD F1-overflow-aware path (durable hal_rpi_pico patch); full 2 h soak zero faults | IN-PROGRESS | docs/artifacts/w4_whd_coex_characterization_20260619.log — shared gSPI bus lock LANDED (patches/airoc_whd_hal_spi_shared_bus_lock.patch wraps whd_bus_spi_transfer; whd_transport.c defines whd_bus_lock; whd_bt_glue.c BT path takes it). Two §2.7 mitigations now in place: (1) cybt reads route through WHD's F1-overflow-aware backplane path → no more assert/panic (0 faults observed); (2) shared lock serializes WLAN vs BT → WiFi associates with BT active. PART 2 LANDED (the cybt re-read, the §2.7 transport fix proper): patches/cybt_shared_bus_reread_index.patch makes cybt_get_bt_buf_index RE-READ a transiently out-of-range ring index (up to 8×) and return CYBT_ERR_HCI_READ_FAILED instead of assert()/abort() — fixed a real boot-time kernel panic (bt_poll asserting on a corrupt index); BT-only now boots+advertises clean. STILL BLOCKED on the 2 h gate by a remaining coex fault: under WiFi+BT concurrency a corrupt-stack/PC memory fault occurs (z_main_stack) and the BLE link is marginal — deeper than the ring-index assert. |
| W7 | (optional) flip default to CYW43_TRANSPORT_WHD; matrix green | TODO | (pending) w7_default_flip |

Architecture decision (W1): the WHD WiFi path REUSES the upstream Zephyr AIROC
driver (zephyr/drivers/wifi/infineon, CONFIG_WIFI_AIROC) over the board's stock
infineon,airoc-wifi node — we do NOT write a custom WiFi driver. The board DTS
already ships that node with correct half-duplex/shared-IRQ pinctrl; WIFI_AIROC
auto-enables from DT_HAS_INFINEON_AIROC_WIFI. This module (CYW43_TRANSPORT_WHD)
contributes no WiFi net device (CMake-gated). src/whd is reserved for the SEAM-1
BT shims (W3+).

DT GOTCHA (W1): devicetree overlays are preprocessed BEFORE Kconfig, so CONFIG_*
is NOT visible in .overlay files — a topology cannot be `#if CONFIG_`-gated.
Instead the georgerobotics topology is the auto-applied board overlay and the
WHD build LAYERS test/coex/whd.overlay on top (re-deletes pio0_spi0, restores
the stock airoc node, drops the dangling zephyr,bt_hci chosen).

WHD-port build matrix (all must stay green every milestone):
```
# WHD mode (W1: wifi-only; from W3: +BT)
west build -p always -b rpi_pico2/rp2350a/m33/w -d build_whd app \
  -- -DEXTRA_CONF_FILE="$PWD/app/local.conf;$PWD/test/coex/whd.conf" \
     -DEXTRA_DTC_OVERLAY_FILE="$PWD/test/coex/whd.overlay"
# regression: proven georgerobotics stack (wifi+bt)
west build -p always -b rpi_pico2/rp2350a/m33/w -d build_pico2 app \
  -- -DEXTRA_CONF_FILE="$PWD/app/local.conf"
# WiFi-only link check
west build -p always -b rpi_pico2/rp2350a/m33/w -d build_wifionly app \
  -- -DEXTRA_CONF_FILE="$PWD/app/local.conf" -DCONFIG_BT=n
```

BT-over-WHD seam (W3): there is no AIROC gSPI BT transport upstream, so the BT
side KEEPS pico-sdk cybt_shared_bus and the existing bt_hci driver (both built
in WHD mode too). Their only georgerobotics coupling is a 4-function backplane
seam + a bus lock + cyw43_state, all provided by src/whd/whd_bt_glue.c:
  cyw43_ll_{read,write}_backplane_reg -> whd_bus_{read,write}_backplane_value
  cyw43_ll_{read,write}_backplane_mem -> whd_bus_transfer_backplane_bytes
  cyw43_thread_enter/exit             -> recursive BT bus mutex (BT-local)
  cyw43_state.mac                     -> whd_wifi_get_mac_address (BD_ADDR=MAC+1)
  + a 4 ms BT RX poll thread (no cyw43 poll thread in WHD mode)
WHD handle via airoc_wifi_get_whd_interface()->whd_driver (whd_int.h).

W6 CRUX — RESOLVED (shared bus lock, see W6 row + patches/): both §2.7
mitigations are in place — cybt reads go through WHD's F1-overflow-aware
backplane path (no assert/panic), and a shared recursive gSPI lock serializes
WHD's WLAN path against BT. Result: WiFi associates with BT active, 0 faults.

BLOCKED (2026-06-20, hardware-state — needs an operator power-cycle): the
device's BT advertising has degraded to UNDISCOVERABLE after this session's many
SWD-reset-only cycles, so the under-load fixes below cannot be cleanly measured.
Evidence (all this session):
  - Run #2 (early): the central FOUND + connected to the peripheral every cycle,
    failing with EARLY DISCONNECT during GATT subscribe under light WiFi load
    (results/soak_20260620_125411.log) — the genuine under-load gap.
  - Run #3 + isolation (later, ~12+ cycles in): the central mostly cannot FIND
    the peripheral at all (results/soak_20260620_131107.log).
  - Disambiguated device vs host: the HOST adapter is healthy — an independent
    `bleak` discover() sees 5 other BLE devices — but the Pico is NOT among them.
    The device IS advertising: fresh-boot UART shows "BT up over WHD bus", BD_ADDR
    88:A2:9E:D1:6D:A0 verified, "starting advertising" at 7.4 s, no error, ALIVE.
    So the controller says it advertises but the RF is weak/absent.
  - A/B proved this is NOT the poll-priority change: BT_POLL_PRIO PREEMPT(8) (W3
    baseline) and PREEMPT(2) (BT-first) are BOTH undiscoverable on a fresh boot
    with the host reset. The regression tracks CYCLE COUNT, not the code.
  - Root cause = the known constraint: SWD `reset run` does NOT power-cycle the
    CYW43 (WL_REG_ON stays high), so BT controller state accumulates across cycles
    until advertising RF degrades. Only a TRUE power cycle (USB unplug/replug, or
    an in-firmware WL_REG_ON / whd_wifi_off+on deinit-reinit we do not yet have)
    recovers it. This is the soak-reproducibility blocker PROGRESS predicted.
  -> OPERATOR ACTION NEEDED: physically power-cycle the Pico 2 W (unplug/replug
     USB), then run `/loop` to resume. First post-power-cycle step: re-confirm a
     fresh chip is discoverable, then A/B BT_POLL_PRIO 8 vs 2 under light load to
     verify the BT-first fix on a clean chip.
  -> ENGINEERING follow-up (the durable fix for reproducibility): add a true
     per-cycle CYW43 power-cycle — a `cyw43 deinit/reinit` shell cmd or a WL_REG_ON
     toggle in WHD mode (whd_wifi_off/whd_deinit then re-on) — so the soak resets
     the chip each cycle instead of relying on SWD reset. Needs the chip recovered
     first to develop against.

CHANGES LANDED THIS ITERATION (unverified-as-fixing due to the block, but kept):
  - test/coex/soak.conf: relaxed the BLE link for coex — CONFIG_BT_PERIPHERAL_PREF_
    TIMEOUT 42->400 (4 s supervision timeout, was 420 ms) + CONFIG_BT_CONN_PARAM_
    UPDATE_TIMEOUT 5000->400 (fire the L2CAP param-update early). Rationale: under
    load the failing cycles were "notif<=1 ... end=ALIVE" = supervision-timeout
    drop on a fault-free device. Harmless to georgerobotics (which already passes).
  - drivers/.../whd_bt_glue.c: documented the BT-first priority inversion (WHD WLAN
    thread + AIROC event task run at CY_RTOS_PRIORITY_HIGH = prio 4; the BT poll
    was prio 8, BELOW them, so WiFi activity preempts BT RX). LEFT at the VERIFIED
    PREEMPT(8) baseline pending a clean-chip A/B; PREEMPT(2) is the prime candidate.
  - test/coex/soak.sh: ELF env-overridable so the harness drives the WHD soak image
    (build_whd_soak). NOTE: the harness calls bare `python`; runs must put the venv
    (../.venv/bin) on PATH or every cycle silently no-ops ("python: command not
    found"). TODO: make soak.sh use python3/venv-robust.

(superseded by the BLOCK above) NEXT: BLE STABILITY UNDER CONCURRENT WiFi TRAFFIC — the real
remaining coex gap (narrowed 2026-06-20). Findings:
  - ble_central with retries reliably PASSes at IDLE WiFi (associated, no
    traffic): repeatable 119/149/495 notifications, 9.9-10/s, 0 stalls, 0 faults.
  - But under the soak's WiFi PING LOAD (even light, 1 ping/s) BLE drops:
    soak.sh cycles give notif=1 then disconnect / notif=0
    (results/w4_whd_coex_soak_retry_20260620.log). So coexistence is stable at
    idle but the BLE ACL link can't survive concurrent WiFi bus traffic.
    georgerobotics held 6073 notif under the SAME bounded load, so this is a WHD
    coex-arbitration gap, not the load itself.
  - Likely cause: WHD's WLAN bus thread holds the shared gSPI lock for long
    bursts during ping TX/RX, starving the BT ACL TX/RX past the BLE connection-
    event timing -> supervision-timeout drop. Fix direction: BT-first
    arbitration / bound WHD's bus-hold time / raise BT RX servicing under load
    (the georgerobotics single-poll BT-first design, REFERENCE §2.3) — and/or
    tune the BLE connection supervision timeout / use a faster BT RX.
  - Harness improvements already landed: ble_central.py --connect-retries (rides
    transient establishment flakiness); soak.sh BOOT_S env-overridable.
  - Still-open harness robustness (lower priority than the under-load fix):
    host BlueZ wedging (needs hciconfig/systemctl reset; no passwordless sudo on
    this host), true per-cycle CYW43 cold boot.

(superseded) earlier NEXT — SOAK REPRODUCIBILITY/ROBUSTNESS:
and the W6 2 h gate. Coexistence is FUNCTIONALLY PROVEN (ble_central PASS: 9.9/s,
495 notif, 0 stalls, 0 faults — docs/artifacts/w4_whd_ble_notifications_pass_20260620.log).
What remains is making it reproducible across a multi-cycle soak:
  (a) BLE connect during active WiFi association is fragile — gate the central
      until association completes, or add connect-retry in ble_central/soak.sh.
  (b) Host BlueZ wedges after repeated power-cycles — use a robust per-cycle host
      reset (hciconfig hciX reset / systemctl restart bluetooth), not just
      bluetoothctl power off/on.
  (c) SWD `reset run` does NOT power-cycle the CYW43 (WL_REG_ON stays high), so
      chip state accumulates across soak cycles — add a per-cycle deinit /
      WL_REG_ON toggle for a true cold boot, or a cyw43 deinit shell cmd.
Once a multi-cycle bounded soak passes (>= georgerobotics 0-fault envelope with
notifications flowing) -> W4 VERIFIED; then a long/2 h run -> W6 VERIFIED.

(historical) the prior blocker note — BLE link quality — is RESOLVED:
(W5's stack overflow + W6's §2.7 asserts are now fixed; bigger stacks did NOT
fix the BLE early-disconnect, confirming it is a separate RF/link issue). See
docs/artifacts/w4_ble_discoverability_diag_20260619.log. Advertising works and
IS discoverable, but the link is MARGINAL:
RSSI ~ -92 dBm (very weak; intermittently not discovered), and on connect the
central EARLY-DISCONNECTS during GATT service discovery (the first ACL/ATT
round-trips). So the coex soak gets 0 stable connections / 0 notifications even
though device-side coex is healthy (0 faults).
  (A) BT RX latency — DONE/RULED OUT as the cause. Implemented host-wake-IRQ-
      driven BT RX: whd_bt_glue.c bt_poll_thread now waits on bt_irq_sem
      (k_sem) kicked from WHD's shared GP24 host-wake ISR (durable airoc patch
      calls whd_bt_notify_irq), priority raised to PREEMPT(2), 20 ms fallback.
      Build+flash+test: STILL early-disconnects during service discovery. So RX
      latency was not the (sole) cause. Kept anyway (correct, lower-latency).
  (B) Weak RSSI — CONFIRMED root cause via A/B on the same bench
      (docs/artifacts/w4_ble_txpower_ab_20260620.log): georgerobotics advertises
      at -61 dBm, WHD at -92 dBm — a ~30 dB BT TX-power deficit, NOT
      environmental. At -92 the link drops during GATT service discovery; -61 is
      robust. Fixing TX power should fix the early-disconnect and unblock W4/W6.
      The georgerobotics cyw43_ll+cybt BT bring-up applies a BT TX-power / PA /
      WiFi+BT coex-arbitration config that whd_bt_glue.c omits (same patchram, so
      it's an init step). NEXT: diff the georgerobotics BT bring-up (cyw43_ll.c
      BT path + cybt cyw43_btbus_init) vs whd_bt_glue.c; look for a coex/ECI
      enable, an HCI Tx-power VSC, or a PA/power register write issued
      post-patchram, and replicate it in the WHD bring-up.

NEXT (resume here): chase the weak-BT-signal root cause.
  1. Decouple BLE-peripheral start from WiFi autoconnect in app/src/main.c — the
     app currently gates ble_peripheral start behind the WiFi-connect path, so
     `-DCONFIG_APP_WIFI_AUTOCONNECT=n` ALSO disables advertising (made the
     WiFi-idle discriminator inconclusive). Fix so BT can be tested with WiFi
     fully idle.
  2. Compare the georgerobotics BT bring-up (cyw43_ll.c BT path + cybt) vs
     whd_bt_glue.c for any TX-power / PA / coex / regulator config or HCI VSC
     that georgerobotics issues and the WHD path skips. The combined BT blob is
     identical, so the delta is in init, not firmware.
  3. Consider an HCI Tx-power VSC or BT coex-config write at bt bring-up.
This blocks W4's notification baseline and W6's 2 h gate. Builds: build_whd
(BT_SHELL, manual diag) and build_whd_soak (auto peripheral). Both build green;
device-side coex (WiFi+BT, 0 faults) is unaffected.

## BLE-connection command-timeout — ROOT CAUSE FOUND + fix (4f72d1f)
ROOT CAUSE: I_HMB_FC_CHANGE (the BT "data ready" interrupt flag in
SDIO_INT_STATUS) has EDGE semantics — set once when BT data arrives, cleared by
cyw43_ll_bt_has_work() after a read. But cyw43_bluetooth_hci_process() read only
ONE packet per assertion. When the controller batched several HCI packets under
one assertion (a connection event + the command-complete the host blocks on in
bt_hci_cmd_send_sync), the rest were stranded until the next assertion -> 10s
timeout -> oops. (CYW43_CLEAR_SDIO_INT defaults to 0, so WiFi does NOT clear the
flag — the bug is one-packet-per-assertion, not a WiFi/BT flag race. An earlier
out-of-band drain from the poll thread corrupted cybt_hci_read's static
`available` accounting and panicked; reverted.)
FIX: cyw43_bluetooth_hci_process() loops cyw43_bt_process_one() while
cyw43_bluetooth_has_pending() (real bt2host ring indices via
cybt_get_bt_buf_index), bounded CYW43_BT_DRAIN_MAX. Stays on the normal
cybt_hci_read path.
RESULT: live notify peripheral now streams cleanly with NO WiFi load (387 notif
/40s, 9.7/s, 0 stalls, no fault) — was a hard fault on subscribe before. Under
EXTREME load (Pico ping 100/s + host ping + BLE) MTBF jumped from ~0 to ~37s
(648 notif over 3 cycles) but 2/3 still faulted -> residual under extreme load.
Characterization artifacts: test/coex/results/soak_*.log.
RESIDUAL (separate issue, throughput not detection): under sustained WiFi load
the BT side eventually hits panic("cyw43 buffer overflow") in cybt_hci_read
(gdb: reason=4 panic, pc=abort, on zephyr_cyw43_event_poll_stack, during the BT
drain). i.e. the controller's bt2host ring OVERFLOWS because BT read latency is
too high while WiFi shares the bus/CPU. Measured: no-load = perfect (no fault);
moderate load (Pico ping 10/s) = 597 notif over 62s then overflow; extreme load
(100/s + host ping) = ~37s MTBF. So BLE throughput is good but read latency
under WiFi contention is the remaining gap.
RESIDUAL FIX (DONE, 23eccd6): poll thread coop -1 -> -2 so it preempts the -1
WiFi rx_q and drains BT promptly (still yields to BT RX WQ -8). 4-cycle moderate-
load soak: ZERO faults, 2550 notifications over 269 s, no stalls. Both root
causes now fixed (drain 4f72d1f + priority 23eccd6).
RESIDUAL cybt overflow (the ONE remaining fault, ~33% of 75s cycles at moderate
load): the BT side still hits panic("cyw43 buffer overflow") in cybt_hci_read
(reason 4 / pc=abort on the poll thread) when the controller's bt2host ring
overflows. Mechanism = COOPERATIVE-THREAD STARVATION, not buffer exhaustion:
  - Disproven: bigger BT host buffers (EVT_RX 10->32, CONN_TX 3->8, ACL counts)
    did NOT change the rate (6-cycle soak: still 2/6 faults) -> the poll is not
    blocking on bt_buf_get.
  - The BT RX workqueue runs at coop CONFIG_BT_RX_PRIO (-8), ABOVE the poll
    thread (-2). When the RX WQ processes a burst of the notify event stream
    (incl. a Number-of-Completed-Packets event per ACL packet) it monopolises
    the CPU cooperatively, the poll can't run, and the controller overruns the
    small bt2host ring before the poll next drains it.
  - Disconnects are now GONE (0/6 with the fixed ble_central.py that no longer
    flags the teardown disconnect).
Measured (moderate load, host->Pico ping + Pico->8.8.8.8 ping, 75s cycles):
  6 cycles, faults 2, disconnects 0, 3634 notifications, 387s connected.
  No-load and light-load = perfect (no fault). Artifacts: test/coex/results/.
THE SCHEDULING DILEMMA — RESOLUTION THIS LOOP:
  (a) DONE (commit 5c17ff6): poll raised from coop -2 to coop -10, one band
      ABOVE both BT cooperative threads (BT RX WQ -8, BT HCI TX -9) via
      MIN(CONFIG_BT_RX_PRIO, CONFIG_BT_HCI_TX_PRIO)-1. The sole bus reader now
      wins the CPU the instant a consumer yields/blocks. RESULT: moderate
      6-cycle 1/6 (was 2/6); HEAVY-load (Pico ping -i25 ~40/s + host ping -i0.2
      + BLE 9/s) 3/3 clean, incl. two full 90s/~800-notif connections. The
      feared -14-era timeout did NOT recur (per-wake drain empties work then the
      poll BLOCKS on event_sem, releasing the CPU to the RX WQ for delivery).
      WiFi-only + full builds green. NOTE: cooperative threads cannot PREEMPT
      one another, so priority only helps at the margins (when the RX WQ
      momentarily blocks); it cannot fully eliminate a long RX-WQ batch
      monopolising the CPU -> the rare residual remains.
  (b) RULED OUT: this Zephyr (zephyr/subsys/bluetooth/host/hci_core.c
      rx_queue_put) has NO inline bt_hci_recv path -- it always k_work_submit()s
      to either the SYS workq or the dedicated BT workq (the BT_RECV_WORKQ
      choice). CONFIG_BT_RECV_WORKQ_BT=n just moves RX onto the SYSTEM workq
      (shared with the net/WiFi stack) -- same or worse contention, not inline.
  (c) AVAILABLE (operator decision): the spec PASS gate is bounded
      ("WiFi throughput within threshold of baseline"), not unlimited load. The
      residual is rare and load-sensitive; no-load/light = perfect. Certifying
      against a defined load threshold is legitimate -- scope is operator's call.
REFINED MECHANISM: the overflow is cybt_hci_read detecting fw_b2h_buf_count <
  `available` (its static leftover) -> the controller's FIXED 4 KB bt2host ring
  (BTSDIO_FWBUF_SIZE 0x1000, in controller firmware RAM, not host-tunable)
  lapped because host read latency was too high. Under shared-bus contention the
  gSPI bandwidth + cooperative CPU latency is the hard limit; priority mitigates
  but a single 3-pin bus shared by WiFi+BT has an inherent ceiling.
RATE WITH coop -10 (aggregate): moderate 5/6 clean (1 fault) + heavy 3/3 clean
  -> ~1 fault in ~9 cycles, down from ~1 in 3. A 12-cycle soak is running to
  pin the rate; full 2h x N gate scope = operator call.

## SOAK ROOT CAUSE — gSPI shared-bus corruption (BELOW the driver) — NEEDS DECISION
DECISIVE gdb evidence (artifact txlock_mutex_held_at_fault_20260619.txt +
txlock_rootcause_backtrace_20260619.log + post_txlock_faultcap_20260619.log):
The soak fault is a kernel panic (reason 4) on the bt_tx_processor thread:
  tx_processor -> send_buf -> zephyr_cyw43_bt_hci_send ->
  cyw43_bluetooth_hci_write -> cybt_hci_write_buf -> cybt_get_bt_buf_index ->
  assert(ring index < BTSDIO_FWBUF_SIZE) FAILS (index read back >= 0x1000).
AT THE FAULT the cyw43 bus mutex owner = bt_tx_processor_workq, lock_count = 1:
the BT TX path HELD the bus lock, so host-side exclusive bus access was in
effect and STILL the controller's shared-memory ring index read back corrupt.
=> The corruption is NOT a host-side locking/arbitration defect. It is at or
   below the vendored pico-sdk cybt_shared_bus transport
   (modules/hal/rpi_pico/.../cybt_shared_bus{,_driver}.c): the BT backplane
   read does not handle gSPI F1-overflow / controller-DMA contention the way
   the WiFi path (cyw43_ll) does, so under concurrent WiFi bus traffic the
   read returns garbage and cybt ASSERTS instead of retrying. (Upstream even
   ships a CYBT_CORRUPTION_TEST debug path acknowledging this corruption.)
Fixes applied this loop are CORRECT arbitration but do NOT remove this fault:
  - poll coop -10 (5c17ff6): poll above BT RX WQ/TX so it drains promptly.
  - BT-TX bus lock (8df7566): TX write now serialized under the bus mutex
    (it was the one unlocked bus path). VERIFIED in the binary (objdump shows
    cyw43_thread_enter/exit around the write) and the mutex is held at fault.
Fault rate with both: ~2 faults / 4 real BLE connections at moderate load
(verify soak soak_20260619_195727.log; faults cycle 2 @6.2s and cycle 4 @
teardown). No-load/light still clean.
THIS IS THE 5th ATTEMPT on the soak fault -> ESCALATE per loop protocol.
CANDIDATE FIXES (need operator approval on APPROACH/scope):
  (A) Patch the vendored pico-sdk cybt HAL: in cybt_get_bt_buf_index, on a
      corrupt (>= BTSDIO_FWBUF_SIZE) index, RE-READ instead of assert (the
      corruption is transient), or handle F1-overflow on the BT read path.
      Likely the real fix, but edits modules/hal/rpi_pico (a west-managed
      upstream module) -> needs a durable patch/fork mechanism, not a raw edit.
  (B) Driver-level redesign: move the BT HCI TX off the separate
      bt_tx_processor thread into the poll-thread context (queue TX bufs; the
      poll thread does cyw43_bluetooth_hci_write in-sequence with the RX
      has_work handshake), matching pico-sdk's single-run-loop model. Larger
      change, in our driver scope, uncertain it removes the corruption.
  (C) Accept + document as a known shared-3-pin-bus-under-load limitation and
      certify the soak against a bounded WiFi-load threshold (spec allows
      "within threshold of baseline"); no-load/light is clean.
Also: the HOST BLE adapter (BlueZ hci0) wedges after ~6 connect/disconnect
cycles (cycles 7-12 of soak_20260619_191658.log = notif=0/conn=0). soak.sh now
needs a per-cycle `bluetoothctl power off/on` (added to the faultcap chain) or
a different host to run a long gate.

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

## BLE-connection command-timeout (TOP soak blocker — reopens item-4 fragility)
The soak BLE half (CONFIG_APP_BLE_PERIPHERAL=y, build_soak) revealed the item-4
"Controller unresponsive" fault is NOT fully fixed: a real BLE central connect +
CCC subscribe FAULTS the Pico with NO WiFi load.
REPRO:
  west build -p always -b rpi_pico2/rp2350a/m33/w -d build_soak app -- \
    -DEXTRA_CONF_FILE="$PWD/app/local.conf" -DCONFIG_APP_BLE_PERIPHERAL=y
  flash; then on the host: python test/coex/ble_central.py --addr 88:A2:9E:D1:6D:A0
  -> central: connects, start_notify -> "GATT Protocol Error: Unlikely Error", disconnects.
  -> Pico: DEAD. gdb: oops reason 3 on sys_work_q_stack, pc = bt_hci_cmd_send_sync+164
     (hci_core.c:482 "Controller unresponsive, command opcode timeout").
So the item-4 poll-priority fix (coop -14 -> -1) helped WiFi+BT + advertise+cmd,
but a real connection + GATT subscribe + the 100ms notify thread still starves an
HCI command-complete past HCI_CMD_TIMEOUT.
PROGRESS THIS LOOP (3rd+ attempt on the command-timeout):
- Removed two variables: CONFIG_BT_SHELL=n + CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048
  (test/coex/soak.conf). Subscribe then SUCCEEDED + 1 notify delivered, but still
  faulted -> not a shell conflict or sys_work_q overflow (fault esf +1744 within
  2048 = no overflow).
- Implemented the DRAIN fix (committed 314c6b8): the poll thread now drains all
  pending BT/WiFi work per wake (bounded 32), not one packet. This MARKEDLY
  helped: a live connected notify peripheral now SURVIVES (uptime advanced
  42s/74s) where it used to fault on subscribe. No regression on the canonical
  shell app (boot+WiFi+bt init+advertise + ping coex all clean). WiFi-only build
  kept green (guarded cyw43_ll_bt_has_work behind CONFIG_BT).
- REMAINING fault (still intermittent): the self-starting peripheral's
  bt_enable() at boot (z_main_stack), and longer-lived connections, still hit the
  command-timeout occasionally. KEY DIAGNOSTIC at the fault: the poll thread is
  PENDING/idle (state 0x02) and the cyw43 bus mutex is FREE (owner 0xffffffff,
  depth 0) -> nothing deadlocked; the command-complete was simply never surfaced
  to the host (poll had no work to do). That points BELOW the driver, into the
  cybt_shared_bus BT-mailbox / WL_HOST_WAKE signaling or the controller firmware:
  either the command-complete never arrived, or cyw43_ll_bt_has_work() never
  flagged it. Note `bt init` from the shell (same bt_enable) is reliable when the
  system is idle; the fault correlates with bt_enable/connection happening while
  the bus is busy (boot WiFi assoc, or active connection traffic).

NEXT-LOOP DEBUG PLAN (was; partially done above):
  - Build with test/coex/debug_threads.conf; repro; at the fault capture ALL
    thread states (`kernel thread list`) to see who holds/starves the bus and
    where the poll/BT-RX-WQ/sys_work_q are.
  - Hypotheses to test: (a) the 100ms notify thread (ACL TX) floods the bus and
    starves the command path -> try slower/CCC-gated notify, or send via the BT
    tx path properly; (b) sys_work_q stack (was 86% in the thread audit) too
    small -> bump CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE; (c) BT_SHELL + self-start
    peripheral both manage BT/adv -> try CONFIG_BT_SHELL=n in the soak build;
    (d) the poll loop processes only ONE BT pkt per pass (cyw43_ctrl.c:224) so a
    burst backs up -> drain BT per poll wake (loop in our poll thread using
    cyw43_ll_bt_has_work). This (d) is the most likely real fix and is the same
    root the scan-flood console artifact hinted at.
  - The soak cannot pass until a connected BLE peripheral with notifications is
    stable under WiFi load; fix this first, then re-verify item 4 with a REAL
    connection (not just advertise+cmd).

## SOAK plan + environment findings (last item remaining)
NETWORK RESOLVED (operator): host + Pico both on SSID "funrun", same /22 subnet
(host 192.168.5.215, Pico 192.168.4.28), host<->Pico ping 0% loss -> true
bidirectional WiFi load is now possible (need iperf v2 for zperf, or a TCP/UDP
load gen; no iperf3 on host yet). bleak installed (3.0.2) via `uv pip install`.
local.conf updated to funrun (rebuild to apply). BLE works over direct RF.
Everything except SOAK is VERIFIED. Soak harness needs (none built yet):
1. App: a connectable BLE PERIPHERAL with a notify GATT characteristic (current
   app only has the BT shell `bt advertise`). Add a small GATT service +
   periodic notify; keep behind a Kconfig so the reference app stays lean.
2. Host: a bleak central (test/coex/, to write) that connects, subscribes to the
   notify char, and records notification latency + disconnects. Host HAS a BLE
   adapter: hci0 = C8:95:CE:C7:B2:7F, UP RUNNING. bleak NOT installed
   (`pip install bleak` into ../.venv).
3. WiFi load: host CANNOT reach the Pico STA IP (host 192.168.5.215 is on a
   different subnet than the Pico's 192.168.11.20; ping = 100% loss; no iperf3 on
   host). So "host iperf3 against STA IP, both directions" per the spec is NOT
   possible as written. Pico-side load is the alternative (zperf/UDP/TCP to an
   external/iperf server, or DNS/ping bursts) — exercises the shared bus the same
   way. NOTE: `net ping 192.168.11.1` (gateway) TX works but gets no ICMP reply;
   need a reachable load target.
4. Cold boots: SWD `reset run` re-powers the CYW43 + reloads WiFi/BT firmware
   (= effective cold boot of the wireless subsystem; proven in cold_boot_btaddr).
   True board power-cycle needs hardware the loop lacks.
5. PASS gate: zero CYW43 lockups, zero Zephyr faults/asserts, zero unexpected BLE
   disconnects, WiFi throughput within threshold, BLE notify latency under bound,
   over SOAK_HOURS x COLD_BOOTS. Archive logs under test/coex/results/.
The item-4 fix already makes the realistic coex (advertise + WiFi load + HCI
cmds) survive; the soak is the long-duration proof.

OPEN DECISIONS for the operator (asked at end of this run): WiFi-load topology
(Pico-side vs put host on AP LAN vs provide an iperf server) and soak
scope/duration + whether SWD-reset counts as a cold boot.

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

## NEXT UP (resume pointer) — COMPLETE (driver scope)
Shipped code: coop -2 (a17aecd) + BT-TX bus lock (8df7566). All driver-fixable
items VERIFIED on hardware. Bounded soak re-verified at the shipped code
(soak_bounded_8boot_coop2_20260619.log: 8 cold boots, 0 faults). The ONE
residual (sustained/heavy load: long soak link, dense BT flood) is the §2.7
below-driver cybt gSPI corruption — root-caused (persists with the bus lock
held), operator-accepted + documented, transport-level fix recommended (NOT a
host-side defect; out of this driver's reach without patching the vendored
pico-sdk cybt HAL). Nothing else outstanding in driver scope.
  -> If resumed: the only open work is the operator's transport-level fix
     decision (patch cybt to retry-on-corrupt-index / handle F1-overflow on the
     BT read), or a full 2h gate once that lands. Both are beyond this driver.

## (superseded) earlier resume pointer
M0.0, M0.1, items 1-6, REF VERIFIED. SOAK is BLOCKED (escalated) — root cause
now PRECISELY pinned (see "SOAK ROOT CAUSE — gSPI shared-bus corruption"):
the fault is a corrupt controller ring-index read in the vendored pico-sdk
cybt_shared_bus HAL that persists EVEN WITH the cyw43 bus mutex held by the BT
TX thread (gdb-confirmed). Both arbitration fixes this loop (poll coop -10
5c17ff6, BT-TX bus lock 8df7566) are correct but do NOT remove it.
  -> AWAITING OPERATOR DECISION on approach: (A) patch the vendored cybt HAL to
     retry-on-corrupt-index / handle F1-overflow on the BT read (likely real
     fix, edits modules/hal/rpi_pico -> needs durable patch mechanism); (B)
     driver redesign to do BT TX from the poll-thread context (pico-sdk
     single-run-loop model); (C) accept + document as a known shared-bus
     limitation and certify against a bounded load threshold. Recommendation:
     (A) first (most likely to actually fix it) with (C) as the fallback if the
     corruption is truly unavoidable at the bus level.
  -> Harness fix needed for a long gate: host BlueZ adapter wedges after ~6
     cycles; add per-cycle `bluetoothctl power off/on` to soak.sh (already in
     the faultcap chain) or use a different host.
  -> Orthogonal re-verifies still pending for the two arbitration commits (low
     risk; soak already exercises RX/TX/poll hardest): cold-boot BD_ADDR
     (item 1) + rx_stress (item 3) on the canonical app; WiFi-only + full
     builds already green this loop.
Board currently holds build_soak (BLE peripheral). Soak build:
  west build -b rpi_pico2/rp2350a/m33/w -d build_soak app -- \
    -DEXTRA_CONF_FILE="$PWD/app/local.conf;$PWD/test/coex/soak.conf" \
    -DCONFIG_APP_BLE_PERIPHERAL=y
Drive: test/coex/soak.sh [CYCLES] [MAX_CONN_S]; fault backtrace via /tmp/faultcap.sh.
Tools: build_soak (-DCONFIG_APP_BLE_PERIPHERAL=y + soak.conf), test/coex/soak.sh
(gdb fault-check; net ping blocks the shell so use gdb for liveness),
ble_central.py (teardown disconnect no longer false-flags). Board: canonical app.
Soak build: -DCONFIG_APP_BLE_PERIPHERAL=y (+ test/coex/soak.conf). Drive with
test/coex/soak.sh (gdb fault-check is reliable; NOTE: `net ping` blocks the
shell, so use gdb -- not console uptime -- to check liveness during load).
Board holds the canonical app (no peripheral). PROBE-GUARD reminder: never put
`pkill -f '[g]db'` in the same Bash call as a gdb path (self-match -> exit 144);
guard in a separate call.

## (historical) was-NEXT pointer below:
  -> NEXT: debug + fix the BLE-connection command-timeout (see that section's
     plan; hypothesis (d) drain-BT-per-poll is most promising). Then re-verify a
     stable connected BLE peripheral, THEN run the soak (network is ready:
     funrun, host<->Pico OK, bleak installed; soak build = -DCONFIG_APP_BLE_PERIPHERAL=y;
     WiFi load = host<->Pico or Pico->internet; BLE = test/coex/ble_central.py;
     cold boots = SWD reset). Soak scope (full 2h x5 vs reduced) still an open
     operator decision.
Board holds canonical app (build_pico2, funrun creds, no peripheral).
OLD remaining note (superseded): REF is now done.
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
- Reverted poll coop -10 -> -2 + re-characterized item 3 honestly. Re-running
  rx_stress with a reliable gdb-liveness check (not a shell uptime probe, which
  the flooded UART starves) showed the device FAULTS at ~70s under a dense BT
  advertising flood -- in the POLL thread's cybt_hci_read path, i.e. the SAME
  below-driver gSPI corruption as the soak (REFERENCE §2.7), confirmed
  load-proportional (orig ~4.5 rpt/s clean 150s; now ~7.5 rpt/s faults ~70s) and
  NOT poll-priority-dependent (coop -2 and -10 both fault). The poll -10
  experiment gave no real benefit (didn't fix the below-driver fault) so reverted
  to the verified-good -2 baseline. Kept the BT-TX bus lock (correct arbitration,
  separate path). Item 3's host RX-buffer hardening (NULL-drop/bounds) is correct
  + unchanged; the panic is in the transport beneath it. Improved rx_stress.sh:
  gdb-liveness fault detection + documented UART-drain (an undrained console adds
  its own backpressure overflow). Re-verifying the bounded soak at -2 next.
  Artifacts: test/coex/results/rxstress_coop2_drained_20260619.log,
  /tmp/scanflood_cap.out (poll-thread cybt backtrace).
- SOAK root-caused to gSPI corruption BELOW the driver + ESCALATED (5th
  attempt). gdb esf-unwind of the soak fault: panic(reason 4) on bt_tx_processor
  in cybt_get_bt_buf_index (corrupt controller ring index >= 0x1000) via the BT
  HCI TX path. Added the missing bus lock on that TX path (8df7566,
  cyw43_thread_enter/exit around cyw43_bluetooth_hci_write) -- VERIFIED in the
  binary -- but a re-run faulted IDENTICALLY with the mutex HELD by
  bt_tx_processor (lock_count=1). So host-side exclusive bus access does not
  prevent the corruption: it is at/below the vendored pico-sdk cybt_shared_bus
  transport (gSPI F1-overflow / controller-DMA contention on the BT backplane
  read). Both arbitration fixes this loop (poll coop -10, TX bus lock) are
  correct but neither removes the fault (~2/4 real connections at moderate
  load). Escalated with 3 candidate approaches (patch cybt HAL / move TX to poll
  context / accept+document+bounded-load). Also found the host BlueZ adapter
  wedges after ~6 cycles (soak harness needs a per-cycle adapter reset).
  Artifacts: test/coex/results/txlock_mutex_held_at_fault_20260619.txt,
  txlock_rootcause_backtrace_20260619.log, post_txlock_faultcap_20260619.log,
  soak_20260619_195727.log.
- Soak residual mitigated + characterized (commit 5c17ff6). Raised the gSPI poll
  thread from coop -2 to coop -10 (one band above BT RX WQ -8 and BT HCI TX -9,
  via MIN(BT_RX_PRIO,BT_HCI_TX_PRIO)-1) so the sole bus reader drains the
  controller's 4 KB bt2host ring before it overflows. Moderate 6-cycle soak
  improved 2/6 -> 1/6 faults; heavy-load gdb fault-capture (Pico ping ~40/s +
  host ping 5/s + BLE 9/s) was 3/3 CLEAN incl. two full 90s/~800-notif
  connections (no fault, no unexpected disconnect; backtraces healthy = poll
  mid-SPI-read / idle). Ruled out option (b): this Zephyr's bt_hci_recv has no
  inline path (always k_work_submit to SYS or BT workq), so RECV_WORKQ_BT=n
  would only move RX to the shared system workq. Refined the overflow mechanism
  (cybt_hci_read fw_b2h_buf_count < static `available`; ring is a fixed 4 KB FW
  buffer) and concluded driver-side scheduling levers are exhausted; the rare
  residual is an inherent shared-3-pin-bus latency ceiling -> soak load-threshold
  + gate scope is an operator decision. WiFi-only + full builds green; checkpatch
  clean. Artifacts: test/coex/results/soak_20260619_184447.log (moderate 1/6),
  /tmp/faultcap.out (heavy 3/3 clean), 12-cycle soak in flight.
- Soak residual narrowed. Fixed ble_central.py teardown false-positive (only a
  pre-window drop counts as an unexpected disconnect now) -> real disconnects are
  GONE (0/6). Bumped BT host buffers in soak.conf to test the cybt-overflow
  mechanism -> did NOT help (still 2/6 faults), so it is cooperative-thread
  starvation (BT RX WQ -8 > poll -2), not buffer exhaustion; reverted the bump.
  Documented the scheduling dilemma + 3 next-step options. 6-cycle moderate-load
  soak: 2 faults / 0 disconnects / 3634 notifications / 387 s.
- SOAK BLOCKER RESOLVED (faults). Root-caused the live-BLE command-timeout to
  I_HMB_FC_CHANGE edge semantics + one-packet-per-assertion (fix: drain the full
  bt2host ring per BT poll, 4f72d1f) and a BT-read-latency ring overflow under
  WiFi load (fix: poll thread coop -1 -> -2, 23eccd6). 4-cycle moderate-load soak
  now ZERO faults / 2550 notifications / 269 s. Residual: occasional BLE
  disconnect under sustained load (no crash) + full 2h x5 not yet run.
  Artifact: test/coex/results/soak_20260619_174657.log.
- SOAK characterized (operator chose "characterize"): built test/coex/soak.sh
  orchestrator (SWD-reset cycles + Pico/host WiFi load + bleak central + gdb
  fault-check). Result: a live BLE connect+subscribe faults the controller
  FREQUENTLY (most attempts, within seconds), with or without WiFi load — the
  command-timeout is the gating issue and is NOT rare. Shell-driven coex stays
  solid. Soak cannot pass until the cybt_shared_bus/controller command-timeout
  is fixed. Artifact: test/coex/results/soak_characterization_20260619.md.
- DRAIN fix (314c6b8): poll thread drains all pending BT/WiFi work per wake
  (bounded 32) instead of one packet -> live BLE connection survives where it
  faulted before; canonical app + WiFi-only build green; soak.conf added
  (BT_SHELL=n + bigger sys_work_q for the soak build). Command-timeout markedly
  improved but NOT fully eliminated for a live connection under boot/load — deep
  diagnostics (poll idle + lock free at fault) point into cybt_shared_bus /
  controller signaling. SOAK still blocked on this; see "BLE-connection
  command-timeout". 3rd+ attempt on this item -> escalating per loop protocol.
- Soak BLE half built (BLE notify peripheral + bleak central, 43fda24) and the
  network unblocked (operator moved host+Pico to "funrun", host<->Pico reachable,
  bleak installed). BUT validating it exposed that the item-4 command-timeout is
  NOT fully fixed: a real BLE connect + CCC subscribe faults the controller
  ("Controller unresponsive") with no WiFi load. SOAK blocked on this; full
  debug plan in "BLE-connection command-timeout" above. (item 4's advertise+cmd
  coex still passes; the fix is real but insufficient for a live connection.)
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