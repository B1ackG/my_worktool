#!/usr/bin/env python3
"""Quicker-style input daemon: match mouse triggers to keyboard/command/preset actions."""

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

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

try:
    from evdev import InputDevice, ecodes
except ImportError:
    print("ERROR: missing python3-evdev. Install: sudo apt install python3-evdev", flush=True)
    sys.exit(2)

from input_device_utils import (
    DeviceSelectionError,
    choose_device,
    event_to_trigger,
    triggers_match,
)

CONFIG_DIR = Path.home() / ".config" / "LiChenYang"
CONFIG_PATH = Path(
    os.environ.get(
        "INPUT_QUICKER_CONFIG",
        str(CONFIG_DIR / "input_quicker.json"),
    )
)
PID_PATH = Path(
    os.environ.get(
        "INPUT_QUICKER_PID",
        str(CONFIG_DIR / "input_quicker.pid"),
    )
)
LOG_PATH = Path(
    os.environ.get(
        "INPUT_QUICKER_LOG",
        str(CONFIG_DIR / "input_quicker.log"),
    )
)

PRESETS: dict[str, str] = {
    "workspace_prev": "ctrl+alt+Left",
    "workspace_next": "ctrl+alt+Right",
    "volume_up": "XF86AudioRaiseVolume",
    "volume_down": "XF86AudioLowerVolume",
    "mute": "XF86AudioMute",
    "media_next": "XF86AudioNext",
    "media_prev": "XF86AudioPrev",
}

WHEEL_DEBOUNCE_SECONDS = 0.20
RUNNING = True
RELOAD_REQUESTED = False


def log(message: str) -> None:
    print(message, flush=True)
    try:
        LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
        with LOG_PATH.open("a", encoding="utf-8") as f:
            f.write(message + "\n")
    except OSError:
        pass


def handle_stop(_signum: int, _frame: Any) -> None:
    global RUNNING
    RUNNING = False


def handle_reload(_signum: int, _frame: Any) -> None:
    global RELOAD_REQUESTED
    RELOAD_REQUESTED = True


def write_pid_file() -> None:
    PID_PATH.parent.mkdir(parents=True, exist_ok=True)
    PID_PATH.write_text(f"{os.getpid()}\n", encoding="utf-8")


def remove_pid_file() -> None:
    try:
        if PID_PATH.exists() and PID_PATH.read_text(encoding="utf-8").strip() == str(os.getpid()):
            PID_PATH.unlink()
    except OSError:
        pass


def read_pid() -> int | None:
    try:
        text = PID_PATH.read_text(encoding="utf-8").strip()
        return int(text) if text else None
    except (OSError, ValueError):
        return None


def pid_is_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def pid_runs_daemon(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        cmdline = Path(f"/proc/{pid}/cmdline").read_bytes()
    except OSError:
        return False
    return b"input_quicker_daemon.py" in cmdline


def clear_stale_pid_file() -> None:
    existing = read_pid()
    if existing is None:
        return
    if existing == os.getpid():
        return
    if pid_is_alive(existing) and pid_runs_daemon(existing):
        return
    remove_pid_file()


def ensure_single_instance() -> None:
    clear_stale_pid_file()
    existing = read_pid()
    if existing is not None and existing != os.getpid() and pid_runs_daemon(existing) and pid_is_alive(existing):
        log(f"ERROR: another input_quicker_daemon is already running (pid {existing})")
        sys.exit(4)


def default_config() -> dict[str, Any]:
    return {
        "devicePath": "",
        "wheel2Axis": "REL_HWHEEL",
        "enabled": True,
        "grabDevice": False,
        "daemonAutostart": True,
        "bindings": [
            {
                "id": "default-side-prev",
                "name": "侧键上一工作区",
                "enabled": True,
                "trigger": {"type": "mouse_button", "code": "BTN_SIDE"},
                "action": {"type": "preset", "preset": "workspace_prev"},
            },
            {
                "id": "default-side-next",
                "name": "侧键下一工作区",
                "enabled": True,
                "trigger": {"type": "mouse_button", "code": "BTN_EXTRA"},
                "action": {"type": "preset", "preset": "workspace_next"},
            },
        ],
    }


def migrate_legacy_config() -> dict[str, Any] | None:
    legacy_path = Path.home() / ".config" / "LiChenYang" / "workspace_mouse.json"
    if not legacy_path.exists():
        return None
    try:
        with legacy_path.open("r", encoding="utf-8") as f:
            legacy = json.load(f)
    except Exception:
        return None
    if not isinstance(legacy, dict):
        return None

    config = default_config()
    config["devicePath"] = str(legacy.get("devicePath", ""))
    config["wheel2Axis"] = str(legacy.get("wheel2Axis", "REL_HWHEEL"))
    bindings: list[dict[str, Any]] = []

    if legacy.get("sideButtonsEnabled", True):
        bindings.extend(default_config()["bindings"])
    if legacy.get("wheel2Enabled", False):
        axis = str(legacy.get("wheel2Axis", "REL_HWHEEL"))
        bindings.append(
            {
                "id": "migrated-hwheel-prev",
                "name": "第二滚轮上一工作区",
                "enabled": True,
                "trigger": {"type": "wheel", "axis": axis, "direction": "negative"},
                "action": {"type": "preset", "preset": "workspace_prev"},
            }
        )
        bindings.append(
            {
                "id": "migrated-hwheel-next",
                "name": "第二滚轮下一工作区",
                "enabled": True,
                "trigger": {"type": "wheel", "axis": axis, "direction": "positive"},
                "action": {"type": "preset", "preset": "workspace_next"},
            }
        )
    config["bindings"] = bindings or default_config()["bindings"]
    log(f"INFO: migrated legacy config from {legacy_path}")
    return config


def load_config() -> dict[str, Any]:
    if not CONFIG_PATH.exists():
        migrated = migrate_legacy_config()
        config = migrated if migrated else default_config()
        save_config(config)
        return config

    try:
        with CONFIG_PATH.open("r", encoding="utf-8") as f:
            loaded = json.load(f)
    except Exception as exc:
        log(f"WARN: failed to read config: {exc}")
        return default_config()

    if not isinstance(loaded, dict):
        return default_config()
    config = default_config()
    config.update(loaded)
    if not isinstance(config.get("bindings"), list):
        config["bindings"] = default_config()["bindings"]
    return config


def save_config(config: dict[str, Any]) -> None:
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    with CONFIG_PATH.open("w", encoding="utf-8") as f:
        json.dump(config, f, indent=4, ensure_ascii=False)
        f.write("\n")


def send_key(combo: str) -> None:
    try:
        subprocess.run(
            ["xdotool", "key", combo],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except FileNotFoundError:
        log("ERROR: xdotool not found. Install: sudo apt install xdotool")
    except Exception as exc:
        log(f"ERROR: xdotool failed: {exc}")


def run_command(command: str) -> None:
    if not command.strip():
        return
    try:
        subprocess.Popen(command, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception as exc:
        log(f"ERROR: command failed: {exc}")


def execute_action(action: dict[str, Any], binding_name: str) -> None:
    atype = str(action.get("type", ""))
    if atype == "keyboard":
        combo = str(action.get("combo", "")).strip()
        if combo:
            log(f"INFO: [{binding_name}] keyboard -> {combo}")
            send_key(combo)
    elif atype == "command":
        command = str(action.get("command", "")).strip()
        if command:
            log(f"INFO: [{binding_name}] command -> {command}")
            run_command(command)
    elif atype == "preset":
        preset = str(action.get("preset", "")).strip()
        combo = PRESETS.get(preset)
        if combo:
            log(f"INFO: [{binding_name}] preset {preset} -> {combo}")
            send_key(combo)
        else:
            log(f"WARN: unknown preset: {preset}")


def resolve_device_path(config: dict[str, Any]) -> str:
    configured = str(config.get("devicePath", "")).strip()
    path = choose_device(configured)
    if not configured:
        try:
            device = InputDevice(path)
            name = device.name or path
            device.close()
            log(f"INFO: auto-selected device {path} ({name})")
        except OSError:
            log(f"INFO: auto-selected device {path}")
    return path


class InputQuickerDaemon:
    def __init__(self) -> None:
        self.config = load_config()
        self.device: InputDevice | None = None
        self.grabbed = False
        self.last_wheel_time = 0.0
        self.active_bindings: list[dict[str, Any]] = []
        self.listening_path = ""

    def reload_bindings(self) -> None:
        self.config = load_config()
        self.active_bindings = [
            b for b in self.config.get("bindings", [])
            if isinstance(b, dict) and b.get("enabled", True)
        ]
        log(f"INFO: loaded {len(self.active_bindings)} active binding(s)")

    def open_device(self) -> None:
        self.close_device()
        path = resolve_device_path(self.config)
        self.device = InputDevice(path)
        self.listening_path = path
        log(f"INFO: listening on {path} ({self.device.name})")

        want_grab = bool(self.config.get("grabDevice", False))
        if want_grab:
            try:
                self.device.grab()
                self.grabbed = True
                log("INFO: input device grabbed (exclusive)")
            except OSError as exc:
                self.grabbed = False
                log(f"WARN: could not grab device: {exc}")
        else:
            self.grabbed = False
            log("INFO: passive listen mode (mouse events also go to system)")

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
        self.listening_path = ""

    def reload(self) -> None:
        log("INFO: reloading config")
        old_path = self.listening_path
        old_grab = bool(self.config.get("grabDevice", False))
        self.reload_bindings()
        new_path = resolve_device_path(self.config)
        new_grab = bool(self.config.get("grabDevice", False))
        if old_path == new_path and old_grab == new_grab and self.device is not None:
            log("INFO: bindings hot-reloaded (device unchanged)")
            return
        self.open_device()

    def handle_event(self, event: Any) -> None:
        if not self.config.get("enabled", True):
            return

        event_trigger = event_to_trigger(event)
        if not event_trigger:
            return

        if event_trigger.get("type") == "wheel":
            now = time.monotonic()
            if now - self.last_wheel_time < WHEEL_DEBOUNCE_SECONDS:
                return
            self.last_wheel_time = now

        for binding in self.active_bindings:
            trigger = binding.get("trigger", {})
            if not isinstance(trigger, dict):
                continue
            if triggers_match(trigger, event_trigger):
                binding_name = str(binding.get("name", binding.get("id", "?")))
                log(
                    f"INFO: trigger matched [{binding_name}] "
                    f"{trigger} <- {event_trigger}"
                )
                action = binding.get("action", {})
                if isinstance(action, dict):
                    execute_action(action, binding_name)
                return

    def run(self) -> int:
        global RELOAD_REQUESTED

        if os.environ.get("XDG_SESSION_TYPE", "").lower() == "wayland":
            log("WARN: Wayland session detected; xdotool keyboard actions may not work")

        self.reload_bindings()
        if not self.config.get("enabled", True):
            log("INFO: input quicker disabled in config")
            return 0

        self.open_device()
        log("INFO: input quicker daemon started")

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
                    self.handle_event(event)
            except OSError as exc:
                log(f"WARN: read failed: {exc}; retrying")
                time.sleep(1.0)
                self.open_device()

        self.close_device()
        log("INFO: input quicker daemon stopped")
        return 0


def main() -> int:
    signal.signal(signal.SIGTERM, handle_stop)
    signal.signal(signal.SIGINT, handle_stop)
    signal.signal(signal.SIGHUP, handle_reload)

    ensure_single_instance()
    write_pid_file()
    try:
        return InputQuickerDaemon().run()
    except DeviceSelectionError as exc:
        log(f"ERROR: {exc}")
        if exc.code == "permission_denied":
            log("ERROR: add user to input group: sudo usermod -aG input $USER")
        return 3
    except PermissionError as exc:
        log(f"ERROR: permission denied: {exc}")
        log("ERROR: add user to input group: sudo usermod -aG input $USER")
        return 3
    except Exception as exc:
        log(f"ERROR: {exc}")
        return 1
    finally:
        remove_pid_file()


if __name__ == "__main__":
    sys.exit(main())
