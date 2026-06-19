#!/usr/bin/env python3
"""Host BLE central for the WiFi+BLE coexistence soak.

Connects to the Pico's coex notify peripheral (CONFIG_APP_BLE_PERIPHERAL),
subscribes to the notify characteristic, and records notification throughput,
inter-notification gaps (a stall proxy), and any disconnects for DURATION
seconds. Exits non-zero on connect failure, an unexpected disconnect, or a
notification stall over --max-gap.

Usage:
  ble_central.py [--name test-picow-bluetooth] [--addr 88:A2:9E:D1:6D:A0]
                 [--duration 30] [--max-gap 3.0]
"""
import argparse
import asyncio
import sys
import time

from bleak import BleakClient, BleakScanner

NOTIFY_UUID = "6e400002-c0e0-4001-b000-000000000002"
DEFAULT_NAME = "test-picow-bluetooth"


async def run(args):
    print(f"scanning for '{args.name}' / {args.addr} ...", flush=True)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: (args.addr and d.address.upper() == args.addr.upper())
        or (ad.local_name == args.name),
        timeout=args.scan_timeout,
    )
    if dev is None:
        print("FAIL: peripheral not found in scan", flush=True)
        return 2
    print(f"found {dev.address} ({dev.name}); connecting ...", flush=True)

    state = {"count": 0, "first": None, "last": None, "max_gap": 0.0,
             "last_val": None, "gaps_over": 0, "disconnected": False}

    def on_disconnect(_):
        state["disconnected"] = True
        print("DISCONNECTED", flush=True)

    def on_notify(_char, data: bytearray):
        now = time.monotonic()
        if state["first"] is None:
            state["first"] = now
        if state["last"] is not None:
            gap = now - state["last"]
            state["max_gap"] = max(state["max_gap"], gap)
            if gap > args.max_gap:
                state["gaps_over"] += 1
                print(f"  STALL: gap {gap:.2f}s > {args.max_gap}s", flush=True)
        state["last"] = now
        state["count"] += 1
        if len(data) >= 4:
            state["last_val"] = int.from_bytes(data[:4], "little")

    async with BleakClient(dev, disconnected_callback=on_disconnect) as client:
        print("connected; subscribing ...", flush=True)
        await client.start_notify(NOTIFY_UUID, on_notify)
        end = time.monotonic() + args.duration
        while time.monotonic() < end and not state["disconnected"]:
            await asyncio.sleep(0.5)
        try:
            await client.stop_notify(NOTIFY_UUID)
        except Exception:
            pass

    dur = (state["last"] - state["first"]) if state["first"] and state["last"] else 0
    rate = state["count"] / dur if dur > 0 else 0
    print("=== BLE CENTRAL SUMMARY ===", flush=True)
    print(f"notifications: {state['count']}", flush=True)
    print(f"duration: {dur:.1f}s   rate: {rate:.1f}/s", flush=True)
    print(f"max inter-notify gap: {state['max_gap']:.3f}s", flush=True)
    print(f"stalls over {args.max_gap}s: {state['gaps_over']}", flush=True)
    print(f"last counter value: {state['last_val']}", flush=True)
    print(f"unexpected disconnect: {state['disconnected']}", flush=True)
    ok = (state["count"] > 0 and not state["disconnected"]
          and state["gaps_over"] == 0)
    print("RESULT: " + ("PASS" if ok else "FAIL"), flush=True)
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default=DEFAULT_NAME)
    ap.add_argument("--addr", default="")
    ap.add_argument("--duration", type=float, default=30)
    ap.add_argument("--scan-timeout", type=float, default=15)
    ap.add_argument("--max-gap", type=float, default=3.0)
    args = ap.parse_args()
    try:
        sys.exit(asyncio.run(run(args)))
    except Exception as e:
        print(f"FAIL: {type(e).__name__}: {e}", flush=True)
        sys.exit(3)


if __name__ == "__main__":
    main()
