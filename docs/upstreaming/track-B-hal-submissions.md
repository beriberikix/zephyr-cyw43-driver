# Track B — HAL / vendor-module submissions

Two of the four fixes live in west modules that mirror **external upstream
projects**, not in `zephyrproject-rtos/zephyr`. They go to those projects (and/or
the Zephyr HAL module that vendors them). Each is independently useful (not
coex-only), so neither has to wait on the RFC.

---

## B1 — `cybt` re-read instead of `assert()` (→ pico-sdk / `hal_rpi_pico`)

**Patch:** `patches/cybt_shared_bus_reread_index.patch`
**File:** `src/rp2_common/pico_cyw43_driver/cybt_shared_bus/cybt_shared_bus_driver.c`
**Provenance:** this file is from the **Raspberry Pi pico-sdk**; Zephyr vendors it via
`zephyrproject-rtos/hal_rpi_pico`. Primary upstream is pico-sdk.

**What it fixes:** `cybt_get_bt_buf_index()` reads the BT controller's shared-memory
ring indices over the gSPI backplane and `assert()`s if an index is out of range —
which aborts/panics the whole system. Under transient bus contention (and even at
bring-up) those indices can read back briefly corrupt. The patch **re-reads up to 8×**
and, only if it persists, returns `CYBT_ERR_HCI_READ_FAILED` so the caller drops the
cycle gracefully instead of asserting. This is a robustness fix that benefits **all**
cybt users, not just coexistence.

**Where to send it:**
1. Preferred: a PR to **raspberrypi/pico-sdk** (the true upstream).
2. Also/short-term: a PR to **zephyrproject-rtos/hal_rpi_pico** (so Zephyr users get
   it without waiting for a pico-sdk roll-up), noting the pico-sdk PR link.

**Prepared commit message (adjust the area prefix to match the target repo's style):**
```
pico_cyw43_driver: cybt: re-read transient corrupt ring index

cybt_get_bt_buf_index() asserts when the BT controller's bt2host /
host2bt ring indices read back out of range over the gSPI backplane.
Under transient bus contention these indices can read corrupt for a
single access, so the assert turns a recoverable glitch into a system
abort. Re-read the index up to CYBT_BUF_INDEX_REREAD_MAX (8) times and,
if it still does not validate, return CYBT_ERR_HCI_READ_FAILED so the
caller can drop the cycle instead of aborting.

Reproduced on a CYW43439 (Pico 2 W) under concurrent WiFi+BT load: the
assert fired at boot and under sustained traffic; with the re-read the
controller recovers and BT stays up.

Assisted-by: Claude Opus 4.8 <noreply@anthropic.com>
```
> Add YOUR `Signed-off-by` via `git commit -s`. pico-sdk uses the DCO too; check the
> target repo's CONTRIBUTING for any extra requirements (e.g. their PR template).

**Steps (human):**
```sh
# pico-sdk
git clone git@github.com:<you>/pico-sdk.git && cd pico-sdk
git checkout -b cybt-reread-corrupt-index <pico-sdk default branch>
git apply <this-repo>/patches/cybt_shared_bus_reread_index.patch   # path is relative to the cybt dir; adjust -p as needed
$EDITOR src/rp2_common/pico_cyw43_driver/cybt_shared_bus/cybt_shared_bus_driver.c   # verify
git commit -s            # paste the message above
git push origin cybt-reread-corrupt-index   # open PR vs raspberrypi/pico-sdk
```

---

## B2 — enable BT coex in the Murata-1YN NVRAM (→ Infineon / `hal_infineon`)

**Patch:** `patches/whd_nvram_43439_1yn_btcoex.patch`
**File:** `whd-expansion/WHD/COMPONENT_WIFI5/resources/nvram/COMPONENT_43439/COMPONENT_MURATA-1YN/cyfmac43439-1YN.txt`
**Provenance:** an **Infineon WHD vendor asset**, vendored in
`zephyrproject-rtos/hal_infineon`. Primary upstream is Infineon.

**What it fixes:** the shipped Murata-1YN NVRAM has BT coexistence **disabled**
(`btc_mode=0`, `muxenab=0x11`). The CYW43439 on the Pico 2 W has a **single shared
WiFi/BT antenna**, so with coex off the BT radio can't arbitrate antenna access and
its TX is parked — BLE advertised ~30 dB weak (−92 dBm, intermittently
undiscoverable), dropping every BLE link under WiFi load. Setting `btc_mode=1` +
`muxenab=0x100` (matching the known-good georgerobotics config) restores BT to
−67 dBm (= georgerobotics −70). Verified by a same-bench A/B
(`docs/artifacts/w4_btx_geo_vs_whd_discriminator_20260620.log`).

**Open question (raise in the submission):** is the correct fix to (a) change the
**vendor NVRAM asset** in hal_infineon/Infineon, or (b) override `btc_mode`/`muxenab`
from the **board** (a board-level NVRAM overlay / a `CONFIG_`/devicetree knob for "BT
coex enabled on a shared antenna")? Option (b) may be cleaner upstream since it is
board-RF policy, not a generic part default. Ask the Infineon / hal_infineon
maintainers which they prefer.

**Where to send it:**
1. Raise an **issue** on `zephyrproject-rtos/hal_infineon` describing the deficit + the
   A/B evidence, and ask (a) vs (b) above.
2. If they want the asset changed: it likely has to go through **Infineon** (the
   authoritative source of the NVRAM). The hal_infineon maintainers can route it.

**Prepared issue/PR text:**
```
Title: CYW43439 Murata-1YN NVRAM ships BT coex disabled (btc_mode=0) ->
       ~30 dB BLE TX deficit on the shared antenna (Pico 2 W)

The Murata-1YN NVRAM (cyfmac43439-1YN.txt) sets btc_mode=0, muxenab=0x11.
On the CYW43439's single shared WiFi/BT antenna this parks BT TX: BLE
advertises ~30 dB weak (measured -92 dBm, vs -70 dBm with coex enabled on
the same chip/bench) and BLE links drop under any WiFi load.

Setting btc_mode=1 + muxenab=0x100 (the georgerobotics/pico-sdk config for
the same module) restores BT to -67 dBm and lets WiFi+BLE coexist (verified
by a 2 h zero-fault soak on rpi_pico2/rp2350a/m33/w).

Question: should this be fixed in the vendor NVRAM asset, or exposed as a
board-level coex-enable override? Happy to provide the A/B logs and submit
the change wherever you prefer.

Assisted-by: Claude Opus 4.8 <noreply@anthropic.com>
```

> Do not silently change a vendor RF asset without maintainer/Infineon agreement —
> NVRAM/RF parameters are regulatory-adjacent. Lead with the issue + evidence.
