# Task: Harden the Beechwoods cyw43 BT HCI driver into a rock-solid WiFi+BLE coexistence reference

## Mission
Make the Bluetooth HCI driver in this repo (Georgerobotics cyw43_ll stack) rock-solid for
simultaneous WiFi (STA) + BLE on the Raspberry Pi Pico 2 W (CYW43439 over the shared 3-pin gSPI
bus). This driver is intended to become the REFERENCE implementation that a future Infineon
WHD/AIROC shared-bus BT transport is ported from. Correctness, robustness, and a clearly
documented transport/arbitration contract matter more than features.

## Hard scope rules
- IN scope: STA + BLE (Central and Peripheral) coexistence on rpi_pico2/rp2350a/m33/w.
- OUT of scope (do NOT fix): AP+STA concurrency and AP+STA+BLE. These are upstream
  CYW43-firmware/single-radio limitations (pico-sdk #2835, cyw43-driver #111), not driver bugs.
  Don't regress them; just note behavior.
- Do NOT touch WHD/AIROC. This is only the cyw43_ll-based stack. WHD is a separate future effort
  that will consume the contract documented here.
- Keep WiFi-only and BT-only builds working at all times.

## Environment & hardware (verification is hardware-in-the-loop — no simulated results)
- Repo: this fork. Work on branch `harden-coex`. Pin the working Zephyr SHA in PROGRESS.md.
- Zephyr: current 4.x (4.4.x) with in-tree RP2xxx native PIO-SPI.
- Board: rpi_pico2/rp2350a/m33/w.
- Hardware: Pico 2 W attached via a Raspberry Pi Debug Probe (SWD + UART bridge),
  VID:PID 2e8a:000c. See M0.0 for the flash/console recipe.
- Flashing: RPi openocd fork preferred (~/.pico-sdk/openocd/0.12.0+dev, has rp2350.cfg +
  interface/cmsis-dap.cfg); probe-rs (--chip RP235x) is a known-working fallback. ONE tool may
  own the probe at a time.
- Console: UART via /dev/serial/by-id (scriptable with pyserial). RTT optional (requires adding
  the `segger` module to west.yml's name-allowlist + west update).
- WiFi test AP: SSID/PSK in app/local.conf (not committed).
- Host-side BLE central: a bleak Python script you write under test/coex/.

## Milestone 0 — Bring-up gate

### M0.0 — Flash & console bring-up (FIRST)
Goal: a documented, repeatable, single-command flash, plus a console the loop can DRIVE
programmatically (send a shell command, parse the reply). Record exact commands in PROGRESS.md.

Constraints / known findings:
- Only ONE tool may own the CMSIS-DAP probe at a time. Before any flash/debug, kill stale holders
  (`pkill -f probe-rs; pkill openocd; pkill -f gdb`). "could not claim interface: Resource busy" /
  USB I/O errors == another process holds the probe, NOT a permissions problem.
- Prefer the RPi openocd fork:
  `~/.pico-sdk/openocd/0.12.0+dev/openocd -s ~/.pico-sdk/openocd/0.12.0+dev/scripts
   -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000"
   -c "program build/zephyr/zephyr.elf verify reset exit"`.
  If it complains about cores, add `-c "set USE_CORE 0"`. probe-rs `--chip RP235x` is the fallback.
- Verify the image actually RUNS before chasing console: openocd+gdb, `monitor reset run`,
  `continue`, Ctrl-C, `bt` — Zephyr frames == alive; bootrom/fault == boot problem to fix first.
- Console backend for the loop = UART (scriptable via pyserial on /dev/serial/by-id). hello_world's
  banner is one-shot — DO NOT verify via boot banner. The driver app is a SHELL app that stays up;
  verify console by sending e.g. `kernel version` and parsing the reply.
- RTT is silent in this workspace because `segger` is not in west.yml's name-allowlist, so
  CONFIG_USE_SEGGER_RTT doesn't exist and is ignored. If you want RTT (single-cable, good for fault
  capture), add `segger` to the allowlist and `west update`; otherwise use UART.

Hardware you CANNOT do yourself — escalate (BLOCKED -> ask) with exact instructions:
- If UART is chosen and unwired: tell the operator to connect the probe's "U" connector to Pico
  UART0 — probe TX -> GP1, probe RX -> GP0, GND <-> GND — then resume.
- If a flash path needs BOOTSEL/UF2: give the operator the exact button/plug sequence.

Done when: one documented command flashes reliably from clean; the app boots (gdb-confirmed); the
loop can issue a shell command over the recorded console and read the response; flash command,
console device, and any wiring are written into PROGRESS.md.

### M0.1 — Driver app builds & boots on Pico 2 W
Clean build+flash+boot on rpi_pico2/rp2350a/m33/w with CONFIG_WIFI=y AND CONFIG_BT=y in one image:
shell reachable, `wifi connect` associates to the test AP, AND `bt init` + advertising succeed.
Note: the repo ships overlays only for rpi_pico/rp2040/w + esp32 and has drifted from Zephyr 4.4
(the infineon,cyw43-gpio binding is now upstream and was removed in setup). So M0.1 = rebase the
module onto current 4.4 + add the rpi_pico2 board overlay + reconcile the cyw43 GPIO driver to
upstream's binding property schema. Capture real console output proving it.

## Hardening backlog (each: fix + hardware-verified acceptance criterion)
Driver: drivers/wifi/zephyr_cyw43/src/zephyr_cyw43_bt_hci_drv.c (+ binding
dts/bindings/bluetooth/infineon,cyw43-bt-hci.yaml).

1. HCI setup / BD_ADDR. `zephyr_cyw43_bt_hci_setup()` is a no-op. Implement CONFIG_BT_HCI_SETUP:
   known controller init + stable correct public BD_ADDR (CYW43 derives BT addr = WiFi MAC+1;
   verify against what the controller reports).
   DONE: `bt id-show` reports a stable correct public address across 10 cold boots, logged.
2. SCO/ISO separation. BT_HCI_H4_SCO is conflated with the ISO path (wrong buf type/header). Route
   SCO correctly or cleanly gate it out and document.
   DONE: ISO correct, SCO no longer mis-routes; build + BLE-with-ISO smoke test pass, logged.
3. RX robustness. RX ignores `cyw43_bluetooth_hci_read` returns and can deref NULL from
   bt_buf_get_evt/bt_buf_get_rx under exhaustion. Add return checks, a drop/backpressure policy on
   alloc failure, length bounds checks.
   DONE: buffer-pressure stress (flood events/notifications) runs with zero faults, logged.
4. Threading / bus-arbitration audit. Validate the cooperative poll thread (priority, stack) vs the
   BT host TX path and WiFi RX; confirm the lock discipline upholds the single-bus invariant under
   concurrent load; rule out priority inversion / stack overflow. Fix what's needed.
   DONE: documented analysis + the coexistence soak passes with no stalls.
5. Shared power line (WL_REG_ON/BT_REG_ON tie). Define + verify behavior including
   BT-without-WiFi and re-init ordering.
   DONE: BT-only, WiFi-then-BT, BT-then-WiFi init orders all come up clean, logged.
6. Firmware blob pinning. Pin the combined wb43439 blob; record version/provenance/license. Do not
   switch to EULA blobs.
   DONE: blob SHA + version + license recorded in REFERENCE.md.

## Coexistence soak harness (the rock-solid gate)
Scripts in test/coex/. Associate STA to the test AP; start a BLE peripheral advertising a notify
characteristic; host bleak central connects and subscribes; run a concurrent WiFi load generator
(Zephyr zperf or host iperf3/ping flood against the STA IP) BOTH directions. Run continuously for
SOAK_HOURS (default 2) across COLD_BOOTS (default 5) power cycles.
PASS gate (all): zero CYW43 controller lockups, zero Zephyr faults/asserts, zero unexpected BLE
disconnects, WiFi throughput within threshold of baseline, BLE notification latency under bound.
Archive raw logs under test/coex/results/.

## Deliverable for the WHD port
Maintain REFERENCE.md documenting the transport contract a WHD impl must mirror: HCI-over-gSPI
framing (4-byte header, H4 indicators), the read/write/has_work/ensure_up primitives, the
single-lock poll-loop arbitration model, init/power ordering, BD_ADDR derivation. Keep the BT
driver's dependency on a thin transport interface, not on WiFi-driver internals.

## Loop protocol
Maintain PROGRESS.md as durable state: working flash command, Zephyr SHA, per-item status
(TODO / IN-PROGRESS / VERIFIED with a link to the proving log artifact), and a changelog.

One iteration =
1. Read PROGRESS.md. Work M0.0, then M0.1, then the highest-priority unverified backlog item.
2. Implement the smallest correct change. One concern per commit; pass checkpatch.
3. Before any flash/debug: kill stale probe holders (pkill -f probe-rs; pkill openocd; pkill -f gdb).
   Build -> flash -> run the item's acceptance test on the real Pico 2 W. Save actual console/SWD
   output to an artifact. No item is VERIFIED without a real hardware log — never fabricate a pass.
4. If it fails, diagnose (host vs controller via gdb), fix, repeat 3. Don't move on.
5. Update PROGRESS.md (status + artifact link + changelog) and commit.
6. Re-run the coexistence soak if you changed RX/TX/poll/arbitration paths.

Stop condition (DONE): M0.0 + M0.1 + items 1-6 + SOAK + REF all VERIFIED; WiFi-only and BT-only
builds still pass; checkpatch clean; REFERENCE.md complete. Then stop and produce a final summary
with links to soak artifacts. If BLOCKED (hardware unreachable, missing creds, needs a wire moved,
or a failure unresolved after 3 attempts), checkpoint and ask a specific question rather than guess.