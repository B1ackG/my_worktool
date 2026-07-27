#!/usr/bin/env python3
"""Capture one mouse button or wheel event and print trigger JSON to stdout."""

from __future__ import annotations

import argparse
import json
import select
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

try:
    from evdev import InputDevice, ecodes
except ImportError:
    print("ERROR: missing python3-evdev", file=sys.stderr, flush=True)
    sys.exit(2)

from input_device_utils import (
    DeviceSelectionError,
    WheelDetentNormalizer,
    choose_device,
    device_hi_res_wheel_axes,
    event_to_trigger,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="", help="Input device path")
    parser.add_argument("--timeout", type=float, default=10.0, help="Seconds to wait")
    args = parser.parse_args()

    try:
        path = choose_device(args.device)
        device = InputDevice(path)
    except DeviceSelectionError as exc:
        print(f"ERROR: {exc.code}: {exc}", file=sys.stderr, flush=True)
        return 1
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr, flush=True)
        return 1

    deadline = time.monotonic() + max(1.0, args.timeout)
    grabbed = False
    wheel_normalizer = WheelDetentNormalizer(device_hi_res_wheel_axes(device))
    try:
        device.grab()
        grabbed = True
    except OSError:
        pass

    try:
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            readable, _, _ = select.select([device.fd], [], [], min(0.5, remaining))
            if not readable:
                continue
            for event in device.read():
                # Buttons fire immediately; wheels wait for one full detent.
                if event.type == ecodes.EV_REL:
                    triggers = wheel_normalizer.process_event(event)
                    if triggers:
                        print(json.dumps(triggers[0], ensure_ascii=False), flush=True)
                        return 0
                    continue
                trigger = event_to_trigger(event)
                if trigger:
                    print(json.dumps(trigger, ensure_ascii=False), flush=True)
                    return 0
    finally:
        if grabbed:
            try:
                device.ungrab()
            except OSError:
                pass
        device.close()

    print("ERROR: capture timeout", file=sys.stderr, flush=True)
    return 2


if __name__ == "__main__":
    sys.exit(main())
