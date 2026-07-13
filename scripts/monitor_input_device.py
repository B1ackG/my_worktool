#!/usr/bin/env python3
"""Monitor input device events and emit JSON lines for the Input Quicker UI."""

from __future__ import annotations

import argparse
import json
import select
import signal
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

try:
    from evdev import InputDevice, ecodes
except ImportError:
    print("ERROR: missing python3-evdev", file=sys.stderr, flush=True)
    sys.exit(2)

from input_device_utils import DeviceSelectionError, choose_device, event_to_trigger, rel_axis_name

RUNNING = True


def handle_stop(_signum: int, _frame: Any) -> None:
    global RUNNING
    RUNNING = False


def event_label(event: Any) -> str:
    if event.type == ecodes.EV_KEY:
        code_name = ecodes.BTN.get(event.code) or ecodes.KEY.get(event.code) or str(event.code)
        if isinstance(code_name, (list, tuple)):
            code_name = code_name[0] if code_name else str(event.code)
        if event.value == 1:
            return f"{code_name} 按下"
        if event.value == 0:
            return f"{code_name} 释放"
        return f"{code_name} 重复"
    if event.type == ecodes.EV_REL:
        axis_name = rel_axis_name(event.code)
        if event.value > 0:
            return f"{axis_name} 正向"
        if event.value < 0:
            return f"{axis_name} 反向"
        return axis_name
    ev_name = ecodes.EV.get(event.type, f"EV_{event.type}")
    if isinstance(ev_name, (list, tuple)):
        ev_name = ev_name[0] if ev_name else f"EV_{event.type}"
    return f"{ev_name} code={event.code} value={event.value}"


def emit_event(event: Any, device_name: str) -> None:
    trigger = event_to_trigger(event)
    ev_type = ecodes.EV.get(event.type, f"EV_{event.type}")
    if isinstance(ev_type, (list, tuple)):
        ev_type = ev_type[0] if ev_type else f"EV_{event.type}"
    payload = {
        "ts": datetime.now().strftime("%H:%M:%S.%f")[:-3],
        "type": ev_type,
        "code": event.code,
        "value": event.value,
        "label": event_label(event),
        "device": device_name,
    }
    if trigger:
        payload["trigger"] = trigger
    print(json.dumps(payload, ensure_ascii=False), flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="", help="Input device path")
    parser.add_argument("--json-lines", action="store_true", help="Emit JSON lines (default)")
    args = parser.parse_args()

    signal.signal(signal.SIGTERM, handle_stop)
    signal.signal(signal.SIGINT, handle_stop)

    try:
        path = choose_device(args.device)
        device = InputDevice(path)
    except DeviceSelectionError as exc:
        print(f"ERROR: {exc}", file=sys.stderr, flush=True)
        return 1
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr, flush=True)
        return 1

    device_name = device.name or path
    grabbed = False
    try:
        device.grab()
        grabbed = True
    except OSError:
        pass

    print(
        json.dumps(
            {
                "event": "started",
                "path": path,
                "name": device_name,
                "grabbed": grabbed,
            },
            ensure_ascii=False,
        ),
        flush=True,
    )

    try:
        while RUNNING:
            readable, _, _ = select.select([device.fd], [], [], 0.5)
            if not readable:
                continue
            for event in device.read():
                if event.type in (ecodes.EV_SYN, ecodes.EV_MSC):
                    continue
                emit_event(event, device_name)
    finally:
        if grabbed:
            try:
                device.ungrab()
            except OSError:
                pass
        device.close()
        print(json.dumps({"event": "stopped"}, ensure_ascii=False), flush=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
