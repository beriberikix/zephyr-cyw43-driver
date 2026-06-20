<!--
This file is the body for a GitHub RFC issue to open at
https://github.com/zephyrproject-rtos/zephyr/issues/new (label: "RFC").
Paste everything BELOW the title line. Title to use:

  RFC: WiFi+BT coexistence over the WHD gSPI bus for CYW43439 (Pico 2 W)

Do NOT paste this comment block. Replace <RFC#> cross-references after the issue
is created. See ../upstreaming/STEPS.md for the exact submission procedure.
-->

# RFC: WiFi+BT coexistence over the WHD gSPI bus for CYW43439 (Pico 2 W)

## Summary

The Infineon CYW43439 (e.g. Raspberry Pi Pico 2 W, Murata 1YN) carries **both WiFi
and Bluetooth over a single shared 3-pin gSPI bus**. Zephyr ships the AIROC/WHD WiFi
driver (`drivers/wifi/infineon/`) but **no BT-HCI transport for this part**, and no
WiFi+BT *coexistence* support on the shared bus.

This RFC proposes adding WiFi+BT coexistence on the WHD gSPI transport, and asks
maintainers for direction on the right upstream shape before we submit a driver. We
have a **hardware-verified** implementation and want to contribute it correctly.

Headline result: the WHD transport sustains a **2-hour continuous WiFi+BLE
coexistence soak with zero faults** (71,021 notifications / 7200.1 s @ 9.9/s) — a
gate the alternative georgerobotics transport provably cannot meet (it faults at
~19 min on a below-the-host-lock gSPI corruption, documented below).

## Motivation / problem

Under concurrent WiFi+BT load the BT backplane access on the shared gSPI bus is
fragile. We exhaustively characterized this on two independent transports:

- The **georgerobotics** `cyw43_ll` + pico-sdk `cybt_shared_bus` stack: the BT
  ring-index read corrupts **below** the host bus lock and `cybt_get_bt_buf_index()`
  `assert()`s → kernel panic. We proved (gdb) the cyw43 bus mutex is *held* by the
  BT TX thread at the fault, yet the controller's shared-memory ring index reads back
  out of range — i.e. the corruption is at/under the transport, not a host locking
  bug. This transport's WiFi read path has F1-overflow recovery; the BT read path has
  none. The "zero faults over 2 h" gate is **unreachable** on it.

- The **WHD/AIROC** transport (already in-tree for WiFi) + a re-arbitrated `cybt` BT
  path: with four targeted fixes (below) it **meets** the gate.

## Two transports, both exhaustively tested

All testing on `rpi_pico2/rp2350a/m33/w` (RP2350, Cortex-M33) over a Raspberry Pi
Debug Probe, with a self-contained coexistence harness (a connectable BLE notify
peripheral + a host `bleak` central + concurrent WiFi ping load + gdb liveness
classification). **Dozens of soak runs across two days**; full raw logs archived
(artifact inventory at the end).

### georgerobotics transport — hardened to its limit, accepted-with-residual
- Functional bring-up + hardening all verified on hardware: single WiFi+BT image
  (STA associate + DHCP; `bt init`/advertise), **BD_ADDR stable 10/10 cold boots**,
  HCI RX robustness (a 120 s BT flood, 542 reports @ ~4.5/s, uptime 17→150 s, 0
  faults), thread/bus-arbitration audit (root-caused and fixed a poll-thread
  priority inversion), shared-power init-order matrix (BT-only / WiFi→BT / BT→WiFi
  all clean; BT survives WiFi disconnect), firmware-blob provenance pinned.
- **§2.7 corruption, gdb-proven below the host lock:** at the panic the cyw43 bus
  mutex owner is `bt_tx_processor` with `lock_count = 1`, and the ring index reads
  back ≥ `0x1000` (`BTSDIO_FWBUF_SIZE`). Reproduces from both the TX path (soak) and
  the RX path (denser BT flood faults at ~70 s; load-proportional).
- **Soak envelope:** bounded 8-boot cold-boot soak = **0 faults, 0 disconnects,
  6,070 notifications over 630 s**; a single sustained link streamed **11,180
  notifications over ~19 min (1161.7 s) then FAULTED**; moderate load ≈ **2 faults
  per 4 connections**, and **3 faults / 12 cycles (MTBF ≈ 101 s)**.
- Disposition: coexistence accepted **with a documented residual** — the 2 h
  zero-fault gate is not achievable on this transport.

### WHD transport — meets the gate
Milestones W0–W6, each ending in a committed Pico 2 W log:
- W0 transport bring-up; W1 WiFi scan over PIO-SPI; W2 associate + DHCP + ping
  (RSSI −48, 3/3 ping); W3 BT bring-up over the WHD-arbitrated bus (BD_ADDR = MAC+1,
  stable 10/10 boots); W4 coexistence under load; W5 init/power-order matrix (also
  caught + fixed a stack overflow); **W6 the 2 h gate**.
- **W6 — 2-hour continuous coexistence soak: 71,021 notifications / 7200.1 s @
  9.9/s, max inter-notify gap 0.315 s, 0 stalls, 0 unexpected disconnects, 0
  faults.** Single BLE connection under moderate WiFi load.
- Every fix below was **independently root-caused with hardware evidence**, e.g.:
  - BT TX-power: a same-bench A/B showed WHD BT at **−92 dBm** vs georgerobotics
    **−61/−70 dBm**; traced to the Murata-1YN NVRAM shipping BT coex disabled
    (`btc_mode=0`); enabling it restored **−67 dBm**.
  - The sustained-load BLE drop: gdb in-flight buffer counters at pool exhaustion
    read `alloc{TX 1217, RX 27471}` vs `release{TX 1197, RX 27471}` — RX perfectly
    balanced, **20 TX buffers stuck** in the WHD SDPCM TX queue (chip TX
    flow-control starvation), draining the shared `airoc_pool` and starving the BT
    backplane. Load-scaling confirmed it: no/half WiFi load held 300 s clean,
    moderate load dropped at ~101 s; pool 20 → ~101 s, pool 48 → ~185 s.

## The four fixes (and where each belongs upstream)

| Fix | Target | Upstream home | Coex-only? |
|---|---|---|---|
| Shared gSPI bus lock around `whd_bus_spi_transfer()` | `drivers/wifi/infineon/airoc_whd_hal_spi.c` | Zephyr main | yes (needs this RFC) |
| BT-first buffer reserve in `airoc_pool` (+ a latent `net_buf` leak fix) | `drivers/wifi/infineon/airoc_wifi.c` | Zephyr main | reserve = coex; **leak fix = standalone** |
| `cybt_get_bt_buf_index()` re-read instead of `assert()` on a transient corrupt index | `cybt_shared_bus_driver.c` | pico-sdk / `hal_rpi_pico` | no (benefits all cybt users) |
| Enable BT coex in the Murata-1YN NVRAM (`btc_mode=1`, `muxenab=0x100`) | `cyfmac43439-1YN.txt` | Infineon / `hal_infineon` | no (board RF config) |

The BT-over-WHD glue itself retains pico-sdk `cybt_shared_bus` (there is no
WHD-native gSPI BT-HCI transport), re-homed onto the WHD bus lock + backplane.

## Honest caveats (so reviewers have the full picture)
- **BT bring-up after an SWD warm reset is intermittent** (discoverable on every
  other reset). We proved via an operator USB power-cycle that a true **cold boot is
  clean** — it is a debug/warm-reset artifact, not a product cold-boot defect; the
  test harness uses reset-until-discoverable.
- **The WHD coex margin under sustained load is thinner than georgerobotics'** until
  the buffer-reserve fix; that fix is what closes the gap to the 2 h gate.
- **External dependencies / licensing:** the BT path uses pico-sdk `cybt`
  (BSD-3-Clause) and a vendored combined BT firmware blob. The firmware blob is the
  item that needs the Zephyr blob/licensing process.

## Questions for maintainers
1. Where should a **gSPI BT-HCI transport** for the CYW43439 live in Zephyr? Is
   reusing pico-sdk `cybt_shared_bus` (via `hal_rpi_pico`) acceptable, or do you want
   a WHD-native BT transport written?
2. Is a **WiFi-driver-resident coexistence arbitration** (the bus lock + the
   buffer reserve in `drivers/wifi/infineon/`) the right home, or should it sit in a
   coex layer / the BT HCI driver?
3. What is the expected path for the **BT firmware blob** (west blob + EULA/governing
   board)?
4. Would you prefer this as an **out-of-tree module** first (we have one staged) with
   only the transport-agnostic fixes upstreamed?

## What we can upstream immediately regardless of direction
- The **`airoc_wifi.c` latent `net_buf` leak fix** (a too-small buffer returned
  without `net_buf_unref`) — Apache, zero BT dependency. PR ready (see Track A).
- The **`cybt` re-read** fix — a real assert-on-transient-corruption hardening that
  benefits all cybt users — to pico-sdk / `hal_rpi_pico`.

## Reproduction & evidence
Harness: `test/coex/soak.sh`, `test/coex/ble_central.py`, reset-until-discoverable.
Raw logs (georgerobotics + WHD): `docs/artifacts/` (28 files) and
`test/coex/results/` (47 files). Key artifacts: `w6_soak_2h_clean_20260620.log`
(the 2 h gate), `w6_txqueue_stall_localized_20260620.log` (the gdb buffer counters),
`w4_btx_geo_vs_whd_discriminator_20260620.log` (the TX-power A/B),
`soak_bounded_long_20260619.log` (the georgerobotics ~19 min → fault), and the
§2.7 gdb proof (`txlock_mutex_held_at_fault_20260619.txt`).
