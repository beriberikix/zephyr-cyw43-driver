# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WIFI_ZEPHYR_CYW43)
  zephyr_library_include_directories(src/ src/cyw43-driver/src/ src/cyw43-driver/firmware/)

  # Transport backend: georgerobotics cyw43_ll (proven default) vs WHD port.
  #
  # The Zephyr glue (zephyr_cyw43_drv.c + aux drivers) calls georgerobotics
  # cyw43_* symbols directly, so it builds only under that backend. The
  # shared-arbitration-core extraction (so WHD reuses the poll thread + bus
  # lock) is future work; for now the WHD backend provides its own glue under
  # src/whd (WiFi via the upstream AIROC driver, BT via whd_bt_glue.c).
  if(CONFIG_CYW43_TRANSPORT_GEORGEROBOTICS)
    zephyr_library_sources(
      src/zephyr_cyw43_drv.c
      src/zephyr_cyw43_led_drv.c
      src/zephyr_cyw43_gpio_drv.c
      src/zephyr_cyw43_shell.c
      src/cyw43-driver/src/cyw43_ll.c
      src/cyw43-driver/src/cyw43_ctrl.c
    )

    # gSPI bus backend for the georgerobotics stack. It binds the
    # infineon,cyw43 DT node (absent in WHD mode, where the upstream AIROC
    # driver owns the PIO-SPI), so it builds only under this backend.
    if(CONFIG_USE_PICOWI_PIOSPI)
      zephyr_library_sources(
        src/cyw43_bus_picowi_pio_spi.c
        src/picowi_pio/picowi_pio.c
        src/picowi_pio/picowi_pico.c
      )
    else()
      zephyr_library_sources(
        src/cyw43_bus_pio_spi.c
      )
    endif()
  elseif(CONFIG_CYW43_TRANSPORT_WHD)
    # WHD WiFi is provided by the upstream AIROC driver (CONFIG_WIFI_AIROC)
    # over the board's stock infineon,airoc-wifi node. src/whd holds the
    # SEAM-1 BT glue (whd_bt_glue.c) that runs cybt over the WHD bus.
    add_subdirectory(src/whd)
  endif()

  # BT HCI transport over the shared gSPI bus — common to BOTH backends.
  # The HCI driver (zephyr_cyw43_bt_hci_drv.c) + pico-sdk cybt_shared_bus are
  # transport-agnostic: their lower edge (cyw43_bluetooth_hci_* + the backplane
  # primitives + the bus lock) is supplied by cyw43_ctrl.c/cyw43_ll.c under the
  # georgerobotics backend, or by src/whd/whd_bt_glue.c under the WHD backend.
  if(CONFIG_BT)
    set(rp2_shared_bus_dir ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/pico_cyw43_driver/cybt_shared_bus)
    zephyr_library_include_directories(${rp2_shared_bus_dir}/)
    zephyr_library_sources(src/zephyr_cyw43_bt_hci_drv.c
      ${rp2_shared_bus_dir}/cybt_shared_bus.c
      ${rp2_shared_bus_dir}/cybt_shared_bus_driver.c
    )
  endif()

endif()
