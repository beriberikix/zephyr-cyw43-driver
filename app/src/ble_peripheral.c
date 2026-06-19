/*
 * Copyright (c) 2026 Beechwoods Software
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal connectable BLE peripheral with a notify characteristic, for the
 * WiFi+BLE coexistence soak (test/coex). Self-starts at boot when
 * CONFIG_APP_BLE_PERIPHERAL=y: enables BT, registers a vendor GATT service with
 * one notify characteristic, advertises (legacy; this controller has no
 * extended advertising), and once a central subscribes streams a monotonically
 * increasing 32-bit counter every CONFIG_APP_BLE_NOTIFY_INTERVAL_MS so the host
 * can measure notification latency / detect stalls and disconnects.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_peripheral, LOG_LEVEL_INF);

/* Vendor service / characteristic UUIDs (random 128-bit). */
static struct bt_uuid_128 coex_svc_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x6e400001, 0xc0e0, 0x4001, 0xb000, 0x000000000001));
static struct bt_uuid_128 coex_notify_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x6e400002, 0xc0e0, 0x4001, 0xb000, 0x000000000002));

static volatile bool notify_enabled;
static uint32_t notify_counter;

static void coex_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("notifications %s", notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(coex_svc,
	BT_GATT_PRIMARY_SERVICE(&coex_svc_uuid),
	BT_GATT_CHARACTERISTIC(&coex_notify_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(coex_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_WRN("connection failed (err 0x%02x)", err);
	} else {
		LOG_INF("connected");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("disconnected (reason 0x%02x)", reason);
	notify_enabled = false;
	/* Legacy advertising; restart so the central can reconnect. */
	bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void notify_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		k_msleep(CONFIG_APP_BLE_NOTIFY_INTERVAL_MS);
		if (!notify_enabled) {
			continue;
		}
		uint32_t v = sys_cpu_to_le32(notify_counter);

		/* attrs[2] is the characteristic value (svc, chrc-decl, value, ccc). */
		int rv = bt_gatt_notify(NULL, &coex_svc.attrs[2], &v, sizeof(v));

		if (rv == 0) {
			notify_counter++;
		} else if (rv != -ENOTCONN) {
			LOG_WRN("bt_gatt_notify rv %d", rv);
		}
	}
}

K_THREAD_DEFINE(coex_notify_tid, 1024, notify_thread, NULL, NULL, NULL,
		7, 0, -1);

void app_ble_peripheral_start(void)
{
	int err = bt_enable(NULL);

	if (err) {
		LOG_ERR("bt_enable failed (err %d)", err);
		return;
	}
	LOG_INF("Bluetooth initialized; starting advertising as \"%s\"",
		CONFIG_BT_DEVICE_NAME);

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("advertising start failed (err %d)", err);
		return;
	}

	k_thread_start(coex_notify_tid);
}
