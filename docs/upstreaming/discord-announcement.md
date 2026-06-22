# Discord announcement (Zephyr) — RFC

Keep the Discord footprint small: announce in the two **vendor** channels only —
**`#raspberrypi`** and **`#infineon`** — since they own the hardware/firmware and are
the most-invested audiences. The **GitHub RFC issue is canonical**: the architectural
"where should a shared-bus BT-HCI transport live?" question is answered there (the
BT/WiFi maintainers engage via the RFC label + `MAINTAINERS.yml` auto-assignment), so
a generic `#bt-hci`/`#wifi`/`devel@` post is not needed.

Lead with the first-order gap: **the Pico 2 W has no Bluetooth on Zephyr** (its
CYW43439 BT is on the shared gSPI bus, for which there's no in-tree transport;
the existing Infineon HCI driver is UART-only). Coexistence is the bonus.

**Sequence:** create the GitHub RFC issue first (so you have the link), then post the
two messages below with `https://github.com/zephyrproject-rtos/zephyr/issues/111811` filled in.

---

## `#raspberrypi`
> 👋 RFC: **Bluetooth for the Pico 2 W** (and Pico W) on Zephyr — https://github.com/zephyrproject-rtos/zephyr/issues/111811
> Today these boards have WiFi (the in-tree AIROC driver) but **no usable BLE**: the CYW43439's BT is HCI-over-the-shared-gSPI-bus, and Zephyr only has the *UART* Infineon HCI driver — no shared-bus transport. I have one working (presents pico-sdk `cybt` as a `bt_hci` device), plus **WiFi+BLE coexistence** on that one bus: a **2 h soak, 0 faults** on `rpi_pico2/rp2350a/m33/w`. Includes a pico-sdk `cybt_shared_bus` hardening (re-read a transient corrupt index instead of `assert()`→panic) I'd send to pico-sdk too. Direction/feedback welcome in the issue. 🙏

## `#infineon`
> 👋 RFC: **Bluetooth (HCI over the shared gSPI bus) for the CYW43439** — Pico 2 W / Pico W — https://github.com/zephyrproject-rtos/zephyr/issues/111811
> Zephyr has CYW43439 BT only over the *UART* HCI path (`BT_HCI_UART_INFINEON`); on these boards BT shares the gSPI bus with WiFi, so there's no in-tree transport. I wired up the shared-bus path + **WiFi+BLE coexistence** (2 h soak, 0 faults). Two findings for Infineon: (1) the Murata-1YN NVRAM ships **`btc_mode=0`** → BT coex disabled → ~30 dB BLE TX deficit on the single shared antenna (`btc_mode=1`+`muxenab=0x100` → −92→−67 dBm); (2) a small `airoc_wifi.c` buffer fix (+ a `net_buf` leak fix). Would value your take on the NVRAM coex default + where a shared-bus BT-HCI transport should live. Full logs in the issue.

---

Notes:
- Don't paste the full RFC into chat — these are hooks that link the issue.
- If someone pulls in BT/WiFi subsystem folks, great, but you don't need to seed
  `#bt-hci`/`#wifi` yourself — the RFC issue already routes to them.
