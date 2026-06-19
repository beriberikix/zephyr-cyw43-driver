# Coexistence soak — characterization result (2026-06-19)

Goal: quantify the residual BLE-connection command-timeout failure rate with the
current driver fixes (poll-priority + drain), per the operator's "characterize"
decision.

Setup: soak build (CONFIG_APP_BLE_PERIPHERAL=y, BT_SHELL=n, sys_work_q 2048) on
rpi_pico2/w; host bleak central (hci0) connecting to the Pico notify peripheral;
WiFi reachable both ways on "funrun" (Pico<->host 0% loss, Pico->8.8.8.8 ok).

Observations (test/coex/soak.sh + ble_central.py, multiple cycles):
- WITH WiFi load (Pico->8.8.8.8 ping flood + host->Pico ping): cycle 1 FAULT
  (0 notifications), cycle 2 alive but 0 notifications (no successful subscribe).
- WITHOUT WiFi load, fresh reset, first connection: connect + subscribe ->
  immediate DISCONNECT, Pico DEAD (faulted) on the subscribe.
- One earlier run (right after the drain fix) sustained ~74 s uptime and
  delivered 1 notification before dropping — so it is intermittent, but the
  failure dominates.

CONCLUSION: the live BLE connection + GATT subscribe path faults the controller
FREQUENTLY (the majority of connect+subscribe attempts, typically within
seconds), with or without WiFi load. This is NOT a rare intermittent issue; it
is the gating failure for the soak. The poll-priority + drain fixes reduced but
did not eliminate it. Root (per gdb): at the fault the poll thread is idle and
the bus lock is free -> the command-complete is never surfaced to the host;
i.e. below the driver, in the cybt_shared_bus BT-mailbox / WL_HOST_WAKE
signaling or controller firmware.

The shell-driven coex (advertise + WiFi load, no live GATT connection) remains
rock-solid (item 4 artifact). The soak as specified (connected notify peripheral
under load) cannot pass until the live-connection command-timeout is resolved.

RESULT: SOAK NOT PASSING — characterized as frequent live-connection faults.
