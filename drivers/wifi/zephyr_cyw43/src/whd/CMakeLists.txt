# SPDX-License-Identifier: Apache-2.0
#
# Infineon AIROC/WHD transport backend for the CYW43 driver (whd-port).
# W0 wires the build switch; the WHD WiFi glue and the SEAM-1 BT primitive
# shims are added incrementally from W1 onward.

zephyr_library_sources(
  whd_transport.c
)
