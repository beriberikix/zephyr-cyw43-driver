/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Infineon AIROC/WHD transport backend for the CYW43 WiFi+BLE coexistence
 * driver (whd-port branch).
 *
 * WiFi is provided by the upstream Zephyr AIROC driver (CONFIG_WIFI_AIROC) over
 * the board's stock infineon,airoc-wifi node; BT runs over the same WHD bus via
 * whd_bt_glue.c. This file holds the shared gSPI bus lock both sides take
 * (see whd_bus_lock.h) — the W6 fix for the §2.7 concurrent-bus corruption.
 */

#include <zephyr/kernel.h>

#include "whd_bus_lock.h"

/*
 * Single recursive mutex serializing ALL gSPI bus access — WHD's WLAN path
 * (via the durable airoc_whd_hal_spi patch) and the BT path (via
 * whd_bt_glue.c's cyw43_thread_enter/exit). k_mutex is recursive (per-owner
 * lock_count) and grants priority inheritance, matching the georgerobotics
 * single-bus lock the cybt code was written against.
 */
static K_MUTEX_DEFINE(whd_bus_mutex);

void whd_bus_lock(void)
{
	k_mutex_lock(&whd_bus_mutex, K_FOREVER);
}

void whd_bus_unlock(void)
{
	k_mutex_unlock(&whd_bus_mutex);
}
