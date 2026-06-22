<!--
HOW TO USE THIS FILE
The Zephyr "RFC / Proposal" issue template (github.com/zephyrproject-rtos/zephyr,
New issue -> RFC / Proposal) has discrete fields. Paste each section below into the
matching field. `Type` is already set to "RFC" by the template, so the TITLE has no
"RFC:" prefix. Set sidebar "Priority" to your judgement (Low/Medium is fine for an
RFC). `*` = required field.

  Title                         -> "FIELD: Title"
  Problem Description *         -> "FIELD: Problem Description"
  Proposed Change (Summary) *   -> "FIELD: Proposed Change (Summary)"
  Proposed Change (Detailed) *  -> "FIELD: Proposed Change (Detailed)"
  Dependencies                  -> "FIELD: Dependencies"
  Concerns and Unresolved Q.    -> "FIELD: Concerns and Unresolved Questions"
  Alternatives Considered       -> "FIELD: Alternatives Considered"

Replace any <RFC#> / repo-path placeholders. Do not paste this comment block.
-->

## FIELD: Title

WiFi+BT coexistence over the WHD gSPI bus for CYW43439 (Pico 2 W)

## FIELD: Problem Description

The Infineon CYW43439 — used on the Raspberry Pi **Pico 2 W** (Murata 1YN), a very
common low-cost board — carries **WiFi and Bluetooth over a single shared 3-pin gSPI
bus**. Today on mainline Zephyr you cannot run reliable simultaneous WiFi+BLE on this
part:

- Zephyr ships the AIROC/WHD **WiFi** driver (`drivers/wifi/infineon/`) but **no
  BT-HCI transport** for the CYW43439, and **no WiFi+BT coexistence** on the shared
  bus.
- Under concurrent WiFi+BT load, BT backplane access on the shared gSPI bus corrupts
  **below the host bus lock**. The community/pico-sdk BT path (`cybt_shared_bus` on
  the georgerobotics `cyw43_ll` stack) `assert()`s on the corrupt controller
  ring-index read → kernel panic. We proved with gdb that at the fault the host bus
  mutex is *held* by the BT TX thread (`lock_count = 1`) yet the ring index still
  reads back out of range (≥ `0x1000`) — i.e. the corruption is at/under the
  transport, not a host-locking bug, and that transport's BT read path has no
  F1-overflow recovery (its WiFi read path does).

The practical effect: a "zero faults over 2 h continuous coexistence" bar is
**unreachable** on the existing path, so Pico 2 W users can't depend on concurrent
WiFi+BLE in mainline Zephyr.

## FIELD: Proposed Change (Summary)

Add WiFi+BT **coexistence** on the **in-tree AIROC/WHD transport**:

- Reuse the existing WHD WiFi driver as-is (no new WiFi driver).
- Add a BT-HCI path that **retains pico-sdk `cybt_shared_bus`** but re-homes it onto a
  shared recursive gSPI **bus lock** + WHD's **backplane accessors** (there is no
  WHD-native gSPI BT-HCI transport).
- Four targeted fixes (below) close the coexistence gaps.

Result on `rpi_pico2/rp2350a/m33/w`: a **2-hour continuous WiFi+BLE coexistence soak
with 0 faults** (71,021 BLE notifications @ 9.9/s) — the gate the alternative
transport provably cannot meet.

Two of the four fixes are **transport-agnostic and upstreamable immediately** (an
`airoc_wifi.c` `net_buf` leak fix; a `cybt` re-read hardening); the coexistence
arbitration pieces are what this RFC seeks direction on.

## FIELD: Proposed Change (Detailed)

**Architecture (reuses existing components):**
- WiFi: the in-tree `drivers/wifi/infineon/` AIROC/WHD driver, unchanged, over the
  board's PIO-SPI.
- BT: the pico-sdk `cybt_shared_bus` HCI transport (already vendored via
  `hal_rpi_pico`), **re-arbitrated** onto a shared recursive gSPI bus lock and WHD's
  `whd_bus_*_backplane*` accessors so WiFi and BT serialize on the bus. BD_ADDR =
  WiFi MAC + 1.

**The four fixes:**

| Fix | Target file | Upstream home | Coex-only? |
|---|---|---|---|
| Shared gSPI bus lock around `whd_bus_spi_transfer()` | `drivers/wifi/infineon/airoc_whd_hal_spi.c` | Zephyr main | yes (this RFC) |
| BT-first buffer reserve in the shared `airoc_pool` (+ a latent `net_buf` leak fix) | `drivers/wifi/infineon/airoc_wifi.c` | Zephyr main | reserve = coex; **leak fix is standalone** |
| `cybt_get_bt_buf_index()` re-reads a transient corrupt index instead of `assert()` | `cybt_shared_bus_driver.c` | pico-sdk / `hal_rpi_pico` | no (benefits all cybt users) |
| Enable BT coex in the Murata-1YN NVRAM (`btc_mode=1`, `muxenab=0x100`) | `cyfmac43439-1YN.txt` | Infineon / `hal_infineon` | no (board RF config) |

**Validation of the WHD path (hardware, `rpi_pico2/rp2350a/m33/w`):** milestones
W0–W6 each ended in a committed Pico 2 W log — WiFi scan, associate + DHCP + ping,
BT bring-up (BD_ADDR stable 10/10 cold boots), coexistence under load, init/power
order, and the gate:
- **W6 — 2 h continuous coexistence soak: 71,021 notifications / 7200.1 s @ 9.9/s,
  max gap 0.315 s, 0 stalls, 0 unexpected disconnects, 0 faults.**
- Each fix was independently root-caused with hardware evidence, e.g.: a same-bench
  TX-power A/B traced weak BLE to the NVRAM shipping BT coex disabled (`btc_mode=0`)
  → −92 dBm; enabling it → **−67 dBm** (= the georgerobotics config −70). The
  sustained-load BLE drop was pinned by gdb in-flight buffer counters at pool
  exhaustion — `alloc{TX 1217, RX 27471}` vs `release{TX 1197, RX 27471}` → RX
  balanced, **20 TX buffers stuck** in the WHD SDPCM TX queue draining the shared
  `airoc_pool` and starving the BT backplane; load-scaling confirmed it (no/half WiFi
  load held 300 s clean, moderate load dropped ~101 s). The buffer-reserve fix closes
  it.

**Reproduction:** a self-contained coexistence harness lives in `test/coex/`
(`soak.sh`, `ble_central.py`, reset-until-discoverable); raw logs for both transports
are archived (28 files under `docs/artifacts/`, 47 under `test/coex/results/`).

## FIELD: Dependencies

- `drivers/wifi/infineon/` (AIROC/WHD WiFi) — gains coexistence arbitration (the bus
  lock + the `airoc_pool` buffer reserve).
- **Bluetooth HCI subsystem** — needs a home for a gSPI BT-HCI transport for this part.
- `hal_rpi_pico` (pico-sdk `cybt_shared_bus`) — the BT transport + the re-read fix.
- `hal_infineon` — the Murata-1YN NVRAM (`btc_mode`) and WHD WiFi firmware.
- **BT firmware blob** — a combined CYW43439 BT firmware image; needs the Zephyr west
  blob + EULA/governing-board path.
- Reviewers/areas (MAINTAINERS): Infineon AIROC WiFi, Bluetooth HCI, the two HAL
  modules.

## FIELD: Concerns and Unresolved Questions

Questions for maintainers (the reason this is an RFC before a driver PR):
1. **Where should a gSPI BT-HCI transport for the CYW43439 live**, and is reusing
   pico-sdk `cybt_shared_bus` (via `hal_rpi_pico`) acceptable, or do you want a
   WHD-native BT transport written?
2. Is **WiFi-driver-resident coexistence arbitration** (the bus lock + buffer reserve
   in `drivers/wifi/infineon/`) the right home, or should it sit in a coex layer / the
   BT HCI driver?
3. What is the expected path for the **BT firmware blob** (west blob + EULA /
   governing board)?
4. Would you prefer this start as an **out-of-tree module** (we have one staged), with
   only the transport-agnostic fixes upstreamed for now?

Honest caveats:
- **BT bring-up after an SWD *warm* reset is intermittent** (discoverable on every
  other reset). An operator USB power-cycle proved a true **cold boot is clean** — it
  is a debug/warm-reset artifact, not a product cold-boot defect (the harness uses
  reset-until-discoverable).
- The WHD coexistence margin under sustained load was thinner than the alternative
  until the buffer-reserve fix; that fix is what reaches the 2 h gate.
- External dependencies: pico-sdk `cybt` is BSD-3-Clause (documentable); the BT
  firmware blob is the licensing/governing-board item.

## FIELD: Alternatives Considered

**1. The georgerobotics `cyw43_ll` + `cybt` transport (the pico-sdk/community path).**
We started here and hardened it exhaustively on hardware before concluding it cannot
meet the bar:
- Bring-up + hardening all verified: single WiFi+BT image (associate + DHCP; `bt
  init`/advertise), **BD_ADDR stable 10/10 cold boots**, HCI RX robustness (120 s BT
  flood, 542 reports, 0 faults), a root-caused-and-fixed poll-thread priority
  inversion, a clean shared-power init-order matrix, firmware-blob provenance pinned.
- **But the §2.7 corruption is fundamental to it:** gdb-proven below the host lock
  (mutex held by `bt_tx_processor`, index reads ≥ `0x1000`). Soak envelope: bounded
  8-boot cold-boot soak = **0 faults / 6,070 notif / 630 s**, but a single sustained
  link streamed **11,180 notif over ~19 min then FAULTED**, and moderate load gave
  **≈ 2 faults per 4 connections** (MTBF ≈ 101 s). The 2 h zero-fault gate is
  **unreachable** on this transport → we moved to WHD. (We propose still upstreaming
  the transport-agnostic `cybt` re-read hardening, which makes this path degrade
  gracefully instead of panicking.)

**2. A WHD-native BT transport (no `cybt`).** None exists upstream; a larger effort.
Open question #1 above — we retained `cybt` to get a working, tested result; a native
transport could replace it later.

**3. Out-of-tree module only (no mainline).** A valid distribution vehicle (we have
one staged) but it leaves mainline Pico 2 W users without coexistence. We propose
upstreaming at least the two transport-agnostic fixes and using this RFC to find the
right mainline home for the coexistence pieces.
