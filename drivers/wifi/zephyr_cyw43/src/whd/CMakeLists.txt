# SPDX-License-Identifier: Apache-2.0
#
# Infineon AIROC/WHD transport backend for the CYW43 driver (whd-port).
# WiFi is the upstream AIROC driver; this dir holds the BT-over-WHD glue.

zephyr_library_sources(
  whd_transport.c
)

# BT-over-WHD: whd_bt_glue.c backs the retained cybt_shared_bus transport with
# WHD backplane primitives. It needs WHD's internal headers + airoc_wifi.h.
if(CONFIG_BT)
  zephyr_library_sources(whd_bt_glue.c)

  set(whd_root ${ZEPHYR_HAL_INFINEON_MODULE_DIR}/whd-expansion/WHD)
  zephyr_library_include_directories(
    ${whd_root}/COMPONENT_WIFI5/inc
    ${whd_root}/COMPONENT_WIFI5/src/include
    ${whd_root}/COMPONENT_WIFI5/src/bus_protocols
    ${whd_root}/COMMON/inc
    ${ZEPHYR_BASE}/drivers/wifi/infineon
  )
endif()
