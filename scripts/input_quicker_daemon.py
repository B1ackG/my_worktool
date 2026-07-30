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
from dataclasses import dataclass
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
    WheelDetentNormalizer,
    button_code_name,
    choose_device,
    device_hi_res_wheel_axes,
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
    "super": "super",
    # Tiling Assistant / Mutter: Super+Left/Right
    "window_snap_left": "super+Left",
    "window_snap_right": "super+Right",
}

# Hold+drag: accumulate REL_X until release; horizontal dominance required.
DEFAULT_HOLD_DRAG_THRESHOLD = 100

RUNNING = True
RELOAD_REQUESTED = False


@dataclass
class HoldDragSession:
    binding: dict[str, Any]
    code: str
    threshold: int
    dx: int = 0
    dy: int = 0


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
        "deviceName": "",
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


def _gdbus_get_overview_active() -> bool | None:
    """Return OverviewActive, or None if GNOME Shell D-Bus is unavailable."""
    try:
        result = subprocess.run(
            [
                "gdbus",
                "call",
                "--session",
                "--dest",
                "org.gnome.Shell",
                "--object-path",
                "/org/gnome/Shell",
                "--method",
                "org.freedesktop.DBus.Properties.Get",
                "org.gnome.Shell",
                "OverviewActive",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=3,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        return None
    if result.returncode != 0:
        return None
    text = (result.stdout or "").lower()
    if "true" in text:
        return True
    if "false" in text:
        return False
    return None


def _gdbus_set_overview_active(active: bool) -> bool:
    try:
        result = subprocess.run(
            [
                "gdbus",
                "call",
                "--session",
                "--dest",
                "org.gnome.Shell",
                "--object-path",
                "/org/gnome/Shell",
                "--method",
                "org.freedesktop.DBus.Properties.Set",
                "org.gnome.Shell",
                "OverviewActive",
                "<true>" if active else "<false>",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=3,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        return False
    return result.returncode == 0


def _kde_invoke_overview() -> bool:
    try:
        result = subprocess.run(
            [
                "qdbus",
                "org.kde.kglobalaccel",
                "/component/kwin",
                "org.kde.kglobalaccel.Component.invokeShortcut",
                "Overview",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=3,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        return False
    return result.returncode == 0


def toggle_activities_overview() -> None:
    """Toggle desktop Activities/Overview via GNOME OverviewActive.

    Fallbacks (KDE / Alt+F1) only when GNOME D-Bus is unavailable.
    Never fall back after a partial Set — that previously fired Alt+F1 while
    closing overview and broke an otherwise working toggle.
    """
    current = _gdbus_get_overview_active()
    if current is None:
        if _kde_invoke_overview():
            return
        log("WARN: overview D-Bus unavailable; falling back to alt+F1")
        send_key("alt+F1")
        return

    target = not current
    deadline = time.monotonic() + 0.40
    last_after: bool | None = current
    while time.monotonic() < deadline:
        if not _gdbus_set_overview_active(target):
            time.sleep(0.05)
            continue
        for _ in range(8):
            last_after = _gdbus_get_overview_active()
            if last_after == target:
                log(f"INFO: OverviewActive -> {target}")
                return
            time.sleep(0.025)
        time.sleep(0.03)

    log(
        f"WARN: OverviewActive did not become {target} "
        f"(last={last_after}); not falling back to alt+F1"
    )


def snap_window_horizontal(side: str, binding_name: str) -> None:
    """Snap focused window left/right via Super+Left/Right (Tiling Assistant)."""
    preset = "window_snap_left" if side == "left" else "window_snap_right"
    combo = PRESETS[preset]
    log(f"INFO: [{binding_name}] window snap {side} -> {combo}")
    send_key(combo)


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
        if preset == "activities_overview":
            log(f"INFO: [{binding_name}] preset activities_overview -> toggle overview")
            toggle_activities_overview()
            return
        if preset == "window_snap_left":
            snap_window_horizontal("left", binding_name)
            return
        if preset == "window_snap_right":
            snap_window_horizontal("right", binding_name)
            return
        if preset == "window_snap_horizontal":
            # Direction is chosen by hold-drag; calling this alone is a no-op.
            log(f"WARN: [{binding_name}] window_snap_horizontal needs button_hold_drag")
            return
        combo = PRESETS.get(preset)
        if combo:
            log(f"INFO: [{binding_name}] preset {preset} -> {combo}")
            send_key(combo)
        else:
            log(f"WARN: unknown preset: {preset}")


def resolve_device_path(config: dict[str, Any]) -> str:
    configured = str(config.get("devicePath", "")).strip()
    configured_name = str(config.get("deviceName", "")).strip()
    path = choose_device(configured, configured_name)
    try:
        device = InputDevice(path)
        name = device.name or path
        device.close()
    except OSError:
        name = configured_name or path

    if configured and configured != path:
        log(
            f"INFO: device path changed {configured} -> {path}"
            + (f" ({name})" if name else "")
        )
    elif configured_name and not configured:
        log(f"INFO: resolved device by name '{configured_name}' -> {path}")
    elif not configured and not configured_name:
        log(f"INFO: auto-selected device {path} ({name})")
    return path


class InputQuickerDaemon:
    def __init__(self) -> None:
        self.config = load_config()
        self.device: InputDevice | None = None
        self.grabbed = False
        self.active_bindings: list[dict[str, Any]] = []
        self.hold_drag_by_code: dict[str, dict[str, Any]] = {}
        self.hold_session: HoldDragSession | None = None
        self.listening_path = ""
        self.wheel_normalizer = WheelDetentNormalizer()

    def reload_bindings(self) -> None:
        self.config = load_config()
        self.active_bindings = [
            b for b in self.config.get("bindings", [])
            if isinstance(b, dict) and b.get("enabled", True)
        ]
        self.hold_drag_by_code = {}
        for binding in self.active_bindings:
            trigger = binding.get("trigger", {})
            if not isinstance(trigger, dict):
                continue
            if trigger.get("type") != "button_hold_drag":
                continue
            code = str(trigger.get("code", "")).strip()
            if code:
                self.hold_drag_by_code[code] = binding
        self.hold_session = None
        log(
            f"INFO: loaded {len(self.active_bindings)} active binding(s)"
            + (
                f", hold-drag on {', '.join(sorted(self.hold_drag_by_code))}"
                if self.hold_drag_by_code
                else ""
            )
        )

    def finish_hold_drag(self) -> None:
        session = self.hold_session
        self.hold_session = None
        if session is None:
            return

        binding = session.binding
        binding_name = str(binding.get("name", binding.get("id", "?")))
        dx = session.dx
        dy = session.dy
        threshold = session.threshold
        action = binding.get("action", {})
        if not isinstance(action, dict):
            action = {}

        # Require clear horizontal intent so vertical flicks don't steal the tap.
        if abs(dx) >= threshold and abs(dx) >= abs(dy):
            side = "right" if dx > 0 else "left"
            log(
                f"INFO: hold-drag [{binding_name}] dx={dx} dy={dy} "
                f"threshold={threshold} -> snap {side}"
            )
            snap_window_horizontal(side, binding_name)
            return

        tap_preset = str(action.get("tapPreset", "")).strip()
        if tap_preset:
            log(
                f"INFO: hold-drag [{binding_name}] tap "
                f"(dx={dx} dy={dy}) -> {tap_preset}"
            )
            execute_action({"type": "preset", "preset": tap_preset}, binding_name)
            return

        log(
            f"INFO: hold-drag [{binding_name}] ignored "
            f"(dx={dx} dy={dy} < threshold {threshold})"
        )

    def open_device(self) -> None:
        self.close_device()
        path = resolve_device_path(self.config)
        self.device = InputDevice(path)
        self.listening_path = path
        # Remember stable identity so reboot / reconnect can find the mouse again.
        # Persist stable identity + current event node so reconnects stay on
        # the same mouse (e.g. Logitech MX Master 3S) instead of auto-picking
        # a laptop pointer after Bluetooth renumbers /dev/input/eventN.
        live_name = (self.device.name or "").strip()
        changed = False
        if live_name and str(self.config.get("deviceName", "")).strip() != live_name:
            self.config["deviceName"] = live_name
            changed = True
        if path and str(self.config.get("devicePath", "")).strip() != path:
            self.config["devicePath"] = path
            changed = True
        if changed:
            try:
                save_config(self.config)
                log(f"INFO: persisted device {path} ({live_name})")
            except OSError as exc:
                log(f"WARN: could not persist device identity: {exc}")

        prefer_hi_res = device_hi_res_wheel_axes(self.device)
        self.wheel_normalizer.set_prefer_hi_res(prefer_hi_res)
        log(f"INFO: listening on {path} ({self.device.name})")
        if prefer_hi_res:
            log(
                "INFO: preferring high-res wheel axes "
                f"({', '.join(sorted(prefer_hi_res))}); legacy companions ignored"
            )

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
        self.hold_session = None
        self.wheel_normalizer.reset()

    def open_device_with_retry(self, reason: str = "device unavailable") -> bool:
        """Keep daemon alive while mouse is missing (boot / sleep / reconnect)."""
        delay = 1.0
        while RUNNING:
            try:
                self.open_device()
                return True
            except DeviceSelectionError as exc:
                log(f"WARN: {reason}: {exc}; retrying in {delay:.0f}s")
            except OSError as exc:
                log(f"WARN: {reason}: {exc}; retrying in {delay:.0f}s")
            self.close_device()
            time.sleep(delay)
            delay = min(delay * 1.5, 10.0)
            # Config may change (SIGHUP) while we wait.
            if RELOAD_REQUESTED:
                return False
        return False

    def reload(self) -> None:
        log("INFO: reloading config")
        old_path = self.listening_path
        old_grab = bool(self.config.get("grabDevice", False))
        self.reload_bindings()
        try:
            new_path = resolve_device_path(self.config)
        except DeviceSelectionError as exc:
            log(f"WARN: reload deferred, device not ready: {exc}")
            self.close_device()
            return
        new_grab = bool(self.config.get("grabDevice", False))
        if old_path == new_path and old_grab == new_grab and self.device is not None:
            log("INFO: bindings hot-reloaded (device unchanged)")
            return
        if not self.open_device_with_retry("reload open failed"):
            return

    def dispatch_trigger(self, event_trigger: dict[str, str]) -> None:
        for binding in self.active_bindings:
            trigger = binding.get("trigger", {})
            if not isinstance(trigger, dict):
                continue
            # Hold-drag bindings are handled in handle_event (press/move/release).
            if trigger.get("type") == "button_hold_drag":
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

    def handle_event(self, event: Any) -> None:
        if not self.config.get("enabled", True):
            return

        if event.type == ecodes.EV_KEY:
            code_name = button_code_name(event.code)
            if code_name and code_name in self.hold_drag_by_code:
                binding = self.hold_drag_by_code[code_name]
                trigger = binding.get("trigger", {})
                if event.value == 1:
                    try:
                        threshold = int(trigger.get("threshold", DEFAULT_HOLD_DRAG_THRESHOLD))
                    except (TypeError, ValueError):
                        threshold = DEFAULT_HOLD_DRAG_THRESHOLD
                    threshold = max(20, threshold)
                    self.hold_session = HoldDragSession(
                        binding=binding,
                        code=code_name,
                        threshold=threshold,
                    )
                    return
                if event.value == 0:
                    if self.hold_session and self.hold_session.code == code_name:
                        self.finish_hold_drag()
                    return
                # value == 2 (key repeat): ignore
                return

        if event.type == ecodes.EV_REL:
            if self.hold_session is not None:
                if event.code == ecodes.REL_X:
                    self.hold_session.dx += int(event.value)
                elif event.code == ecodes.REL_Y:
                    self.hold_session.dy += int(event.value)
            for event_trigger in self.wheel_normalizer.process_event(event):
                self.dispatch_trigger(event_trigger)
            return

        event_trigger = event_to_trigger(event)
        if event_trigger:
            self.dispatch_trigger(event_trigger)

    def run(self) -> int:
        global RELOAD_REQUESTED

        if os.environ.get("XDG_SESSION_TYPE", "").lower() == "wayland":
            log("WARN: Wayland session detected; xdotool keyboard actions may not work")

        self.reload_bindings()
        if not self.config.get("enabled", True):
            log("INFO: input quicker disabled in config")
            return 0

        # Wait through early autostart / Bluetooth reconnect instead of exiting.
        if not self.open_device_with_retry("waiting for input device at startup"):
            if not RUNNING:
                self.close_device()
                log("INFO: input quicker daemon stopped")
                return 0
        log("INFO: input quicker daemon started")

        while RUNNING:
            if RELOAD_REQUESTED:
                RELOAD_REQUESTED = False
                self.reload()

            if self.device is None:
                if not self.open_device_with_retry("device lost"):
                    continue
                continue

            readable, _, _ = select.select([self.device.fd], [], [], 0.5)
            if not readable:
                continue

            try:
                for event in self.device.read():
                    self.handle_event(event)
            except OSError as exc:
                log(f"WARN: read failed: {exc}; reopening device")
                self.close_device()
                self.open_device_with_retry("device reconnect")

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
