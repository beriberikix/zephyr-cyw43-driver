# CYW43 WiFi+BLE Coexistence — Reference / Transport Contract

Reference documentation for the Beechwoods cyw43_ll-based WiFi+BLE coexistence
driver on the Raspberry Pi Pico 2 W (CYW43439 over the shared 3-pin gSPI bus).
A future Infineon WHD/AIROC shared-bus BT transport should mirror the contracts
documented here.

Pinned environment (see PROGRESS.md for the live loop state):
- Zephyr: `44af4534166bad6cec2f5a6eb226021260b9c49d` (v4.4.0-5745), in-tree RP2xxx PIO-SPI.
- Board: `rpi_pico2/rp2350a/m33/w`.
- georgerobotics cyw43-driver (vendored at
  `drivers/wifi/zephyr_cyw43/src/cyw43-driver`): **v1.0.4**
  (`1d2227bca200e13c1d6a630032d5f6d7fca69fef`).

---

## 1. Firmware blob pinning (backlog item 6)

This driver uses the **combined, RP-licensed (non-EULA) WiFi+BT blob** shipped in
the georgerobotics cyw43-driver. It is selected in
`drivers/wifi/zephyr_cyw43/src/cyw43_configport.h` via
`CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE` whenever `CONFIG_BT` is set:

```c
#ifdef CONFIG_BT
#define CYW43_ENABLE_BLUETOOTH (1)
#endif
#if CYW43_ENABLE_BLUETOOTH
#define CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE "wb43439A0_7_95_49_00_combined.h"
#else
#define CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE "w43439A0_7_95_49_00_combined.h"
#endif
```

| Item | Value |
|------|-------|
| Combined WiFi+BT blob (BT builds) | `firmware/wb43439A0_7_95_49_00_combined.h` |
| SHA-256 (header file) | `6b4b9a717c3c5d669b56a3bc38b58df3901f63e3a7363ccf5d74706460762176` |
| WiFi-only blob (non-BT builds) | `firmware/w43439A0_7_95_49_00_combined.h` |
| WiFi-only SHA-256 | `120bb63db1c5cf42cd44cf9a057d6f14601038a9413badf5a3b737f6552bc9ea` |
| Provenance | georgerobotics/cyw43-driver v1.0.4, dir `firmware/` |
| Layout | WiFi firmware padded to 512 B + CLM blob, then the BT firmware patch records appended (the "wb" = WiFi+Bluetooth combined image) |

Runtime-reported versions (from the boot log, `docs/artifacts/m0.0_console_20260619.log`):
- WiFi firmware: `7.95.61 (abcd531 CY)`, CRC `4528a809`, date `2023-01-11`,
  Ucode `1043.2169`, FWID `01-7afb0879`, API `12.2`, CLM import `1.47.1`,
  customization `v5 22/06/24`.
- BT firmware: `CYW4343A2_001.003.016.0031.0000_Generic_SDIO_37MHz_wlbga_BU_dl_signed`.

### License

The firmware blobs are licensed under the **George Robotics / Raspberry Pi
"RP" license** (`drivers/wifi/zephyr_cyw43/src/cyw43-driver/LICENSE.RP`):

> Copyright (C) 2019-2022 George Robotics Pty Ltd. Raspberry Pi Ltd (Licensor)
> hereby grants … a non-exclusive license to use this software solely with the
> Licensor's microcontroller chip (RP2040) or any other semiconductor device
> produced by the Licensor.

The RP2350 (rp_pico2/w) is a Raspberry-Pi-produced device, so this use is within
the license grant. Redistribution in source and binary form is permitted with
the copyright notice retained (conditions 1–3 of LICENSE.RP).

**Do NOT switch to the Infineon EULA blobs.** This reference is intentionally
pinned to the RP-licensed combined blob; the Infineon AIROC/WHD EULA firmware is
out of scope and has incompatible redistribution terms.

### Pinning policy

The blob is pinned transitively by the cyw43-driver submodule SHA (v1.0.4 above).
To change the firmware, bump the submodule, recompute the SHA-256 in this table,
re-run the cold-boot BD_ADDR test and the coexistence soak, and update the
runtime-reported versions.

---

## 2. Transport / arbitration contract (for the WHD port)

A WHD/AIROC shared-bus BT transport must mirror the contracts below. The BT HCI
driver (`zephyr_cyw43_bt_hci_drv.c`) depends ONLY on the thin transport
primitives in §2.2 plus Zephyr's `bt_hci` device API — never on WiFi-driver
internals. Re-implementing those primitives for WHD is the whole port.

### 2.1 HCI-over-gSPI framing

The controller exchanges H:4-style HCI packets over the shared gSPI bus with a
**4-byte transport header** prepended:

```
byte 0..2 : cyw43 shared-bus header (managed by cybt_shared_bus)
byte 3    : H4 packet-type indicator (BT_HCI_H4_CMD/ACL/SCO/EVT/ISO)
byte 4..  : the HCI packet (HCI header + payload), no extra H4 byte
```

- `cyw43_bluetooth_hci_write(buf, len)` and `cyw43_bluetooth_hci_read(buf, max,
  *len)` both take/return a buffer that **includes** this 4-byte header; `len`
  counts it. The last header byte (index 3) is the H4 type.
- TX: the Zephyr host hands the driver a `net_buf` whose `data[0]` is already the
  H4 indicator (the modern device-based `bt_hci` model). The driver copies the
  buffer verbatim starting at header index 3, so the indicator lands in byte 3.
  It validates the type is CMD/ACL/ISO and bounds the length against the TX
  buffer (`zephyr_cyw43_bt_hci_send`).
- RX: the driver reads into a static buffer, validates the read return and the
  reported length, switches on the H4 type at index 3, allocates the correct
  Zephyr buffer (`bt_buf_get_evt` for EVT, `bt_buf_get_rx(BT_BUF_ACL_IN/ISO_IN)`
  for ACL/ISO), bounds the parsed HCI length against both the received payload
  and the net_buf tailroom, and delivers with `bt_hci_recv(dev, buf)`. SCO is
  dropped (out of scope; never delivered by this controller). NULL allocations
  (pool exhaustion, or a discardable advertising report under `K_NO_WAIT`) are
  dropped, not dereferenced. See item 3.

### 2.2 Transport primitives (the WHD seam)

Provided by the cyw43_ll stack (`cyw43_ctrl.c`), consumed by the BT HCI driver:

| Primitive | Contract |
|-----------|----------|
| `cyw43_bluetooth_hci_init()` | Ensure the chip is powered and BT firmware is loaded (`cyw43_ensure_bt_up`). Idempotent. Returns 0 on success. |
| `cyw43_bluetooth_hci_write(buf, len)` | Write one framed HCI packet to the controller. Calls `cyw43_ensure_bt_up` first. Returns 0 on success. |
| `cyw43_bluetooth_hci_read(buf, max, *len)` | Read one framed HCI packet; `*len` includes the 4-byte header. Returns non-zero (and leaves `*len` meaningless) on bus error — callers MUST check. |
| `cyw43_ll_bt_has_work(ll)` | True when the controller has BT data pending; polled by the poll loop. |
| `cyw43_ensure_up()` / `cyw43_ensure_bt_up()` | Idempotent bring-up: raise WL_REG_ON / load WiFi fw / load BT fw as needed. |

A WHD transport must offer equivalents with the same framing and the same
"ensure up is idempotent and ordering-independent" guarantee (§2.4).

### 2.3 Single-lock poll-loop arbitration (the single-bus invariant)

There is ONE gSPI bus shared by WiFi and BT, serialized by ONE recursive mutex
(`zephyr_cyw43_lock`, owner-tracked, depth-counted; gives priority inheritance).

- A single **cooperative** poll thread (`zephyr_cyw43_event_poll_thread`) is the
  sole bus reader. It waits on `event_sem` (signalled by the WL_HOST_WAKE GPIO
  ISR, or a 5 s timeout), takes the lock, and runs `cyw43_poll()` →
  `cyw43_poll_func()`, which services **BT first then WiFi** under the lock:
  `if (bt_has_work) cyw43_bluetooth_hci_process(); if (ll_has_work) ll_process_packets();`
- Host TX paths must take the SAME lock. WiFi TX uses `CYW43_THREAD_ENTER/EXIT`.
  The BT HCI TX path (`zephyr_cyw43_bt_hci_send` → `cyw43_bluetooth_hci_write`)
  is the ONE bus path the upstream georgerobotics/pico-sdk code does NOT wrap, so
  this driver wraps it explicitly in `cyw43_thread_enter()/exit()` (commit
  8df7566). Without it, a BT TX from the `bt_tx_processor` thread issues SPI
  transactions concurrently with the poll thread (the bus code yields/sleeps
  mid-transfer, so even cooperative threads interleave). A WHD transport MUST
  hold the bus lock around every bus access on every path, TX included.
- **Priority rule (item 4):** the poll thread MUST be cooperative (the cyw43_ll
  relies on no-preemption for implicit mutual exclusion — making it preemptible
  corrupts state and asserts). It runs **one cooperative band above both BT
  threads** — `K_PRIO_COOP(MIN(CONFIG_BT_RX_PRIO, CONFIG_BT_HCI_TX_PRIO) - 1)`
  == coop -10, above the BT RX workqueue (-8) and BT HCI TX (-9) — so the sole
  bus reader drains the controller's bt2host ring promptly under load. The
  per-wake drain (`cyw43_bluetooth_hci_process` loops the full pending ring, then
  the poll BLOCKS on `event_sem`) is what releases the CPU to the RX WQ for
  delivery, so command-completes still arrive within `HCI_CMD_TIMEOUT`. (History:
  the original highest-coop value -14 starved the RX WQ — events read but never
  delivered → timeout; the intermediate lowest-coop -2 sat below the RX WQ so the
  ring overflowed under load. Both fixed by -10 + the per-wake drain.)
- A WHD transport sharing the bus must keep this single-lock, single-reader,
  BT-first arrangement with the bus reader prioritized above the BT consumers and
  every TX path holding the lock.

See **§2.7** for a shared-bus coexistence limitation that survives this
arbitration (a gSPI-level corruption below the transport).

### 2.4 Init / power ordering (item 5)

- One chip-enable, **WL_REG_ON**, powers the whole CYW43439 (WiFi+BT). No
  separate BT_REG_ON on the Pico W/2 W.
- WL_REG_ON is raised at WiFi driver init (POST_KERNEL) and WiFi firmware loads
  then, independent of association; it is only lowered by `cyw43_deinit()`.
- BT firmware loads lazily on the first BT op (`cyw43_ensure_bt_up`).
- `wifi disconnect` is `cyw43_wifi_leave()` only — it does NOT drop WL_REG_ON, so
  BT is unaffected.
- Therefore all init orders work (BT-only, WiFi-then-BT, BT-then-WiFi), and BT
  survives WiFi association cycling. A WHD transport must keep `ensure_up`
  idempotent and never tie BT liveness to WiFi association state.

### 2.5 BD_ADDR derivation (item 1)

The controller derives its BT **public** address from the WiFi MAC **+ 1**
(48-bit big-endian increment), read from OTP. The driver's `CONFIG_BT_HCI_SETUP`
hook reads the controller BD_ADDR (HCI `Read_BD_ADDR`) and verifies it equals
`cyw43_state.mac + 1`, failing loudly on a zero/broadcast address. Stable across
≥10 cold boots. A WHD transport should expose the same derivation/verification.

### 2.6 Controller capability notes (CYW4343A2 BT firmware)

- HCI 5.2, manufacturer 0x0131 (Infineon/Cypress).
- Does **not** support LE Extended Advertising (HCI 0x2036/0x203a → "Unknown HCI
  Command"). Use legacy advertising only.
- Does **not** support LE ISO (`iso listen` → -ENOTSUP). No CIS/BIS audio.
- These are controller-firmware limits, not driver limits; relevant to what BLE
  features the coexistence scope can offer.

### 2.7 Shared-bus coexistence limitation — gSPI corruption under heavy load

**Symptom.** Under *sustained heavy concurrent WiFi throughput* simultaneous with
an active BLE connection, the Pico can take a kernel panic (K_ERR_KERNEL_PANIC)
on the `bt_tx_processor` thread:

```
tx_processor -> send_buf -> zephyr_cyw43_bt_hci_send -> cyw43_bluetooth_hci_write
  -> cybt_hci_write_buf -> cybt_get_bt_buf_index
  -> assert(ring_index < BTSDIO_FWBUF_SIZE)   # index read back >= 0x1000 (garbage)
```

**Root cause (gdb-proven, not a host-side bug).** The BT TX path reads the
controller's shared-memory ring indices over the gSPI backplane. At the panic the
cyw43 bus mutex is **held** by `bt_tx_processor` (`lock_count = 1`), i.e. host-side
mutual exclusion was in force and the index *still* read back corrupt. The
corruption is therefore at/below the vendored **pico-sdk `cybt_shared_bus`**
transport: under concurrent WiFi bus traffic the BT backplane read is hit by the
same gSPI F1-overflow / controller-DMA contention the WiFi path (`cyw43_ll`)
explicitly recovers from, but the BT read path does not — and `cybt` `assert()`s
on the resulting out-of-range index instead of retrying. (Upstream even ships a
`CYBT_CORRUPTION_TEST` debug path acknowledging this corruption exists.)

**What this driver already does right (necessary, not sufficient).**
- Poll thread one cooperative band above both BT threads (§2.3) so the bus reader
  drains the ring promptly.
- The BT HCI TX write is wrapped in the bus lock (§2.3, commit 8df7566) — the one
  path upstream leaves unlocked.
These uphold the single-bus arbitration contract but cannot prevent corruption
that occurs *below* host serialization, inside the gSPI transport.

**Operating envelope (hardware-measured).** STA associated + BLE central connected
+ a notify characteristic streaming (~9.6 notif/s) + **bounded bidirectional WiFi
load** (device→internet ping ~1/s, host→device ping ~0.5/s):
- **Short connections:** 8 SWD-reset cold boots × 90 s = **0 faults, 0 unexpected
  disconnects, 6070 notifications / 630 s connected**
  (`test/coex/results/soak_bounded_8boot_20260619.log`).
- **Sustained connection:** a single continuous link streamed cleanly for
  **~19 min (11180 notifications, 9.6/s, max inter-notify gap 0.32 s, 0 stalls)
  and then took the fault at ~1162 s**
  (`test/coex/results/soak_bounded_long_20260619.log`).

So the fault is **probabilistic and low-rate, not eliminated**, even at bounded
load: MTBF is short-connection-clean but ≈ tens of minutes for a sustained link
(it scales with cumulative BLE-TX × WiFi-bus interaction time, and is far more
frequent under heavy throughput — see the moderate-load runs at ~2 faults / 4
connections). No-load / light-load is clean. The original soak gate's "zero
faults over 2 h continuous" is therefore **NOT met**; this residual is accepted
and documented (operator decision) pending the transport-level fix below.

**Recommendation for the WHD port / a production fix.** Make the BT backplane read
robust at the transport: detect the out-of-range index and **re-read** (the
corruption is transient) rather than asserting, and/or apply the WiFi path's
F1-overflow recovery to BT reads. That fix belongs in the shared-bus transport
(here: pico-sdk `cybt_shared_bus_driver.c::cybt_get_bt_buf_index`), so it must be
carried as a durable patch/fork of that upstream module, not a raw edit that
`west update` would revert. Until then, certify against the bounded envelope above
and bound concurrent WiFi throughput when a BLE link must stay up.

