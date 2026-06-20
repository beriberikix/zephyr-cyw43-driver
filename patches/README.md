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
