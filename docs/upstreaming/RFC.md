<!--
HOW TO USE THIS FILE
The Zephyr "RFC / Proposal" issue template (github.com/zephyrproject-rtos/zephyr ->
New issue -> RFC / Proposal) has discrete fields. Paste each section below into the
matching field. `Type` is already set to "RFC" by the template, so the TITLE has no
"RFC:" prefix. `*` = required field.

  Title                         -> "FIELD: Title"
  Problem Description *         -> "FIELD: Problem Description"
  Proposed Change (Summary) *   -> "FIELD: Proposed Change (Summary)"
  Proposed Change (Detailed) *  -> "FIELD: Proposed Change (Detailed)"
  Dependencies                  -> "FIELD: Dependencies"
  Concerns and Unresolved Q.    -> "FIELD: Concerns and Unresolved Questions"
  Alternatives Considered       -> "FIELD: Alternatives Considered"

NOTE: prose below is intentionally NOT wrapped at 80 cols — GitHub renders a single
newline as a line break, so each paragraph/bullet is one long line on purpose. Don't
re-wrap when pasting. Replace any <RFC#>/path placeholders; don't paste this comment.
-->

## FIELD: Title

Bluetooth (HCI over the shared gSPI bus) for the CYW43439 — Raspberry Pi Pico 2 W

## FIELD: Problem Description

There is no usable Bluetooth on the Raspberry Pi Pico 2 W in mainline Zephyr. The board's Infineon CYW43439 has WiFi support (the in-tree AIROC/WHD driver, `drivers/wifi/infineon/`), but its Bluetooth controller is unreachable.

Zephyr *does* ship CYW43439 Bluetooth support — but only over a **UART** HCI transport: `drivers/bluetooth/hci/hci_uart_infineon.c` (`BT_HCI_UART_INFINEON`, `default y if BT_AIROC && BT_H4`), whose `AIROC_PART` choice even lists `CYW43439` / the Murata 1YN module. That path assumes the controller's HCI is exposed on a UART.

On the Pico 2 W (RP2350 + CYW43439) and the original Pico W (RP2040 + CYW43439), the CYW43439's Bluetooth is **not on a UART** — HCI/ACL is carried over the **same 3-pin gSPI bus as WiFi** (the "shared bus" that pico-sdk's `cybt_shared_bus` drives). Zephyr has **no in-tree HCI-over-gSPI / shared-bus transport** for the CYW43439, so the existing UART driver cannot drive BT on these boards. Net result: Pico 2 W / Pico W have WiFi but **no Bluetooth** on mainline Zephyr.

Second-order: because BT and WiFi share that single gSPI bus, a shared-bus BT transport must also be **coexistence-safe** — concurrent WiFi+BT load corrupts BT bus access *below* the host bus lock if it is not arbitrated (we gdb-proved this; see Alternatives).

Impact: the Pico W / Pico 2 W are among the most popular low-cost dev boards, so "WiFi but no BLE on Zephyr" is a visible, frequently-hit gap.

## FIELD: Proposed Change (Summary)

Add a **Bluetooth HCI transport for the CYW43439 over the shared gSPI bus**, bringing BLE to the Pico 2 W (and Pico W) on Zephyr, by presenting the existing pico-sdk `cybt_shared_bus` gSPI BT transport as a Zephyr `bt_hci` device sharing the bus with the in-tree AIROC/WHD WiFi driver. Then make WiFi+BT **coexist** safely on that single bus.

Outcome on `rpi_pico2/rp2350a/m33/w`: BLE works on the Pico 2 W, and WiFi+BLE coexist through a **2-hour continuous soak with 0 faults** (71,021 BLE notifications @ 9.9/s). Two supporting fixes are transport-agnostic and upstreamable immediately (an `airoc_wifi.c` `net_buf` leak fix; a `cybt` re-read hardening).

## FIELD: Proposed Change (Detailed)

**First-order — Bluetooth at all on these boards.** Present pico-sdk `cybt_shared_bus` (the only existing CYW43439 gSPI BT transport) as a Zephyr `bt_hci` device, re-homed onto the AIROC/WHD bus accessors; BD_ADDR = WiFi MAC + 1. (Reuses existing components: pico-sdk `cybt`; the in-tree AIROC WiFi driver.) Two fixes are needed just to get BT up reliably on this part: (a) `cybt_get_bt_buf_index()` must **re-read** a transient corrupt controller ring index instead of `assert()`-ing (today it panics at bring-up/under load); (b) the Murata-1YN NVRAM ships BT coex disabled (`btc_mode=0`), which parks BT TX ~30 dB down on the single shared antenna — BLE is ~undiscoverable until `btc_mode=1` + `muxenab=0x100`.

**Second-order — WiFi+BT coexistence on the shared bus.** A shared recursive gSPI **bus lock** so the WHD WiFi thread and `cybt` serialize per transfer; and a **BT-first buffer reserve** in the WiFi driver's shared `net_buf` pool so a stalled WiFi TX queue can't drain the pool and starve the BT backplane under sustained load.

The four supporting fixes:

| Fix | Target file | Upstream home | Needed for |
|---|---|---|---|
| `cybt_get_bt_buf_index()` re-reads a transient corrupt index instead of `assert()` | `cybt_shared_bus_driver.c` | pico-sdk / `hal_rpi_pico` | BT at all (no panic) |
| Enable BT coex in the Murata-1YN NVRAM (`btc_mode=1`, `muxenab=0x100`) | `cyfmac43439-1YN.txt` | Infineon / `hal_infineon` | BT TX power |
| Shared gSPI bus lock around `whd_bus_spi_transfer()` | `drivers/wifi/infineon/airoc_whd_hal_spi.c` | Zephyr main | coexistence |
| BT-first buffer reserve in `airoc_pool` (+ a latent `net_buf` leak fix) | `drivers/wifi/infineon/airoc_wifi.c` | Zephyr main | coexistence (leak fix is standalone) |

**Validation (hardware, `rpi_pico2/rp2350a/m33/w`).** BT bring-up: advertise + connect, BD_ADDR stable 10/10 cold boots. Coexistence: a **2 h continuous WiFi+BLE soak — 71,021 notifications / 7200.1 s @ 9.9/s, 0 stalls, 0 unexpected disconnects, 0 faults.** Each fix was root-caused with hardware evidence — e.g. a same-bench TX-power A/B traced weak BLE to `btc_mode=0` (−92 dBm → −67 dBm enabled); the sustained-load BLE drop was pinned by gdb in-flight buffer counters at pool exhaustion (`alloc{TX 1217, RX 27471}` vs `release{TX 1197, RX 27471}` → RX balanced, 20 TX buffers stuck in the WHD TX queue draining the shared pool), and load-scaling confirmed it (no/half WiFi load held 300 s clean, moderate load dropped ~101 s) — closed by the buffer reserve.

**Reproduction.** A self-contained coexistence harness (`test/coex/`: `soak.sh`, `ble_central.py`, reset-until-discoverable) plus archived raw logs for both transports.

## FIELD: Dependencies

- `drivers/bluetooth/hci/` — a new shared-bus/gSPI HCI transport for the CYW43439 (complements, doesn't replace, the existing `BT_HCI_UART_INFINEON` UART path).
- `drivers/wifi/infineon/` (AIROC/WHD WiFi) — shares the bus; gains the coexistence arbitration (bus lock + buffer reserve).
- `hal_rpi_pico` — pico-sdk `cybt_shared_bus` (the BT transport) + the re-read fix.
- `hal_infineon` — the Murata-1YN NVRAM (`btc_mode`) and WHD WiFi firmware.
- **BT controller firmware (blob).** A Zephyr-accepted CYW43439 BT firmware blob already exists: `hal_infineon` ships `zephyr/blobs/img/bluetooth/firmware/COMPONENT_43439/COMPONENT_MURATA-1YN/bt_firmware.hcd` (declared in its `module.yml` manifest under the Infineon EULA `license.txt`, opt-in via `west blobs fetch hal_infineon`), used today by the UART driver (`BT_HCI_UART_INFINEON`). The catch is that the gSPI/`cybt` path does **not** load that `.hcd` — it compiles in pico-sdk's BT *patchram* (`cyw43_btfw_43439.h` / `brcm_patchram_buf[]`), a different firmware artifact and format from a different source (Raspberry Pi pico-sdk). So the open dependency is *which* BT firmware the upstream gSPI transport uses: **(a)** reuse the already-accepted `hal_infineon` `.hcd` blob — single source, license already cleared — which needs a gSPI download path that accepts the `.hcd` container; or **(b)** carry the pico-sdk patchram as a *new* blob in a HAL module, which needs its license (Raspberry Pi's redistribution terms over the underlying vendor firmware) cleared through Zephyr's binary-blobs policy. Mechanics are the same either way: the blob lives in a HAL module's `zephyr/blobs/`, is declared in `module.yml` (path + sha256 + `license-path` + url + type), is opt-in via `west blobs fetch` (never auto-fetched, requires explicit acceptance), and follows the project's binary-blobs policy for non-Apache vendor firmware. Option (a) is strongly preferred — it reuses an already-vetted blob and avoids a new license review. (The WiFi firmware blob is a non-issue: it is already a `hal_infineon` EULA blob, which is how the in-tree AIROC WiFi driver ships.)
- Board: the `rpi_pico2/...w` (and `rpi_pico/...w`) board(s) need a `zephyr,bt-hci` chosen + DT node wired to the new transport.
- Reviewers/areas (MAINTAINERS): Bluetooth HCI, Infineon AIROC WiFi, the two HAL modules.

## FIELD: Concerns and Unresolved Questions

1. **Where should a gSPI / shared-bus BT-HCI transport for the CYW43439 live**, and is reusing pico-sdk `cybt_shared_bus` (via `hal_rpi_pico`) acceptable, or do you want a WHD-native BT transport written?
2. Where should the **coexistence arbitration** live — in the WiFi driver (as prototyped), a dedicated coex layer, or the BT HCI driver?
3. For the **BT firmware blob** (see Dependencies): should the gSPI transport **reuse the already-accepted `hal_infineon` `.hcd` blob** (needs a gSPI loader for that container) or carry pico-sdk's patchram as a **new** blob (needs a fresh license review)? Strong preference for reusing the existing one.
4. Would you prefer this start as an **out-of-tree module** (we have one staged), with only the transport-agnostic fixes upstreamed for now?

Honest caveats: BT bring-up after an SWD *warm* reset is intermittent (an operator USB power-cycle proved a true cold boot is clean — a debug/warm-reset artifact, not a product defect; the harness uses reset-until-discoverable). The coexistence margin under sustained load needed the buffer-reserve fix to reach the 2 h gate. External deps: `cybt` is BSD-3-Clause (documentable); the BT firmware blob is the licensing item.

## FIELD: Alternatives Considered

**1. The existing `BT_HCI_UART_INFINEON` (H4 / UART) path.** It already supports the CYW43439 — but only where the controller's HCI is on a UART. The Pico 2 W / Pico W expose BT only over the shared gSPI bus, so it does not apply to these boards. This RFC adds the missing shared-bus transport; it does not replace the UART one.

**2. The georgerobotics `cyw43_ll` + `cybt` stack (the pico-sdk-style shared-bus path).** This is the other way to drive the CYW43439's gSPI BT, and we hardened + tested it exhaustively on hardware: full bring-up + RX-robustness + a fixed poll-thread priority inversion + a clean init-order matrix, BD_ADDR stable 10/10 boots, a bounded 8-boot cold-boot soak at **0 faults / 6,070 notifications / 630 s**. But the `§2.7` corruption is fundamental to it — gdb-proven *below* the host bus lock (the bus mutex is held by the BT TX thread, yet the controller ring index reads back out of range), so a single sustained link **faults at ~19 min (11,180 notifications)** and moderate load gives **≈ 2 faults per 4 connections**. The 2 h zero-fault gate is unreachable on it, so we built the coexistence path on the WHD WiFi transport instead. (We still propose upstreaming its transport-agnostic `cybt` re-read hardening, which makes any `cybt` user degrade gracefully instead of panicking.)

**3. A WHD-native BT transport (no `cybt`).** None exists upstream; a larger effort. Open question #1 — we retained `cybt` to get a working, fully-tested result; a native transport could replace it later.

**4. Out-of-tree module only (no mainline).** A valid distribution vehicle (we have one staged), but it leaves mainline Pico 2 W / Pico W users without Bluetooth. We propose upstreaming at least the two transport-agnostic fixes and using this RFC to find the right mainline home for the BT transport + coexistence pieces.
