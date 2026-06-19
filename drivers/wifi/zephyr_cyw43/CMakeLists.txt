# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WIFI_ZEPHYR_CYW43)
  zephyr_library_include_directories(src/ src/cyw43-driver/src/ src/cyw43-driver/firmware/)

  # Transport backend: georgerobotics cyw43_ll (proven default) vs WHD port.
  #
  # The Zephyr glue (zephyr_cyw43_drv.c + aux drivers) currently calls
  # georgerobotics cyw43_* symbols directly, so it builds only under that
  # backend. The shared-arbitration-core extraction (so WHD reuses the poll
  # thread + bus lock) lands with BT arbitration in W3; until then the WHD
  # backend provides its own glue under src/whd.
  if(CONFIG_CYW43_TRANSPORT_GEORGEROBOTICS)
    zephyr_library_sources(
      src/zephyr_cyw43_drv.c
      src/zephyr_cyw43_led_drv.c
      src/zephyr_cyw43_gpio_drv.c
      src/zephyr_cyw43_shell.c
      src/cyw43-driver/src/cyw43_ll.c
      src/cyw43-driver/src/cyw43_ctrl.c
    )
  elseif(CONFIG_CYW43_TRANSPORT_WHD)
    # WHD WiFi glue + SEAM-1 BT primitive shims land here (W1+).
    add_subdirectory(src/whd)
  endif()

  # The default now is to use Zephyr's PIO-SPI (the 2nd one)  
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

  # BT HCI transport over the shared gSPI bus. Only the georgerobotics backend
  # provides the cyw43_bluetooth_hci_* primitives today; BT-over-WHD (the cybt
  # transport re-arbitrated onto the WHD lock) lands in W3. Until then the WHD
  # backend builds WiFi-only even when CONFIG_BT is set.
  if(CONFIG_CYW43_TRANSPORT_GEORGEROBOTICS)
    set(rp2_shared_bus_dir ${ZEPHYR_HAL_RPI_PICO_MODULE_DIR}/src/rp2_common/pico_cyw43_driver/cybt_shared_bus)
    zephyr_library_include_directories_ifdef(CONFIG_BT ${rp2_shared_bus_dir}/)
    zephyr_library_sources_ifdef(CONFIG_BT src/zephyr_cyw43_bt_hci_drv.c
      ${rp2_shared_bus_dir}/cybt_shared_bus.c
      ${rp2_shared_bus_dir}/cybt_shared_bus_driver.c
    )
  endif()

endif()
