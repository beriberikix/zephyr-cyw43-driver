/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Infineon AIROC/WHD transport backend for the CYW43 WiFi+BLE coexistence
 * driver (whd-port branch).
 *
 * W0 establishes the build-time transport switch (CYW43_TRANSPORT_WHD) and
 * this compilation unit so the WHD branch is wired into CMake/Kconfig while
 * the proven georgerobotics backend remains the default.
 *
 * The WHD WiFi bring-up (whd_init / whd_bus_spi_attach / whd_wifi_on /
 * whd_wifi_scan) and the SEAM-1 BT primitive shims (see docs/whd-contract.md
 * §2.2) are filled in from W1 onward.
 */

#include <zephyr/kernel.h>

/*
 * Placeholder translation unit. Keeps the WHD library non-empty so the
 * CYW43_TRANSPORT_WHD build links cleanly before the real glue exists.
 * Replaced by the WHD WiFi driver registration in W1.
 */
