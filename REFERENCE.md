# CYW43 WiFi+BLE Coexistence — Reference / Transport Contract

Reference documentation for the Beechwoods cyw43_ll-based WiFi+BLE coexistence
driver on the Raspberry Pi Pico 2 W (CYW43439 over the shared 3-pin gSPI bus).
A future Infineon WHD/AIROC shared-bus BT transport should mirror the contracts
documented here.

Pinned environment (see PROGRESS.md for the live loop state):
- Zephyr: `44af4534166bad6cec2f5a6eb226021260b9c49d` (v4.4.0-5745), in-tree RP2xxx PIO-SPI.
- Board: `rpi_pico2/rp2350a/m33/w`.
- georgerobotics cyw43-driver (vendored at
  `drivers/wifi/zephyr_cyw43/src/cyw43-driver`): **v1.0.4**
  (`1d2227bca200e13c1d6a630032d5f6d7fca69fef`).

---

## 1. Firmware blob pinning (backlog item 6)

This driver uses the **combined, RP-licensed (non-EULA) WiFi+BT blob** shipped in
the georgerobotics cyw43-driver. It is selected in
`drivers/wifi/zephyr_cyw43/src/cyw43_configport.h` via
`CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE` whenever `CONFIG_BT` is set:

```c
#ifdef CONFIG_BT
#define CYW43_ENABLE_BLUETOOTH (1)
#endif
#if CYW43_ENABLE_BLUETOOTH
#define CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE "wb43439A0_7_95_49_00_combined.h"
#else
#define CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE "w43439A0_7_95_49_00_combined.h"
#endif
```

| Item | Value |
|------|-------|
| Combined WiFi+BT blob (BT builds) | `firmware/wb43439A0_7_95_49_00_combined.h` |
| SHA-256 (header file) | `6b4b9a717c3c5d669b56a3bc38b58df3901f63e3a7363ccf5d74706460762176` |
| WiFi-only blob (non-BT builds) | `firmware/w43439A0_7_95_49_00_combined.h` |
| WiFi-only SHA-256 | `120bb63db1c5cf42cd44cf9a057d6f14601038a9413badf5a3b737f6552bc9ea` |
| Provenance | georgerobotics/cyw43-driver v1.0.4, dir `firmware/` |
| Layout | WiFi firmware padded to 512 B + CLM blob, then the BT firmware patch records appended (the "wb" = WiFi+Bluetooth combined image) |

Runtime-reported versions (from the boot log, `docs/artifacts/m0.0_console_20260619.log`):
- WiFi firmware: `7.95.61 (abcd531 CY)`, CRC `4528a809`, date `2023-01-11`,
  Ucode `1043.2169`, FWID `01-7afb0879`, API `12.2`, CLM import `1.47.1`,
  customization `v5 22/06/24`.
- BT firmware: `CYW4343A2_001.003.016.0031.0000_Generic_SDIO_37MHz_wlbga_BU_dl_signed`.

### License

The firmware blobs are licensed under the **George Robotics / Raspberry Pi
"RP" license** (`drivers/wifi/zephyr_cyw43/src/cyw43-driver/LICENSE.RP`):

> Copyright (C) 2019-2022 George Robotics Pty Ltd. Raspberry Pi Ltd (Licensor)
> hereby grants … a non-exclusive license to use this software solely with the
> Licensor's microcontroller chip (RP2040) or any other semiconductor device
> produced by the Licensor.

The RP2350 (rp_pico2/w) is a Raspberry-Pi-produced device, so this use is within
the license grant. Redistribution in source and binary form is permitted with
the copyright notice retained (conditions 1–3 of LICENSE.RP).

**Do NOT switch to the Infineon EULA blobs.** This reference is intentionally
pinned to the RP-licensed combined blob; the Infineon AIROC/WHD EULA firmware is
out of scope and has incompatible redistribution terms.

### Pinning policy

The blob is pinned transitively by the cyw43-driver submodule SHA (v1.0.4 above).
To change the firmware, bump the submodule, recompute the SHA-256 in this table,
re-run the cold-boot BD_ADDR test and the coexistence soak, and update the
runtime-reported versions.

---

## 2. Transport / arbitration contract (for the WHD port)

> TODO (backlog item REF): document the HCI-over-gSPI framing (4-byte header,
> H4 indicators), the read/write/has_work/ensure_up primitives, the single-lock
> poll-loop arbitration model, init/power ordering, and BD_ADDR derivation. The
> implementation facts are captured in PROGRESS.md (items 1–5) and will be
> consolidated here.
