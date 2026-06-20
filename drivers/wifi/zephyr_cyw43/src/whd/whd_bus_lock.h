/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared gSPI bus lock for the WHD backend (whd-port, W6).
 *
 * The Pico 2 W shares ONE gSPI bus between WiFi (WHD's whd_thread) and BT
 * (cybt_shared_bus, driven from whd_bt_glue.c). WHD's Zephyr SPI HAL
 * (airoc_whd_hal_spi.c::whd_bus_spi_transfer) has no mutex — WHD relies on
 * "only whd_thread touches the bus", which the BT path violates. Under
 * concurrent WiFi+BT traffic this corrupts the gSPI transfer (the §2.7 fault:
 * WiFi join fails / ring indices read back corrupt).
 *
 * This single recursive mutex is taken by BOTH paths:
 *   - WHD WLAN: a durable patch brackets whd_bus_spi_transfer() with it
 *     (see patches/airoc_whd_hal_spi_shared_bus_lock.patch).
 *   - BT: whd_bt_glue.c's cyw43_thread_enter/exit map to it, held across each
 *     whole cybt read-modify-write sequence.
 * Recursive so the BT path (which holds it across a sequence) can re-enter when
 * its backplane ops call back into whd_bus_spi_transfer().
 */

#ifndef WHD_BUS_LOCK_H
#define WHD_BUS_LOCK_H

#ifdef __cplusplus
extern "C" {
#endif

void whd_bus_lock(void);
void whd_bus_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* WHD_BUS_LOCK_H */
