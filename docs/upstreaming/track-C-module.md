# Track C — out-of-tree WHD coexistence module (the BT-over-WHD feature)

The novel part — BT-over-WHD coexistence (`src/whd/whd_bt_glue.c` + the retained
pico-sdk `cybt`) plus the **two coex-motivated Zephyr patches** (the shared bus lock
and the `airoc_pool` buffer reserve) — is **not a mainline drop-in**:

- there is no WHD-native gSPI BT-HCI transport, so it retains pico-sdk `cybt`;
- it pulls in a vendored BT firmware **blob** (governing-board/EULA territory);
- the two Zephyr-side patches only make sense *with* this BT feature, so they are
  gated on the RFC (Track 0) deciding where coexistence arbitration should live.

So the realistic vehicle today is a **standalone, `west`-installable Zephyr module**.

## This is already scoped
See **`docs/whd-standalone-module-plan.md`** (commit `cc93027`) for the full plan:
- key finding: the georgerobotics coupling is **header/type-only** (cybt is pico-sdk),
  so decoupling is a small `cyw43_compat.h` shim, not a rewrite;
- the measured compat symbol surface, the new-repo layout, the `west.yml`/`module.yml`
  manifest, the `PROVENANCE.md` mechanism to reference the `whd-port` fork + SHAs;
- a **build + coexistence-soak verification gate** (the module must reproduce the
  2 h / 0-fault parity before it is considered working).

## What lands here vs upstream
| Piece | Home |
|---|---|
| `whd_bt_glue.c`, `whd_transport.c`, `whd_bus_lock.h`, the bt_hci driver, `cyw43_compat.h`, BT-fw blob | **this module** |
| shared gSPI bus lock (`airoc_whd_hal_spi.c`) | this module's `patches/` **until** the RFC blesses a mainline home |
| `airoc_pool` buffer reserve (`airoc_wifi.c`) | this module's `patches/` (the **leak-fix half** goes upstream via Track A) |
| `cybt` re-read, `btc_mode` NVRAM | their own upstreams (Track B); the module pins fixed SHAs / carries patches meanwhile |

## Sequence
1. Land Track A (leak fix) and open Track B PRs.
2. Post the RFC (Track 0); incorporate maintainer direction on where the bus
   lock / buffer reserve / BT transport should live.
3. Build the module repo per `docs/whd-standalone-module-plan.md`, **re-run the coex
   soak to prove parity**, then publish it and link it from the RFC as the reference
   implementation.

Provenance: the module's `README.md` + `PROVENANCE.md` reference
`beriberikix/zephyr-cyw43-driver @ whd-port` and the per-file commit SHAs
(`whd_bt_glue.c ← 564f03b`, the four patches ← their commits, etc.).
