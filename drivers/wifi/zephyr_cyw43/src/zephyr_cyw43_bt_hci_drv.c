/**
 * Copyright (c) 2025 Beechwoods Software, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT infineon_cyw43_bt_hci

#include <zephyr/device.h>
#include <zephyr/drivers/bluetooth.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "cyw43.h"
#include "cybt_shared_bus_driver.h"

#define LOG_LEVEL CONFIG_BT_HCI_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_driver);

/* Offset of special item */
#define PACKET_TYPE             0
#define PACKET_TYPE_SIZE        1
#define EVT_HEADER_EVENT	1
#define EVT_HEADER_SIZE		2
#define EVT_LE_META_SUBEVENT	3
#define EVT_VENDOR_CODE_LSB	3
#define EVT_VENDOR_CODE_MSB	4

#define MAX_BT_MSG_SIZE 2048

// cyw43_bluetooth_hci_write and cyw43_bluetooth_hci_read require a custom 4-byte packet header in front of the actual HCI packet
// the HCI packet type is stored in the fourth byte of the packet header
#define CYW43_PACKET_HEADER_SIZE 4
static uint8_t __noinit cyw43_rxbuf[MAX_BT_MSG_SIZE + CYW43_PACKET_HEADER_SIZE];
static uint8_t __noinit cyw43_txbuf[CONFIG_NET_BUF_DATA_SIZE + CYW43_PACKET_HEADER_SIZE];

struct zephyr_cyw43_bt_hci_data {
	/* Must be first: the host stores its recv callback here via bt_hci_open(). */
	struct bt_hci_driver_data common;
};


static int zephyr_cyw43_bt_hci_init(const struct device *dev)
{
	int rv;
	LOG_DBG("zephyr_cyw43_bt_hci_init() calling cyw43_bluetooth_hci_init()");
	rv = cyw43_bluetooth_hci_init();
	LOG_DBG("cyw43_bluetooth_hci_init() rv = %d", rv);
        return rv;
}

static int zephyr_cyw43_bt_hci_open(const struct device *dev)
{
	/*
	 * The current Zephyr HCI model stores the host recv callback in
	 * dev->data (struct bt_hci_driver_data) before calling open(); RX is
	 * delivered with bt_hci_recv(). The controller transport is already
	 * brought up in zephyr_cyw43_bt_hci_init(), so there is nothing more to
	 * do here.
	 */
	ARG_UNUSED(dev);

	return 0;
}

static int zephyr_cyw43_bt_hci_close(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

void cyw43_bluetooth_hci_process(void) {
	
	struct net_buf *buf=NULL;
	bool discardable = false;
	k_timeout_t timeout = K_FOREVER;
	struct bt_hci_acl_hdr acl_hdr = { .len = 0 };
	uint32_t cyw43_len;
	uint32_t len;
	const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(0));
	uint8_t packet_type;
	uint8_t *rxmsg;
	
        LOG_DBG("Entering cyw43_bluetooth_hci_process()");
	
	cyw43_bluetooth_hci_read(&cyw43_rxbuf[0], MAX_BT_MSG_SIZE, &cyw43_len);

	rxmsg = &cyw43_rxbuf[CYW43_PACKET_HEADER_SIZE - 1];
	packet_type = rxmsg[PACKET_TYPE];
	len = cyw43_len - (CYW43_PACKET_HEADER_SIZE - 1);
		
	LOG_HEXDUMP_DBG(rxmsg, len, "HCI RX data:");
	LOG_DBG("cyw43_bluetooth_hci_process(), len = %d", len);
	LOG_DBG("cyw43_bluetooth_hci_process(): packet_type = %d", packet_type);
	
	switch (packet_type) {
	case BT_HCI_H4_EVT:
		if (rxmsg[EVT_HEADER_EVENT] == BT_HCI_EVT_LE_META_EVENT &&
		    (rxmsg[EVT_LE_META_SUBEVENT] == BT_HCI_EVT_LE_ADVERTISING_REPORT)) {
			discardable = true;
			timeout = K_NO_WAIT;
		}
		buf = bt_buf_get_evt(rxmsg[EVT_HEADER_EVENT],
				     discardable, timeout);
		len = sizeof(struct bt_hci_evt_hdr) + rxmsg[EVT_HEADER_SIZE];
		LOG_DBG("EVT len = %d", len);
		break;
	case BT_HCI_H4_ACL:
		buf = bt_buf_get_rx(BT_BUF_ACL_IN, timeout);
		memcpy(&acl_hdr, &rxmsg[1], sizeof(acl_hdr));
		len = sizeof(struct bt_hci_acl_hdr) + sys_le16_to_cpu(acl_hdr.len);
		LOG_DBG("ACL len = %d", len);
		if (buf != NULL && len > net_buf_tailroom(buf)) {
			LOG_ERR("ACL too long: %d", len);
			net_buf_unref(buf);
			return;
		}

		break;
#if defined(CONFIG_BT_ISO)
	case BT_HCI_H4_ISO: {
		struct bt_hci_iso_hdr iso_hdr;

		buf = bt_buf_get_rx(BT_BUF_ISO_IN, timeout);
		memcpy(&iso_hdr, &rxmsg[1], sizeof(iso_hdr));
		len = sizeof(struct bt_hci_iso_hdr) +
		      bt_iso_hdr_len(sys_le16_to_cpu(iso_hdr.len));
		LOG_DBG("ISO len = %d", len);
		break;
	}
#endif /* CONFIG_BT_ISO */
	case BT_HCI_H4_SCO:
		/*
		 * Classic SCO (synchronous audio) has a different header and
		 * buffer type than ISO and is out of scope for WiFi+BLE
		 * coexistence; the CYW43 BLE path never delivers it. Drop it
		 * here rather than mis-parsing it as ISO (the previous code
		 * conflated the two, using BT_BUF_ISO_IN and bt_hci_iso_hdr for
		 * SCO).
		 */
		LOG_WRN("dropping unsupported SCO packet (cyw43_len %u)", cyw43_len);
		buf = NULL;
		len = 0;
		break;
	default:
		buf = NULL;
		len = 0;
		break;
	}

	if (len == 0) {
		LOG_WRN("Unknown BT buf type %d", rxmsg[PACKET_TYPE]);
	}
	else {
		net_buf_add_mem(buf, &rxmsg[1], len);
		bt_hci_recv(dev, buf);
	}
	LOG_DBG("Leaving cyw43_bluetooth_hci_process()\n");
	
	return;
}

static int zephyr_cyw43_bt_hci_send(const struct device *dev, struct net_buf *buf)
{
	int rv = 0;
	uint8_t packet_type;
	uint32_t cyw43_len = 0;

	ARG_UNUSED(dev);

	/*
	 * In the current Zephyr HCI model the host hands us a buffer whose first
	 * byte is the H:4 packet-type indicator (BT_HCI_H4_CMD/ACL/ISO). The
	 * CYW43 shared-bus write wants that same indicator in the 4th byte of
	 * its 4-byte header, immediately followed by the rest of the buffer, so
	 * we can copy buf->data verbatim starting at the indicator slot.
	 */
	if (buf->len < 1) {
		LOG_ERR("zero-length HCI TX buffer");
		rv = -EINVAL;
		goto out;
	}

	packet_type = buf->data[0];

	switch (packet_type) {
	case BT_HCI_H4_CMD:
	case BT_HCI_H4_ACL:
	case BT_HCI_H4_ISO:
		break;
	default:
		LOG_ERR("Unknown H4 TX packet type 0x%02x", packet_type);
		rv = -EINVAL;
		goto out;
	}

	if (buf->len + (CYW43_PACKET_HEADER_SIZE - 1) > sizeof(cyw43_txbuf)) {
		LOG_ERR("HCI TX buffer too long: %u", buf->len);
		rv = -EMSGSIZE;
		goto out;
	}

	memcpy(&cyw43_txbuf[CYW43_PACKET_HEADER_SIZE - 1], buf->data, buf->len);
	cyw43_len = buf->len + CYW43_PACKET_HEADER_SIZE;

	LOG_DBG("Calling cyw43_bluetooth_hci_write() type=0x%02x", packet_type);
	rv = cyw43_bluetooth_hci_write(cyw43_txbuf, cyw43_len);
	LOG_DBG("cyw43_bluetooth_hci_write() rv=%d", rv);
	LOG_HEXDUMP_DBG(buf->data, buf->len, "HCI TX data:");
	LOG_DBG("zephyr_cyw43_bt_hci_send(), len = %d\n", buf->len);

out:
	net_buf_unref(buf);
	return rv;
}

#if defined(CONFIG_BT_HCI_SETUP)
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/addr.h>

/*
 * Derive the expected BT public address from the WiFi MAC.
 *
 * The CYW43 controller derives its BT BD_ADDR from the WiFi MAC + 1 (the WiFi
 * MAC is read from OTP at cyw43 init and cached in cyw43_state.mac). The MAC is
 * a 48-bit big-endian value (mac[0] is the most-significant octet), so "+1"
 * increments from the last octet with carry. bt_addr_t stores the address
 * little-endian (val[0] is the least-significant octet).
 */
static void cyw43_expected_bt_addr(bt_addr_t *out)
{
	uint8_t mac[6];
	int i;

	memcpy(mac, cyw43_state.mac, sizeof(mac));

	for (i = 5; i >= 0; i--) {
		if (++mac[i] != 0U) {
			break;
		}
	}

	for (i = 0; i < 6; i++) {
		out->val[i] = mac[5 - i];
	}
}

/*
 * HCI vendor/controller setup hook (runs during bt_enable, after HCI Reset).
 *
 * The CYW43 BT firmware is already downloaded by the shared-bus transport in
 * zephyr_cyw43_bt_hci_init(), and the controller exposes a valid OTP-derived
 * public address, so there is no vendor command we must issue to make the
 * controller usable. What we DO here is a "known controller init" sanity check:
 * read the controller's BD_ADDR and verify it is the expected WiFi-MAC+1 public
 * address. A zero/broadcast or mismatched address is surfaced loudly rather
 * than silently shipping a wrong identity. This is intentionally read-only to
 * avoid perturbing a controller that already reports the correct address.
 */
static int zephyr_cyw43_bt_hci_setup(const struct device *dev,
				  const struct bt_hci_setup_params *param)
{
	struct bt_hci_rp_read_bd_addr *rp;
	struct net_buf *rsp = NULL;
	bt_addr_t expected;
	char got_s[BT_ADDR_STR_LEN];
	char exp_s[BT_ADDR_STR_LEN];
	int err;

	ARG_UNUSED(dev);
	ARG_UNUSED(param);

	err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_BD_ADDR, NULL, &rsp);
	if (err) {
		LOG_ERR("HCI Read_BD_ADDR failed (err %d)", err);
		return err;
	}

	rp = (void *)rsp->data;
	if (rp->status) {
		LOG_ERR("HCI Read_BD_ADDR status 0x%02x", rp->status);
		net_buf_unref(rsp);
		return -EIO;
	}

	cyw43_expected_bt_addr(&expected);
	bt_addr_to_str(&rp->bdaddr, got_s, sizeof(got_s));
	bt_addr_to_str(&expected, exp_s, sizeof(exp_s));

	if (bt_addr_eq(&rp->bdaddr, BT_ADDR_ANY) ||
	    bt_addr_eq(&rp->bdaddr, BT_ADDR_NONE)) {
		LOG_ERR("controller reported invalid public BD_ADDR %s", got_s);
		net_buf_unref(rsp);
		return -EIO;
	}

	if (!bt_addr_eq(&rp->bdaddr, &expected)) {
		LOG_WRN("controller public BD_ADDR %s != expected WiFi-MAC+1 %s",
			got_s, exp_s);
	} else {
		LOG_INF("controller public BD_ADDR %s verified (= WiFi MAC + 1)",
			got_s);
	}

	net_buf_unref(rsp);
	return 0;
}

#endif /* defined(CONFIG_BT_HCI_SETUP) */


static DEVICE_API(bt_hci, zephyr_cyw43_bt_hci_api) = {
        .open = zephyr_cyw43_bt_hci_open,
        .close = zephyr_cyw43_bt_hci_close,
	.send = zephyr_cyw43_bt_hci_send,
#if defined(CONFIG_BT_HCI_SETUP)	
	.setup = zephyr_cyw43_bt_hci_setup,
#endif /* defined(CONFIG_BT_HCI_SETUP) */	
};

#define HCI_DEVICE_INIT(inst) \
	static struct zephyr_cyw43_bt_hci_data zephyr_cyw43_bt_hci_data_##inst = { \
	}; \
	DEVICE_DT_INST_DEFINE(inst, zephyr_cyw43_bt_hci_init, NULL, &zephyr_cyw43_bt_hci_data_##inst, NULL, \
                              POST_KERNEL, CONFIG_WIFI_INIT_PRIORITY, &zephyr_cyw43_bt_hci_api)

HCI_DEVICE_INIT(0)
