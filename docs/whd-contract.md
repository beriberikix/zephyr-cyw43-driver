# WHD Port — Transport Contract Checklist

A future Infineon WHD/AIROC shared-bus BT transport must satisfy every box below.
Items 1–6 are behaviors to **preserve**; item 7 is the defect to **fix**.

Source of record: [`REFERENCE.md`](../REFERENCE.md) §2 (each box cites its section).
The BT HCI driver (`zephyr_cyw43_bt_hci_drv.c`) depends ONLY on the thin transport
primitives in §2.2 plus Zephyr's `bt_hci` device API — never on WiFi-driver
internals. Re-implementing those primitives for WHD is the whole port.

## 1. HCI-over-gSPI framing (§2.1)

- [ ] Prepend the **4-byte transport header**: bytes 0–2 = cyw43 shared-bus header, **byte 3 = H4 packet-type indicator**, byte 4+ = HCI packet (no extra H4 byte).
- [ ] `write(buf, len)` / `read(buf, max, *len)` take/return buffers that **include** the 4-byte header, with `len` counting it.
- [ ] TX copies the host `net_buf` verbatim from header index 3 (host `data[0]` is already the H4 indicator); validate type is CMD/ACL/ISO and bound length against the TX buffer.
- [ ] RX switches on the H4 type at index 3, allocates the matching Zephyr buffer (`bt_buf_get_evt` for EVT, `bt_buf_get_rx(BT_BUF_ACL_IN/ISO_IN)` for ACL/ISO), bounds the parsed HCI length against both the received payload and net_buf tailroom, then `bt_hci_recv(dev, buf)`.
- [ ] Drop SCO (out of scope; never delivered by this controller).
- [ ] Drop (never dereference) NULL allocations from pool exhaustion or a discardable advertising report under `K_NO_WAIT`.

## 2. Transport primitives — the WHD seam (§2.2)

Provide equivalents with the same framing and the same idempotent/ordering-independent guarantees:

- [ ] `hci_init()` — power chip + load BT firmware (`ensure_bt_up`); **idempotent**; returns 0 on success.
- [ ] `hci_write(buf, len)` — write one framed HCI packet; calls `ensure_bt_up` first; returns 0 on success.
- [ ] `hci_read(buf, max, *len)` — read one framed HCI packet; `*len` includes the 4-byte header; **returns non-zero on bus error** (leaving `*len` meaningless) so callers can check.
- [ ] `bt_has_work(ll)` — true when the controller has BT data pending (polled by the poll loop).
- [ ] `ensure_up()` / `ensure_bt_up()` — idempotent bring-up: raise WL_REG_ON / load WiFi fw / load BT fw as needed.

## 3. Single-bus arbitration — the single-lock invariant (§2.3)

- [ ] Serialize the ONE gSPI bus with ONE recursive, owner-tracked, depth-counted mutex (priority inheritance).
- [ ] Keep a single **cooperative** poll thread as the **sole bus reader**; service **BT first, then WiFi** under the lock.
- [ ] Hold the bus lock around **every** bus access on **every** path, **TX included** (upstream leaves the BT TX write unlocked — do not repeat that omission).
- [ ] Keep the poll thread **cooperative** (the ll relies on no-preemption for implicit mutual exclusion; preemptible → state corruption / asserts).
- [ ] Run the poll thread at **coop -2**, *below* the BT consumers (RX WQ at -8), and rely on the **per-wake full-ring drain** then block on the semaphore to meet `HCI_CMD_TIMEOUT` (do NOT raise poll priority above the BT threads — it over-drives the bus and does not fix the §2.7 overflow).

## 4. Init / power ordering (§2.4)

- [ ] Treat **WL_REG_ON** as the single chip-enable for the whole CYW43439 (WiFi+BT); there is **no separate BT_REG_ON**.
- [ ] Load BT firmware lazily on the first BT op; keep `ensure_up` **idempotent**.
- [ ] Make `wifi disconnect` a `cyw43_wifi_leave()` only — it must **not** drop WL_REG_ON.
- [ ] **Never tie BT liveness to WiFi association state**: all init orders (BT-only, WiFi-then-BT, BT-then-WiFi) work and BT survives WiFi assoc cycling.

## 5. BD_ADDR derivation (§2.5)

- [ ] Derive the BT **public** address as WiFi MAC **+ 1** (48-bit big-endian increment, from OTP).
- [ ] Expose the same verification: `Read_BD_ADDR` == `cyw43_state.mac + 1`, failing loudly on a zero/broadcast address; keep it stable across ≥10 cold boots.

## 6. Controller capability limits — scope, not bugs (§2.6)

- [ ] Use **legacy advertising only** — no LE Extended Advertising (HCI 0x2036/0x203a → "Unknown HCI Command").
- [ ] Assume **no LE ISO** (no CIS/BIS audio); `iso listen` → -ENOTSUP. These are CYW4343A2 firmware limits that bound the BLE feature scope.

## 7. The gSPI corruption fix WHD must add (§2.7)

The current driver cannot satisfy this below the host lock — it is the headline WHD work item:

- [ ] Make the BT backplane ring-index read **robust at the transport**: detect the out-of-range index and **re-read** (the corruption is transient) instead of asserting.
- [ ] And/or apply the WiFi path's gSPI **F1-overflow recovery** to the BT read path.
- [ ] Carry the fix as a **durable patch/fork** of the shared-bus module (`cybt_shared_bus_driver.c::cybt_get_bt_buf_index`), **not** a raw edit that `west update` would revert.
- [ ] Until fixed: certify against the bounded soak envelope and bound concurrent WiFi throughput when a BLE link must stay up.
