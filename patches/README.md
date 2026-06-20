# Durable patches (whd-port branch)

These patches modify files in **west-managed** trees (the upstream Zephyr
checkout / HAL modules). `west update` reverts them, so they are carried here
and must be re-applied after any `west update`.

## airoc_whd_hal_spi_shared_bus_lock.patch

**Target:** `zephyr/drivers/wifi/infineon/airoc_whd_hal_spi.c` (the upstream
Zephyr AIROC WiFi SPI HAL).

**Why (W6, the §2.7 fix):** the Pico 2 W shares ONE gSPI bus between WiFi
(WHD's `whd_thread`) and BT (`cybt_shared_bus`, driven from
`drivers/wifi/zephyr_cyw43/src/whd/whd_bt_glue.c`). WHD has no bus mutex — it
relies on "only `whd_thread` touches the bus", which the BT path violates.
Under concurrent WiFi+BT bus traffic this corrupts the gSPI transfer (the
`REFERENCE.md §2.7` fault: WiFi association fails, BT ring indices read back
corrupt). The patch brackets the whole `whd_bus_spi_transfer()` body — including
the `SPI_DATA_IRQ_SHARED` pinctrl/IRQ toggle — with a shared recursive lock
(`whd_bus_lock()/whd_bus_unlock()`), which the BT path (`cyw43_thread_enter/exit`)
also takes. The lock is defined (strong) in
`drivers/wifi/zephyr_cyw43/src/whd/whd_transport.c`; the patch adds `__weak`
no-op fallbacks so a stock build without this module still links.

**Apply:**
```
cd <west-topdir>/zephyr
git apply <this-repo>/patches/airoc_whd_hal_spi_shared_bus_lock.patch
```

**Verify applied:** `grep -n whd_bus_lock zephyr/drivers/wifi/infineon/airoc_whd_hal_spi.c`
should show the `__weak` fallbacks + the lock/unlock around the transfer.

This is the "carry the transport fix as a durable patch, not a raw edit
`west update` reverts" requirement from `REFERENCE.md §2.7` / `docs/whd-contract.md`
item 7.

## cybt_shared_bus_reread_index.patch

**Target:** `modules/hal/rpi_pico/src/rp2_common/pico_cyw43_driver/cybt_shared_bus/cybt_shared_bus_driver.c`
(the pico-sdk BT shared-bus driver in the `hal_rpi_pico` west module).

**Why (W6, the §2.7 transport fix proper):** `cybt_get_bt_buf_index()` reads the
BT controller's shared-memory ring indices over the gSPI backplane and upstream
**`assert()`s** if any index is out of range — which aborts/panics the whole
system. Under concurrent gSPI traffic (and even transiently at bring-up) those
indices can read back corrupt (the §2.7 fault). The patch makes the function
**re-read up to 8×** (the corruption is transient) and, only if it persists,
return `CYBT_ERR_HCI_READ_FAILED` so the caller drops the cycle gracefully
(`cyw43_bluetooth_has_pending()` → "no pending") instead of aborting. Fixed a
real boot-time kernel panic in the WHD BT poll thread.

**Apply:**
```
cd <west-topdir>/modules/hal/rpi_pico
git apply <this-repo>/patches/cybt_shared_bus_reread_index.patch
```
**Verify:** `grep -n CYBT_BUF_INDEX_REREAD_MAX <...>/cybt_shared_bus_driver.c`.

NB this file is compiled by BOTH transports (it is the shared BT bus driver), so
the re-read hardening benefits the georgerobotics backend too.

## whd_nvram_43439_1yn_btcoex.patch

**Target:** `modules/hal/infineon/whd-expansion/WHD/COMPONENT_WIFI5/resources/nvram/COMPONENT_43439/COMPONENT_MURATA-1YN/cyfmac43439-1YN.txt`
(the WHD WiFi NVRAM for the Murata 1YN module = Pico 2 W, in the `hal_infineon`
west module — selected by `CONFIG_CYW43439_MURATA_1YN`).

**Why (W4, the BT TX-power fix):** the stock WHD Murata-1YN NVRAM ships with BT
coexistence **disabled** (`btc_mode=0`, `muxenab=0x11`). The CYW43439 on the
Pico 2 W has ONE shared WiFi/BT antenna, so with coex off the BT radio cannot
arbitrate antenna access and its TX is parked — BLE advertising came out ~30 dB
weak (RSSI ~-92 / undiscoverable), which dropped every BLE link under WiFi load.
The georgerobotics stack runs `btc_mode=1` + `muxenab=0x100` and its BT is
healthy (-70 dBm) on the same chip. This patch matches that known-good coex
config (`btc_mode=0`→`1`, `muxenab=0x11`→`0x100`); WHD BT then advertises at
-67 dBm (= georgerobotics). Root-caused via a same-bench georgerobotics-vs-WHD
A/B (docs/artifacts/w4_btx_geo_vs_whd_discriminator_20260620.log).

**Apply:**
```
cd <west-topdir>/modules/hal/infineon
git apply <this-repo>/patches/whd_nvram_43439_1yn_btcoex.patch
```
**Verify:** `grep -nE 'btc_mode|muxenab' <...>/COMPONENT_MURATA-1YN/cyfmac43439-1YN.txt`
should show `muxenab=0x100` and `btc_mode=1`. Rebuild with `-p always` (the NVRAM
size is captured at CMake configure time).
