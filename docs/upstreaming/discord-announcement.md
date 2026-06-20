# Discord announcement (Zephyr) — RFC

Keep the Discord footprint small: announce in the two **vendor** channels only —
**`#raspberrypi`** and **`#infineon`** — since they own the hardware/firmware and are
the most-invested audiences. The **GitHub RFC issue is canonical**: the architectural
"where should a gSPI BT-HCI transport live?" question is answered there (the BT/WiFi
maintainers engage via the RFC label + `MAINTAINERS.yml` auto-assignment), so a
generic `#bt-hci`/`#wifi`/`devel@` post is not needed.

**Sequence:** create the GitHub RFC issue first (so you have the link), then post the
two messages below with `<RFC link>` filled in.

---

## `#raspberrypi`
> 👋 RFC for Pico 2 W / RP2350: **WiFi+BT coexistence over the CYW43439 shared gSPI bus** — <RFC link>
> The in-tree AIROC/WHD WiFi driver + a re-arbitrated BT path (retains pico-sdk `cybt`) sustains a **2 h continuous WiFi+BLE soak, 0 faults** on `rpi_pico2/rp2350a/m33/w` — where the cybt-on-georgerobotics stack faults at ~19 min on a below-the-host-lock ring-index corruption (gdb-proven). Includes a pico-sdk `cybt_shared_bus` hardening (re-read a transient corrupt index instead of `assert()`→panic) I'd also send upstream to pico-sdk. Direction/feedback welcome in the issue. 🙏

## `#infineon`
> 👋 RFC for the CYW43439 (Pico 2 W) on the in-tree AIROC/WHD driver + a re-arbitrated BT path — <RFC link>
> Sustains a **2 h WiFi+BLE coexistence soak, 0 faults** (71k notifications). Two findings for Infineon: (1) the Murata-1YN NVRAM ships **`btc_mode=0`** → BT coex disabled → ~30 dB BLE TX deficit on the single shared antenna (`btc_mode=1`+`muxenab=0x100` → −92→−67 dBm, = the georgerobotics config); (2) a small `airoc_wifi.c` buffer fix (+ a `net_buf` leak fix) for sustained coex. Would value Infineon's take on the NVRAM coex default. Full logs in the issue.

---

Notes:
- Don't paste the full RFC into chat — these are hooks that link the issue.
- If someone in either channel pulls in BT/WiFi subsystem folks, great, but you don't
  need to seed `#bt-hci`/`#wifi` yourself — the RFC issue already routes to them.
