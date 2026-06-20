/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHD BT glue (whd-port, W3) — runs the retained pico-sdk cybt_shared_bus BT
 * transport over the upstream Infineon WHD WiFi bus.
 *
 * There is no AIROC gSPI BT HCI transport upstream, so the BT side keeps
 * cybt_shared_bus (Layer C). cybt's only coupling to the (now absent)
 * georgerobotics cyw43_ll stack is a tiny seam (see docs/whd-contract.md and
 * the BT-over-WHD map): four backplane primitives, a bus lock, a delay, and a
 * cyw43_state global. This file provides all of them, backed by WHD:
 *
 *   cyw43_ll_{read,write}_backplane_reg  -> whd_bus_{read,write}_backplane_value
 *   cyw43_ll_{read,write}_backplane_mem  -> whd_bus_transfer_backplane_bytes
 *   cyw43_thread_enter/exit              -> a recursive BT bus mutex
 *   cyw43_delay_ms                       -> k_msleep
 *   cyw43_state                          -> a minimal global (.mac, .bt_loaded)
 *
 * It also re-implements the §2.2 SEAM-1 primitives the BT HCI driver calls
 * (cyw43_bluetooth_hci_init/read/write) and runs a BT poll thread that drains
 * the controller->host ring (the georgerobotics single poll thread does not
 * exist in WHD mode).
 *
 * SCOPE (W3): BT bring-up + advertise + BD_ADDR with WiFi essentially idle.
 * The BT bus lock here is BT-LOCAL — it serializes cybt's own multi-transfer
 * read-modify-write sequences, but does NOT yet serialize against WHD's WLAN
 * bus thread. Cross-WHD bus arbitration under concurrent WiFi+BT load is W4/W6
 * (the §2.7 corruption envelope). Until then the Zephyr SPI controller's
 * internal per-transceive mutex bounds interleaving at idle.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <string.h>

/* georgerobotics headers (types + the cybt up-edge), from the vendored submodule */
#include "cyw43.h"
#include "cyw43_btbus.h"
#include "cybt_shared_bus_driver.h"

/* WHD / AIROC */
#include "whd.h"
#include "whd_int.h"
#include "whd_wifi_api.h"
#include "whd_bus_protocol_interface.h"
#include "whd_bus_common.h"
#include "airoc_wifi.h"

#include "whd_bus_lock.h"

LOG_MODULE_REGISTER(whd_bt_glue, CONFIG_LOG_DEFAULT_LEVEL);

/* The BT HCI driver and the cybt up-edge reference this global (.mac for the
 * BD_ADDR == WiFi-MAC+1 check, .bt_loaded as the lazy-init guard, .cyw43_ll as
 * the opaque handle cybt threads back into the backplane shims — unused there). */
cyw43_t cyw43_state;

/* ------------------------------------------------------------------ handle */

static inline whd_driver_t bt_whd_driver(void)
{
	whd_interface_t ifp = airoc_wifi_get_whd_interface();

	return ifp ? ifp->whd_driver : NULL;
}

/* --------------------------------------------------------------- bus lock */
/* The cybt up-edge (CYW43_THREAD_ENTER/EXIT -> cyw43_thread_enter/exit) maps to
 * the SHARED gSPI bus lock (whd_bus_lock.h), which WHD's WLAN path also takes
 * (durable airoc_whd_hal_spi patch). Held across each whole cybt
 * read-modify-write sequence, it serializes BT bus access against WHD's
 * whd_thread — the W6 fix for the §2.7 concurrent-bus corruption. */
void cyw43_thread_enter(void)
{
	whd_bus_lock();
}

void cyw43_thread_exit(void)
{
	whd_bus_unlock();
}

/* --------------------------------------------------------------- host delay */

void cyw43_delay_ms(uint32_t ms)
{
	k_msleep((int32_t)ms);
}

/* ----------------------------------------------------- backplane shims (WHD) */

void cyw43_ll_write_backplane_reg(cyw43_ll_t *self_in, uint32_t addr, uint32_t val)
{
	(void)self_in;
	whd_bus_write_backplane_value(bt_whd_driver(), addr, 4, val);
}

uint32_t cyw43_ll_read_backplane_reg(cyw43_ll_t *self_in, uint32_t addr)
{
	uint8_t b[4] = {0};

	(void)self_in;
	whd_bus_read_backplane_value(bt_whd_driver(), addr, 4, b);
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
	       ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

int cyw43_ll_write_backplane_mem(cyw43_ll_t *self_in, uint32_t addr, uint32_t len,
				 const uint8_t *buf)
{
	(void)self_in;
	return (int)whd_bus_transfer_backplane_bytes(bt_whd_driver(), BUS_WRITE, addr, len,
						     (uint8_t *)buf);
}

int cyw43_ll_read_backplane_mem(cyw43_ll_t *self_in, uint32_t addr, uint32_t len, uint8_t *buf)
{
	(void)self_in;
	return (int)whd_bus_transfer_backplane_bytes(bt_whd_driver(), BUS_READ, addr, len, buf);
}

/* -------------------------------------------------------- BT RX poll thread */
/* The georgerobotics single cooperative poll thread (zephyr_cyw43_drv.c) does
 * not exist in WHD mode (WHD runs its own whd_thread for WLAN RX). Drain the
 * controller->host BT ring here. cyw43_bluetooth_hci_process() /
 * cyw43_bluetooth_has_pending() are defined in zephyr_cyw43_bt_hci_drv.c. */
extern void cyw43_bluetooth_hci_process(void);
extern bool cyw43_bluetooth_has_pending(void);

#define BT_POLL_STACK_SIZE 4096
#define BT_POLL_PRIO       K_PRIO_PREEMPT(8)
/* Poll interval: fast enough for HCI command/event round-trips and ~10 notif/s
 * coex, slow enough not to hammer the backplane at idle. (An earlier host-wake-
 * IRQ-driven variant lowered RX latency but did NOT fix the BLE early-disconnect
 * and destabilised the poll thread — latency was ruled out as the cause, so the
 * stable fixed poll is kept while the real issue, the weak BT RF link, is
 * chased separately. See PROGRESS.md.) */
#define BT_POLL_INTERVAL_MS 4

static K_KERNEL_STACK_DEFINE(bt_poll_stack, BT_POLL_STACK_SIZE);
static struct k_thread bt_poll_thread_data;
static bool bt_poll_started;

/* Wake hook for the shared WL_HOST_WAKE ISR. Retained (no-op give is harmless)
 * so the durable airoc patch keeps a stable symbol; the poll uses a fixed
 * interval, so this is currently advisory only. */
void whd_bt_notify_irq(void)
{
}

static void bt_poll_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		cyw43_thread_enter();
		if (cyw43_bluetooth_has_pending()) {
			cyw43_bluetooth_hci_process();
		}
		cyw43_thread_exit();
		k_msleep(BT_POLL_INTERVAL_MS);
	}
}

static void bt_poll_start(void)
{
	if (bt_poll_started) {
		return;
	}
	bt_poll_started = true;
	k_thread_create(&bt_poll_thread_data, bt_poll_stack, BT_POLL_STACK_SIZE,
			bt_poll_thread, NULL, NULL, NULL, BT_POLL_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&bt_poll_thread_data, "whd_bt_poll");
}

/* ---------------------------------------------------- SEAM-1 BT primitives */
/* Lazy bring-up: WHD already powered the chip + brought the backplane up at
 * AIROC init (whd_wifi_on), so "ensure bt up" here just downloads the BT
 * patchram via cybt once, the first time a BT op runs (matches REFERENCE §2.4
 * "BT firmware loads lazily on first BT op"). */
static int whd_bt_ensure_up(void)
{
	whd_interface_t ifp;
	whd_mac_t mac;

	if (cyw43_state.bt_loaded) {
		return 0;
	}

	ifp = airoc_wifi_get_whd_interface();
	if (ifp == NULL || ifp->whd_driver == NULL) {
		LOG_ERR("WHD not up yet; cannot bring BT up");
		return -EIO;
	}

	/* Cache the WiFi MAC so the HCI setup hook can verify BD_ADDR == MAC+1. */
	if (whd_wifi_get_mac_address(ifp, &mac) == WHD_SUCCESS) {
		memcpy(cyw43_state.mac, mac.octet, sizeof(cyw43_state.mac));
	} else {
		LOG_WRN("whd_wifi_get_mac_address failed; BD_ADDR check may not match");
	}

	cyw43_thread_enter();
	int ret = cyw43_btbus_init(&cyw43_state.cyw43_ll);
	cyw43_thread_exit();
	if (ret) {
		LOG_ERR("cyw43_btbus_init (BT patchram) failed: %d", ret);
		return ret;
	}

	cyw43_state.bt_loaded = true;
	bt_poll_start();
	LOG_INF("BT up over WHD bus (patchram loaded), MAC %02x:%02x:%02x:%02x:%02x:%02x",
		cyw43_state.mac[0], cyw43_state.mac[1], cyw43_state.mac[2],
		cyw43_state.mac[3], cyw43_state.mac[4], cyw43_state.mac[5]);
	return 0;
}

int cyw43_bluetooth_hci_init(void)
{
	return whd_bt_ensure_up();
}

int cyw43_bluetooth_hci_read(uint8_t *buf, uint32_t max_size, uint32_t *len)
{
	int ret = whd_bt_ensure_up();

	if (ret) {
		return ret;
	}
	return cyw43_btbus_read(buf, max_size, len);
}

int cyw43_bluetooth_hci_write(uint8_t *buf, size_t len)
{
	int ret = whd_bt_ensure_up();

	if (ret) {
		return ret;
	}
	return cyw43_btbus_write(buf, (uint32_t)len);
}
