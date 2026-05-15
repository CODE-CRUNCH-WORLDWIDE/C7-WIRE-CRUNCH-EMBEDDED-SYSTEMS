#!/usr/bin/env python3
"""
cc-flash.py - Host-side OTA tool for the Week 10 mini-project.

Reads a .ccf (Code Crunch Firmware v1) file produced by cc-sign.py and uploads
it to a running Pico application that implements the OTA receive protocol
described in Lecture 3.

Usage:
    cc-flash.py <ccf-file> [--port /dev/cu.usbmodem...] [--baud 115200]

Citations:
    - Lecture 3, sections on the .ccf file format and the OTA protocol.
    - MCUboot serial-recovery protocol, design inspiration.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import time
from pathlib import Path
from typing import Optional

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("error: pyserial is required. pip install pyserial")


CHUNK_SIZE = 256


def find_pico_port() -> Optional[str]:
    """Scan known USB ports for a Pico CDC device."""
    for port in serial.tools.list_ports.comports():
        # Raspberry Pi vendor ID is 0x2E8A.
        if port.vid == 0x2E8A:
            return port.device
    return None


def read_response(ser: serial.Serial, timeout_s: float = 15.0) -> str:
    """Read one line from the device, stripping CR/LF."""
    ser.timeout = timeout_s
    raw = ser.readline()
    if not raw:
        raise TimeoutError(f"no response in {timeout_s} seconds")
    return raw.decode("ascii", errors="replace").strip()


def upload(ccf_path: Path, port: str, baudrate: int) -> None:
    image = ccf_path.read_bytes()
    total = len(image)
    sha = hashlib.sha256(image).hexdigest()

    if total < 48 + 64:
        sys.exit(f"error: {ccf_path} is too small to be a .ccf file")

    print(f"cc-flash: file={ccf_path} size={total} bytes")
    print(f"cc-flash: sha256={sha}")
    print(f"cc-flash: port={port} baud={baudrate}")

    ser = serial.Serial(port, baudrate, timeout=15.0)
    # Drain any stale data.
    ser.reset_input_buffer()

    # Phase 1 — BEGIN. This triggers a staging-bank erase that takes ~8 s.
    print("cc-flash: sending BEGIN; waiting for staging-bank erase (~8 s)...")
    ser.write(f"BEGIN {total} {sha}\n".encode("ascii"))
    t0 = time.time()
    resp = read_response(ser, timeout_s=30.0)
    if resp != "OK":
        sys.exit(f"BEGIN rejected: {resp}")
    print(f"cc-flash: BEGIN acked in {time.time() - t0:.2f} s")

    # Phase 2 — CHUNK loop.
    offset = 0
    sent_chunks = 0
    t_chunks = time.time()
    while offset < total:
        chunk = image[offset:offset + CHUNK_SIZE]
        hex_str = chunk.hex()
        line = f"CHUNK {offset} {len(chunk)} {hex_str}\n"
        ser.write(line.encode("ascii"))

        resp = read_response(ser, timeout_s=5.0)
        if not resp.startswith("OK"):
            sys.exit(f"CHUNK {offset} rejected: {resp}")

        offset += len(chunk)
        sent_chunks += 1
        if sent_chunks % 16 == 0:
            elapsed = time.time() - t_chunks
            rate = offset / elapsed if elapsed > 0 else 0
            pct = 100.0 * offset / total
            print(f"cc-flash:   {offset}/{total} bytes "
                  f"({pct:.1f}%) at {rate/1024:.1f} KB/s")

    elapsed = time.time() - t_chunks
    print(f"cc-flash: all chunks acked in {elapsed:.2f} s "
          f"({total / 1024 / elapsed:.1f} KB/s avg)")

    # Phase 3 — END.
    print("cc-flash: sending END; device will verify SHA...")
    ser.write(b"END\n")
    resp = read_response(ser, timeout_s=5.0)
    if resp != "OK":
        sys.exit(f"END rejected: {resp}")
    print("cc-flash: END acked")

    # Phase 4 — REBOOT.
    print("cc-flash: sending REBOOT...")
    ser.write(b"REBOOT\n")
    try:
        resp = read_response(ser, timeout_s=2.0)
        if resp != "OK":
            print(f"cc-flash: REBOOT response was '{resp}' (continuing)")
    except TimeoutError:
        # The device may reboot before sending OK.
        print("cc-flash: no REBOOT ack received (device probably reset)")

    ser.close()
    print("cc-flash: device should be running new firmware in ~12 seconds.")


def main() -> None:
    ap = argparse.ArgumentParser(description="OTA upload tool for Code Crunch C7 Week 10.")
    ap.add_argument("ccf_file", type=Path)
    ap.add_argument("--port", type=str, default=None,
                    help="serial port (auto-detected if omitted)")
    ap.add_argument("--baud", type=int, default=115200)
    args = ap.parse_args()

    if not args.ccf_file.exists():
        sys.exit(f"error: {args.ccf_file} does not exist")

    port = args.port or find_pico_port()
    if port is None:
        sys.exit("error: could not find a Pico CDC port. "
                 "Pass --port /dev/cu.usbmodem... explicitly.")

    upload(args.ccf_file, port, args.baud)


if __name__ == "__main__":
    main()
