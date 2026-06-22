# Plan: clean, georgerobotics-free WHD coexistence module (new repo)

Goal: a standalone, `west`-installable Zephyr **module** containing ONLY the WHD
WiFi + cybt-BT coexistence path (no georgerobotics fallback), referencing the
`whd-port` fork for provenance. This is the realistic distribution vehicle (the
BT side depends on pico-sdk cybt + an EULA BT blob, so it is not a Zephyr-main
drop-in — see docs and the guidelines check).

## Key scoping insight (verified 2026-06-20)
The BT **transport** is pico-sdk **cybt** (`cybt_shared_bus.c`/`.c`, in
`hal_rpi_pico`) — `cyw43_btbus_init/read/write` are cybt entrypoints, NOT
georgerobotics. So decoupling is **header/type only**, not a functional rewrite:
cybt + the HCI driver + whd_bt_glue just need a few types/protos/macros that today
come from the georgerobotics `cyw43.h`/`cyw43_ll.h`/`cyw43_config.h`/`cyw43_btbus.h`.
Replace those with a small local **compat header**; cybt logic is unchanged.

## Compat-header symbol surface (measured — what cybt + bt_hci + whd_bt_glue use)
Types:
  - `cyw43_ll_t`            (opaque/minimal struct; cybt takes `cyw43_ll_t *self`,
                              noted "naff"/unused — an empty struct is fine)
  - `cyw43_t`               (the `cyw43_state` global: needs `.mac[6]`,
                              `.bt_loaded` (bool), `.cyw43_ll` (cyw43_ll_t))
Function prototypes (all IMPLEMENTED by whd_bt_glue.c / bt_hci driver — the header
only declares them):
  - cyw43_thread_enter / cyw43_thread_exit          (-> whd_bus_lock/unlock)
  - cyw43_delay_ms
  - cyw43_ll_read_backplane_reg / cyw43_ll_write_backplane_reg
  - cyw43_ll_read_backplane_mem  / cyw43_ll_write_backplane_mem
  - cyw43_ll_bt_has_work, cyw43_ll_process_packets   (bt_hci uses; provide stubs/impl)
  - cyw43_btbus_init / cyw43_btbus_read / cyw43_btbus_write   (cybt entrypoints)
  - cyw43_bluetooth_hci_init / _read / _write, cyw43_bluetooth_has_pending,
    cyw43_bluetooth_hci_process  (the SEAM-1 surface)
Macros/constants (copy values from the georgerobotics headers):
  - CYW43_PACKET_HEADER_SIZE, CYW43_BUS_MAX_BLOCK_SIZE, CYW43_BT_DRAIN_MAX,
    CYW43_USE_HEX_BTFW, CYW43_THREAD_ENTER/EXIT, CYW43_SDPCM_SEND_COMMON_WAIT,
    CYW43_EVENT_POLL_HOOK
Blob: cybt includes `cyw43_btfw_43439.h` (the BT patchram, gated by
CYW43_USE_HEX_BTFW) — carry this BT-firmware header into the new repo (EULA/blob).
WiFi firmware/NVRAM are NOT needed (WHD/hal_infineon provides them).

## New repo layout (proposed)
```
whd-coex-cyw43/                       # new clean repo (geo-free)
  zephyr/module.yml                   # Zephyr module manifest
  west.yml                            # pins zephyr + hal_infineon + hal_rpi_pico
  drivers/wifi/whd_coex/
    CMakeLists.txt  Kconfig           # WHD-only (no CYW43_TRANSPORT choice)
    src/whd_bt_glue.c                 # from src/whd/ (drop cyw43.h/cyw43_btbus.h
                                      #   includes -> include cyw43_compat.h)
    src/whd_transport.c  whd_bus_lock.h
    src/zephyr_cyw43_bt_hci_drv.c     # the shared HCI driver (compat include)
    src/cybt/                         # cybt_shared_bus{,_driver}.{c,h} (vendored
                                      #   from hal_rpi_pico, OR referenced via west)
    include/compat/cyw43_compat.h     # the shim above; also thin cyw43_ll.h /
                                      #   cyw43_config.h / cyw43_btbus.h / cyw43.h
    include/blob/cyw43_btfw_43439.h   # BT patchram (EULA blob)
  patches/                            # the 4 durable patches + README
  docs/                               # whd-contract.md + the W6 soak evidence
  app/  test/coex/                    # the repro app + soak harness (optional)
  README.md  PROVENANCE.md            # reference beriberikix/...@whd-port + SHAs
```
NB: the georgerobotics submodule `src/cyw43-driver/` is DROPPED entirely; the
compat header replaces its cyw43.h/cyw43_ll.h/cyw43_config.h/cyw43_btbus.h.

## Provenance / "reference my fork & branches"
- `west.yml` pins `zephyr`, `hal_infineon`, `hal_rpi_pico` at the SHAs used here;
  optionally lists `beriberikix/zephyr-cyw43-driver` as a remote.
- `README.md`: "Extracted from beriberikix/zephyr-cyw43-driver @ whd-port; see
  PROVENANCE.md." Link the PR (#1) and the 2 h soak artifact.
- `PROVENANCE.md`: file -> source commit SHA (e.g. whd_bt_glue.c <- 564f03b,
  patches/* <- 564f03b/5791422, bt_hci_drv <- a15c616).

## Build matrix to keep (in the new repo)
- WHD wifi+bt (the coex image)   - WHD wifi-only (CONFIG_BT=n)

## VERIFICATION GATE (must pass before calling the new repo "working")
This is a BT-transport refactor -> per the loop rule, re-run the coex soak:
  1. Build the new module's wifi+bt image + wifi-only image (both green).
  2. Flash; reset-until-BT-discoverable; confirm BLE advertises (-67 dBm).
  3. Re-run the moderate-load coex soak (>=300 s, target 2 h) -> 0 faults,
     notifications flowing == parity with docs/artifacts/w6_soak_2h_clean_*.
Only then is the decoupled tree verified-equivalent to whd-port @ 16777c4.

## Carries over (unchanged by a new repo)
- DCO: any upstream-bound commit needs the human's real `Signed-off-by` (an AI
  must not add it) + `Assisted-by:` trailer. Drop the `Co-Authored-By/Claude-
  Session` trailers for upstream.
- Licensing: cybt = BSD-3 (pico-sdk), document `Origin:`; the BT blob is the
  EULA/governing-board item if this ever targets Zephyr main.
- The Zephyr-main `airoc_wifi.c` leak fix is a SEPARATE submission to a fork of
  `zephyrproject-rtos/zephyr`, not this module repo.
