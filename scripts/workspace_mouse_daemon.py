#!/usr/bin/env python3
"""DEPRECATED: use scripts/input_quicker_daemon.py instead.

Legacy mapper for mouse side buttons / second wheel to workspace shortcuts.
Kept only for reference; the Qt "快捷助手" page drives input_quicker_daemon.py.

Original behavior: The Qt application wrote ~/.config/LiChenYang/workspace_mouse.json
and started this daemon. It read evdev events and emitted xdotool shortcuts that
most Linux desktops map to previous/next workspace.
"""

from __future__ import annotations

import json
import os
import select
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

try:
    from evdev import InputDevice, ecodes, list_devices
except ImportError:
    print("ERROR: missing python3-evdev. Install it with: sudo apt install python3-evdev", flush=True)
    sys.exit(2)


CONFIG_PATH = Path(
    os.environ.get(
        "WORKSPACE_MOUSE_CONFIG",
        str(Path.home() / ".config" / "LiChenYang" / "workspace_mouse.json"),
    )
)

DEFAULT_CONFIG: dict[str, Any] = {
    "devicePath": "",
    "sideButtonsEnabled": True,
    "wheel2Enabled": False,
    "wheel2Axis": "REL_HWHEEL",
}

LEFT_COMBO = "ctrl+alt+Left"
RIGHT_COMBO = "ctrl+alt+Right"
WHEEL_DEBOUNCE_SECONDS = 0.20

RUNNING = True
RELOAD_REQUESTED = False


def log(message: str) -> None:
    print(message, flush=True)


def handle_stop(_signum: int, _frame: Any) -> None:
    global RUNNING
    RUNNING = False


def handle_reload(_signum: int, _frame: Any) -> None:
    global RELOAD_REQUESTED
    RELOAD_REQUESTED = True


def load_config() -> dict[str, Any]:
    config = DEFAULT_CONFIG.copy()
    if CONFIG_PATH.exists():
        try:
            with CONFIG_PATH.open("r", encoding="utf-8") as f:
                loaded = json.load(f)
            if isinstance(loaded, dict):
                config.update(loaded)
        except Exception as exc:
            log(f"WARN: failed to read config {CONFIG_PATH}: {exc}")
    else:
        save_config(config)
    return config


def save_config(config: dict[str, Any]) -> None:
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    with CONFIG_PATH.open("w", encoding="utf-8") as f:
        json.dump(config, f, indent=4, ensure_ascii=False)
        f.write("\n")


def axis_code(axis_name: str) -> int:
    code = getattr(ecodes, axis_name, None)
    if isinstance(code, int):
        return code
    log(f"WARN: unknown wheel axis {axis_name}, falling back to REL_HWHEEL")
    return ecodes.REL_HWHEEL


def device_supports(device: InputDevice, config: dict[str, Any]) -> bool:
    caps = device.capabilities(absinfo=False)
    keys = set(caps.get(ecodes.EV_KEY, []))
    rels = set(caps.get(ecodes.EV_REL, []))

    has_side_button = bool(
        keys
        & {
            ecodes.BTN_SIDE,
            ecodes.BTN_BACK,
            ecodes.BTN_EXTRA,
            ecodes.BTN_FORWARD,
        }
    )
    has_wheel = axis_code(str(config.get("wheel2Axis", "REL_HWHEEL"))) in rels

    return (bool(config.get("sideButtonsEnabled")) and has_side_button) or (
        bool(config.get("wheel2Enabled")) and has_wheel
    )


def choose_device(config: dict[str, Any]) -> str:
    configured = str(config.get("devicePath", "")).strip()
    if configured:
        return configured

    candidates: list[tuple[int, str, str]] = []
    for path in list_devices():
        try:
            device = InputDevice(path)
            name = device.name or ""
            if device_supports(device, config):
                score = 10
                lowered = name.lower()
                if any(word in lowered for word in ("mouse", "logitech", "razer", "steelseries", "usb")):
                    score += 5
                candidates.append((score, path, name))
        except OSError:
            continue

    if not candidates:
        raise RuntimeError("no matching mouse input device found")

    candidates.sort(reverse=True)
    _score, path, name = candidates[0]
    log(f"INFO: auto-selected device {path} ({name})")
    return path


def send_key(combo: str) -> None:
    try:
        subprocess.run(["xdotool", "key", combo], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        log("ERROR: xdotool not found. Install it with: sudo apt install xdotool")
    except Exception as exc:
        log(f"ERROR: failed to run xdotool: {exc}")


class WorkspaceMouseDaemon:
    def __init__(self) -> None:
        self.config = load_config()
        self.device: InputDevice | None = None
        self.grabbed = False
        self.last_wheel_time = 0.0

    def open_device(self) -> None:
        self.close_device()
        path = choose_device(self.config)
        self.device = InputDevice(path)
        log(f"INFO: listening on {path} ({self.device.name})")
        try:
            self.device.grab()
            self.grabbed = True
            log("INFO: input device grabbed")
        except OSError as exc:
            self.grabbed = False
            log(f"WARN: could not grab input device: {exc}")

    def close_device(self) -> None:
        if self.device is None:
            return
        if self.grabbed:
            try:
                self.device.ungrab()
            except OSError:
                pass
        try:
            self.device.close()
        except OSError:
            pass
        self.device = None
        self.grabbed = False

    def reload(self) -> None:
        log("INFO: reloading config")
        self.config = load_config()
        self.open_device()

    def handle_key(self, event: Any) -> None:
        if not self.config.get("sideButtonsEnabled", True) or event.value != 1:
            return

        if event.code in (ecodes.BTN_SIDE, ecodes.BTN_BACK):
            send_key(LEFT_COMBO)
        elif event.code in (ecodes.BTN_EXTRA, ecodes.BTN_FORWARD):
            send_key(RIGHT_COMBO)

    def handle_rel(self, event: Any) -> None:
        if not self.config.get("wheel2Enabled", False):
            return
        if event.code != axis_code(str(self.config.get("wheel2Axis", "REL_HWHEEL"))):
            return

        now = time.monotonic()
        if now - self.last_wheel_time < WHEEL_DEBOUNCE_SECONDS:
            return
        self.last_wheel_time = now

        if event.value < 0:
            send_key(LEFT_COMBO)
        elif event.value > 0:
            send_key(RIGHT_COMBO)

    def run(self) -> int:
        global RELOAD_REQUESTED

        self.open_device()
        log("INFO: workspace mouse daemon started")

        while RUNNING:
            if RELOAD_REQUESTED:
                RELOAD_REQUESTED = False
                self.reload()

            if self.device is None:
                time.sleep(1.0)
                continue

            readable, _, _ = select.select([self.device.fd], [], [], 0.5)
            if not readable:
                continue

            try:
                for event in self.device.read():
                    if event.type == ecodes.EV_KEY:
                        self.handle_key(event)
                    elif event.type == ecodes.EV_REL:
                        self.handle_rel(event)
            except OSError as exc:
                log(f"WARN: input device read failed: {exc}; retrying")
                time.sleep(1.0)
                self.open_device()

        self.close_device()
        log("INFO: workspace mouse daemon stopped")
        return 0


def main() -> int:
    signal.signal(signal.SIGTERM, handle_stop)
    signal.signal(signal.SIGINT, handle_stop)
    signal.signal(signal.SIGHUP, handle_reload)

    try:
        return WorkspaceMouseDaemon().run()
    except PermissionError as exc:
        log(f"ERROR: permission denied reading input device: {exc}")
        log("ERROR: add the user to the input group or run with appropriate permissions")
        return 3
    except Exception as exc:
        log(f"ERROR: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
