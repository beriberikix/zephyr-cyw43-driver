#!/usr/bin/env python3
"""Drive the Zephyr shell on the Pico 2 W over the Debug Probe's UART bridge.

Usage:
  console.py send "<shell cmd>" [--wait SECONDS]   # send a command, print reply
  console.py capture [--wait SECONDS]              # just capture whatever arrives

The console device + baud are fixed to the probe wired in PROGRESS.md.
"""
import argparse
import sys
import time

import serial

PORT = ("/dev/serial/by-id/"
        "usb-Raspberry_Pi_Debug_Probe__CMSIS-DAP__E665485457925026-if01")
BAUD = 115200


def drain(ser, seconds):
    out = bytearray()
    end = time.time() + seconds
    while time.time() < end:
        chunk = ser.read(512)
        if chunk:
            out += chunk
        else:
            time.sleep(0.02)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("send")
    s.add_argument("text")
    s.add_argument("--wait", type=float, default=2.0)
    c = sub.add_parser("capture")
    c.add_argument("--wait", type=float, default=3.0)
    args = ap.parse_args()

    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    try:
        if args.cmd == "capture":
            sys.stdout.write(drain(ser, args.wait).decode("utf-8", "replace"))
            return
        # send: clear, nudge a prompt, then issue the command
        ser.reset_input_buffer()
        ser.write(b"\r\n")
        ser.flush()
        time.sleep(0.2)
        drain(ser, 0.3)
        ser.write(args.text.encode() + b"\r\n")
        ser.flush()
        reply = drain(ser, args.wait)
        sys.stdout.write(reply.decode("utf-8", "replace"))
    finally:
        ser.close()


if __name__ == "__main__":
    main()
