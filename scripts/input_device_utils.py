#!/usr/bin/env python3
"""Shared input device discovery, scoring, and selection for Input Quicker."""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys
import time
from dataclasses import dataclass, asdict
from typing import Any

try:
    from evdev import InputDevice, ecodes, list_devices
except ImportError:
    InputDevice = None  # type: ignore
    ecodes = None  # type: ignore
    list_devices = None  # type: ignore

NAME_HINTS = ("mouse", "logitech", "razer", "steelseries", "pointer", "trackball", "trackpad")
KEYBOARD_ONLY_HINTS = ("keyboard", "kbd", "keypad")
PROC_DEVICES_PATH = "/proc/bus/input/devices"
EVENT_RE = re.compile(r"\bevent(\d+)\b")


@dataclass
class DeviceInfo:
    path: str
    name: str
    score: int
    caps_summary: str
    accessible: bool
    recommended: bool
    error: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class _ProcDevice:
    name: str
    handlers: str
    path: str
    has_key: bool
    has_rel: bool


class DeviceSelectionError(RuntimeError):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


def _require_evdev() -> None:
    if InputDevice is None or ecodes is None:
        raise DeviceSelectionError(
            "missing_evdev",
            "缺少 python3-evdev，请安装: sudo apt install python3-evdev",
        )


def _ev_type_code(ev_type: Any) -> int:
    if isinstance(ev_type, tuple):
        for part in reversed(ev_type):
            if isinstance(part, int):
                return part
        return 0
    return int(ev_type)


def _code_entry_value(entry: Any) -> int | None:
    if isinstance(entry, int):
        return entry
    if isinstance(entry, tuple):
        for part in reversed(entry):
            if isinstance(part, int):
                return part
    return None


def _code_entry_names(entry: Any) -> list[str]:
    names: list[str] = []
    if not isinstance(entry, tuple) or not entry:
        return names
    first = entry[0]
    if isinstance(first, str):
        names.append(first)
    elif isinstance(first, (list, tuple)):
        names.extend(str(x) for x in first)
    return names


def _device_caps(device: InputDevice) -> tuple[set[str], set[int]]:
    caps: set[str] = set()
    codes: set[int] = set()
    try:
        raw_caps = device.capabilities(verbose=True)
    except Exception:
        raw_caps = device.capabilities(verbose=False)

    for ev_type, ev_codes in raw_caps.items():
        ev_code = _ev_type_code(ev_type)
        if isinstance(ev_type, tuple) and ev_type and isinstance(ev_type[0], str):
            caps.add(ev_type[0])
        else:
            caps.add(ecodes.EV.get(ev_code, f"EV_{ev_code}"))

        for entry in ev_codes:
            code_val = _code_entry_value(entry)
            if code_val is None:
                continue
            codes.add(code_val)
            for name in _code_entry_names(entry):
                caps.add(name)
            if ev_code == ecodes.EV_KEY:
                key_name = ecodes.BTN.get(code_val) or ecodes.KEY.get(code_val)
                if isinstance(key_name, str):
                    caps.add(key_name)
                elif isinstance(key_name, (list, tuple)):
                    caps.update(str(x) for x in key_name)
            elif ev_code == ecodes.EV_REL:
                rel_name = ecodes.REL.get(code_val)
                if isinstance(rel_name, str):
                    caps.add(rel_name)
                elif isinstance(rel_name, (list, tuple)):
                    caps.update(str(x) for x in rel_name)
    return caps, codes


def _score_device(name: str, caps: set[str], codes: set[int]) -> tuple[int, str]:
    lowered = (name or "").lower()
    score = 0
    summary_parts: list[str] = []

    has_left = ecodes.BTN_LEFT in codes
    has_middle = ecodes.BTN_MIDDLE in codes
    has_side = ecodes.BTN_SIDE in codes or ecodes.BTN_BACK in codes
    has_extra = ecodes.BTN_EXTRA in codes or ecodes.BTN_FORWARD in codes
    has_rel_xy = ecodes.REL_X in codes and ecodes.REL_Y in codes
    has_wheel = ecodes.REL_WHEEL in codes
    has_hwheel = ecodes.REL_HWHEEL in codes

    if has_left and has_rel_xy:
        score += 50
        summary_parts.append("pointer")
    elif has_rel_xy:
        score += 35
        summary_parts.append("motion")
    if has_wheel:
        score += 15
        summary_parts.append("wheel")
    if has_hwheel:
        score += 15
        summary_parts.append("hwheel")
    if has_side or has_extra:
        score += 10
        summary_parts.append("side_btns")
    if has_middle:
        score += 5

    if any(hint in lowered for hint in NAME_HINTS):
        score += 8
    if any(hint in lowered for hint in KEYBOARD_ONLY_HINTS) and not has_left and not has_rel_xy:
        score -= 40

    if score <= 0:
        return 0, ""

    if not summary_parts:
        summary_parts.append("input")
    return score, ",".join(summary_parts)


def _score_from_proc(name: str, handlers: str, has_key: bool, has_rel: bool) -> tuple[int, str]:
    lowered = (name or "").lower()
    handlers_lower = (handlers or "").lower()
    score = 0
    summary_parts: list[str] = []

    is_pointer = (
        "mouse" in handlers_lower
        or "mouse" in lowered
        or "touchpad" in lowered
        or "pointer" in lowered
        or any(h in lowered for h in ("logitech", "razer", "steelseries", "trackball", "vxe"))
    )
    if not is_pointer:
        return 0, ""

    if "mouse" in handlers_lower:
        score += 45
        summary_parts.append("pointer")
    if "mouse" in lowered or "pointer" in lowered:
        score += 20
        if "pointer" not in summary_parts:
            summary_parts.append("pointer")
    if "touchpad" in lowered:
        score += 30
        summary_parts.append("touchpad")
    if has_rel:
        score += 15
        summary_parts.append("rel")
    if has_key:
        score += 5
        summary_parts.append("buttons")
    if any(hint in lowered for hint in NAME_HINTS):
        score += 8

    if score <= 0:
        return 0, ""

    if not summary_parts:
        summary_parts.append("input")
    return score, ",".join(summary_parts)


def _parse_proc_devices() -> list[_ProcDevice]:
    try:
        with open(PROC_DEVICES_PATH, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()
    except OSError:
        return []

    devices: list[_ProcDevice] = []
    name = ""
    handlers = ""
    has_key = False
    has_rel = False

    def flush() -> None:
        nonlocal name, handlers, has_key, has_rel
        match = EVENT_RE.search(handlers)
        if not match:
            name = ""
            handlers = ""
            has_key = False
            has_rel = False
            return

        path = f"/dev/input/event{match.group(1)}"
        score, _summary = _score_from_proc(name, handlers, has_key, has_rel)
        if score > 0:
            devices.append(
                _ProcDevice(
                    name=name or path,
                    handlers=handlers,
                    path=path,
                    has_key=has_key,
                    has_rel=has_rel,
                )
            )
        name = ""
        handlers = ""
        has_key = False
        has_rel = False

    for line in content.splitlines():
        line = line.rstrip()
        if not line:
            flush()
            continue
        if line.startswith("N: Name="):
            raw = line[len("N: Name=") :].strip().strip('"')
            name = raw
        elif line.startswith("H: Handlers="):
            handlers = line[len("H: Handlers=") :].strip()
        elif line.startswith("B: KEY="):
            payload = line[len("B: KEY=") :].strip()
            has_key = payload not in ("", "0")
        elif line.startswith("B: REL="):
            payload = line[len("B: REL=") :].strip()
            has_rel = payload not in ("", "0")

    flush()
    return devices


def _glob_event_devices() -> list[str]:
    return sorted(glob.glob("/dev/input/event*"))


def _probe_device(path: str, fallback_name: str = "") -> DeviceInfo:
    info = DeviceInfo(
        path=path,
        name=fallback_name or path,
        score=0,
        caps_summary="",
        accessible=os.access(path, os.R_OK),
        recommended=False,
    )

    if InputDevice is None or ecodes is None:
        return info

    try:
        device = InputDevice(path)
        info.name = device.name or fallback_name or path
        caps, codes = _device_caps(device)
        score, summary = _score_device(info.name, caps, codes)
        info.score = score
        info.caps_summary = summary
        info.accessible = os.access(path, os.R_OK)
        device.close()
    except PermissionError:
        info.error = "permission_denied"
        info.accessible = False
        if info.score <= 0 and fallback_name:
            score, summary = _score_from_proc(fallback_name, "", True, True)
            info.score = score
            info.caps_summary = summary
    except OSError as exc:
        info.error = str(exc)
        info.accessible = False

    if info.score > 0:
        info.recommended = info.score >= 30
    return info


def scan_devices() -> list[DeviceInfo]:
    _require_evdev()

    by_path: dict[str, DeviceInfo] = {}

    evdev_paths: list[str] = []
    if list_devices is not None:
        try:
            evdev_paths = list(list_devices())
        except OSError:
            evdev_paths = []

    if not evdev_paths:
        evdev_paths = _glob_event_devices()

    for path in evdev_paths:
        info = _probe_device(path)
        if info.score > 0:
            by_path[path] = info

    for proc_dev in _parse_proc_devices():
        if proc_dev.path in by_path:
            continue
        info = _probe_device(proc_dev.path, proc_dev.name)
        if info.score <= 0:
            score, summary = _score_from_proc(
                proc_dev.name, proc_dev.handlers, proc_dev.has_key, proc_dev.has_rel
            )
            info.score = score
            info.caps_summary = summary
            info.name = proc_dev.name
            info.recommended = score >= 30
            if not info.accessible and not info.error:
                info.error = "permission_denied"
        if info.score > 0:
            by_path[proc_dev.path] = info

    devices = list(by_path.values())
    devices.sort(key=lambda d: (-d.score, d.path))
    return devices


def find_device_path_by_name(device_name: str) -> str | None:
    """Resolve a stable device name to the current /dev/input/eventN path."""
    wanted = device_name.strip().casefold()
    if not wanted:
        return None

    devices = scan_devices()
    accessible = [d for d in devices if d.accessible and d.name.strip().casefold() == wanted]
    if accessible:
        accessible.sort(key=lambda d: d.score, reverse=True)
        return accessible[0].path

    # Soft match: configured name is a unique substring of the live device name.
    soft = [
        d for d in devices
        if d.accessible and wanted in d.name.casefold()
    ]
    if len(soft) == 1:
        return soft[0].path
    if len(soft) > 1:
        soft.sort(key=lambda d: d.score, reverse=True)
        return soft[0].path
    return None


def choose_device(preferred_path: str = "", preferred_name: str = "") -> str:
    """Pick an input device.

    Preferred path is used when it still exists. If the event node was renumbered
    (common after reboot / Bluetooth reconnect), fall back to preferred_name,
    then to automatic scoring. Never treat a stale event path as fatal when a
    name or auto fallback is available.
    """
    _require_evdev()
    preferred = preferred_path.strip()
    preferred_name = preferred_name.strip()

    if preferred and os.path.exists(preferred):
        if not os.access(preferred, os.R_OK):
            raise DeviceSelectionError(
                "permission_denied",
                f"无法读取设备 {preferred}。请将用户加入 input 组: sudo usermod -aG input $USER，然后重新登录。",
            )
        info = _probe_device(preferred)
        if info.score <= 0:
            proc_match = next((d for d in _parse_proc_devices() if d.path == preferred), None)
            if proc_match:
                score, _summary = _score_from_proc(
                    proc_match.name, proc_match.handlers, proc_match.has_key, proc_match.has_rel
                )
                info.score = score
            if info.score <= 0:
                raise DeviceSelectionError(
                    "not_pointer",
                    f"所选设备不像鼠标/指针设备: {preferred}",
                )
        return preferred

    if preferred and not os.path.exists(preferred):
        by_name = find_device_path_by_name(preferred_name)
        if by_name:
            return by_name

    if preferred_name:
        by_name = find_device_path_by_name(preferred_name)
        if by_name:
            return by_name
        # Name configured but device not present yet (boot / sleep).
        if not preferred:
            raise DeviceSelectionError(
                "device_missing",
                f"尚未找到输入设备: {preferred_name}",
            )

    if preferred and not os.path.exists(preferred):
        # Stale event path and no usable name — try auto rather than dying forever.
        pass

    devices = scan_devices()
    if not devices:
        raise DeviceSelectionError(
            "no_device",
            "未找到鼠标/指针输入设备。请连接鼠标或触摸板后点击「刷新设备」。",
        )

    accessible = [d for d in devices if d.accessible]
    if not accessible:
        names = ", ".join(d.name for d in devices[:3])
        raise DeviceSelectionError(
            "permission_denied",
            f"已识别到输入设备（{names}），但当前用户无 /dev/input 读取权限。"
            "请执行: sudo usermod -aG input $USER，然后注销并重新登录。",
        )

    return accessible[0].path


# --- Trigger parsing and matching (shared by daemon / monitor / capture) ---

BUTTON_ALIAS_GROUPS: tuple[frozenset[str], ...] = (
    frozenset({"BTN_SIDE", "BTN_BACK"}),
)
WHEEL_AXIS_GROUPS: tuple[frozenset[str], ...] = (
    frozenset({"REL_WHEEL", "REL_WHEEL_HI_RES"}),
    frozenset({"REL_HWHEEL", "REL_HWHEEL_HI_RES"}),
)

# Linux high-res wheel: 120 units == one physical detent.
# See Documentation/input/event-codes.rst (REL_WHEEL_HI_RES).
WHEEL_HI_RES_DETENT = 120
# After the first detent of a continuous scroll, suppress further triggers until
# the axis is idle for this long (or the direction reverses).
WHEEL_GESTURE_IDLE_SECONDS = 0.30
HI_RES_WHEEL_AXES = frozenset({"REL_WHEEL_HI_RES", "REL_HWHEEL_HI_RES"})
LEGACY_WHEEL_AXES = frozenset({"REL_WHEEL", "REL_HWHEEL"})
HI_RES_TO_LEGACY = {
    "REL_WHEEL_HI_RES": "REL_WHEEL",
    "REL_HWHEEL_HI_RES": "REL_HWHEEL",
}
LEGACY_TO_HI_RES = {legacy: hi for hi, legacy in HI_RES_TO_LEGACY.items()}


def device_hi_res_wheel_axes(device: Any) -> set[str]:
    """Return HI_RES wheel axis names advertised by the device capabilities."""
    if ecodes is None or device is None:
        return set()
    try:
        rels = set(device.capabilities().get(ecodes.EV_REL, []))
    except Exception:
        return set()
    found: set[str] = set()
    for axis_name in HI_RES_WHEEL_AXES:
        code = getattr(ecodes, axis_name, None)
        if code is not None and code in rels:
            found.add(axis_name)
    return found


def wheel_gesture_key(axis: str) -> str:
    """Collapse legacy/HI_RES companions into one gesture bucket per physical wheel."""
    for group in WHEEL_AXIS_GROUPS:
        if axis in group:
            return next(iter(sorted(group)))
    return axis


class WheelDetentNormalizer:
    """Normalize raw wheel REL events into one trigger per scroll gesture.

    Modern mice emit REL_*_HI_RES in fractions of 120 and often also emit a
    legacy REL_WHEEL/REL_HWHEEL companion. Treating every non-zero sample as a
    trigger makes forward/reverse bindings fire on micro-steps and duplicates,
    which feels unstable. This class:

    1. Accumulates HI_RES values and recognizes ±WHEEL_HI_RES_DETENT (120).
    2. Emits at most one trigger per continuous scroll gesture (same axis /
       direction). Further detents in the same gesture are consumed silently.
    3. Starts a new gesture after WHEEL_GESTURE_IDLE_SECONDS of quiet, or when
       the direction reverses.
    4. Ignores legacy companion events when HI_RES is available/preferred
       (legacy often arrives *before* HI_RES in the same frame).
    """

    def __init__(self, prefer_hi_res: set[str] | None = None) -> None:
        self._accum = {"REL_WHEEL_HI_RES": 0, "REL_HWHEEL_HI_RES": 0}
        self._hi_res_seen: set[str] = set()
        self._prefer_hi_res = set(prefer_hi_res or ())
        # gesture_key -> direction that already fired in the current gesture
        self._gesture_fired: dict[str, str] = {}
        self._gesture_last_activity: dict[str, float] = {}

    def reset(self) -> None:
        for key in self._accum:
            self._accum[key] = 0
        self._hi_res_seen.clear()
        self._gesture_fired.clear()
        self._gesture_last_activity.clear()

    def set_prefer_hi_res(self, axes: set[str]) -> None:
        self._prefer_hi_res = set(axes)
        self.reset()

    def process_event(self, event: Any) -> list[dict[str, str]]:
        if ecodes is None or event.type != ecodes.EV_REL or event.value == 0:
            return []
        return self.process_axis_value(rel_axis_name(event.code), int(event.value))

    def process_axis_value(self, axis: str, value: int) -> list[dict[str, str]]:
        if value == 0:
            return []

        if axis in HI_RES_WHEEL_AXES:
            self._hi_res_seen.add(axis)
            self._accum[axis] = self._accum.get(axis, 0) + value
            return self._drain_detents(axis)

        if axis in LEGACY_WHEEL_AXES:
            hi_res = LEGACY_TO_HI_RES[axis]
            if hi_res in self._prefer_hi_res or hi_res in self._hi_res_seen:
                return []
            direction = "positive" if value > 0 else "negative"
            if self._accept_gesture_trigger(axis, direction):
                return [{"type": "wheel", "axis": axis, "direction": direction}]
            return []

        if "WHEEL" in axis:
            direction = "positive" if value > 0 else "negative"
            if self._accept_gesture_trigger(axis, direction):
                return [{"type": "wheel", "axis": axis, "direction": direction}]
            return []
        return []

    def _expire_gesture_if_idle(self, gesture_key: str, now: float) -> None:
        last = self._gesture_last_activity.get(gesture_key, 0.0)
        if last > 0.0 and (now - last) >= WHEEL_GESTURE_IDLE_SECONDS:
            self._gesture_fired.pop(gesture_key, None)

    def _accept_gesture_trigger(self, axis: str, direction: str) -> bool:
        """Return True once for a continuous scroll; False for later detents."""
        gesture_key = wheel_gesture_key(axis)
        now = time.monotonic()
        self._expire_gesture_if_idle(gesture_key, now)
        self._gesture_last_activity[gesture_key] = now

        fired = self._gesture_fired.get(gesture_key)
        if fired is None or fired != direction:
            self._gesture_fired[gesture_key] = direction
            return True
        return False

    def _drain_detents(self, hi_res_axis: str) -> list[dict[str, str]]:
        triggers: list[dict[str, str]] = []
        acc = self._accum[hi_res_axis]
        while abs(acc) >= WHEEL_HI_RES_DETENT:
            direction = "positive" if acc > 0 else "negative"
            acc -= WHEEL_HI_RES_DETENT if acc > 0 else -WHEEL_HI_RES_DETENT
            # Consume every detent so accumulation does not backlog, but only
            # the first detent of this gesture becomes a trigger.
            if self._accept_gesture_trigger(hi_res_axis, direction):
                triggers.append(
                    {"type": "wheel", "axis": hi_res_axis, "direction": direction}
                )
        self._accum[hi_res_axis] = acc
        return triggers


def button_code_name(code: int) -> str | None:
    if ecodes is None:
        return None
    name = ecodes.BTN.get(code)
    if isinstance(name, str) and name.startswith("BTN_"):
        return name
    if isinstance(name, (list, tuple)):
        for candidate in name:
            if isinstance(candidate, str) and candidate.startswith("BTN_"):
                return candidate
    return None


def button_codes_equivalent(binding_code: str, event_code: str) -> bool:
    if binding_code == event_code:
        return True
    for group in BUTTON_ALIAS_GROUPS:
        if binding_code in group and event_code in group:
            return True
    return False


def rel_axis_name(code: int) -> str:
    if ecodes is None:
        return f"REL_{code}"
    name = ecodes.REL.get(code, f"REL_{code}")
    if isinstance(name, str):
        return name
    if isinstance(name, (list, tuple)) and name:
        return str(name[0])
    return f"REL_{code}"


def wheel_axes_equivalent(binding_axis: str, event_axis: str) -> bool:
    if binding_axis == event_axis:
        return True
    for group in WHEEL_AXIS_GROUPS:
        if binding_axis in group and event_axis in group:
            return True
    return False


def event_to_trigger(event: Any) -> dict[str, str] | None:
    if ecodes is None:
        return None

    if event.type == ecodes.EV_KEY and event.value == 1:
        code_name = button_code_name(event.code)
        if code_name:
            return {"type": "mouse_button", "code": code_name}
    elif event.type == ecodes.EV_REL and event.value != 0:
        axis_name = rel_axis_name(event.code)
        direction = "positive" if event.value > 0 else "negative"
        return {"type": "wheel", "axis": axis_name, "direction": direction}
    return None


def triggers_match(binding_trigger: dict[str, Any], event_trigger: dict[str, Any]) -> bool:
    if binding_trigger.get("type") != event_trigger.get("type"):
        return False
    if binding_trigger.get("type") == "mouse_button":
        binding_code = str(binding_trigger.get("code", ""))
        event_code = str(event_trigger.get("code", ""))
        return button_codes_equivalent(binding_code, event_code)
    if binding_trigger.get("type") == "wheel":
        return (
            wheel_axes_equivalent(
                str(binding_trigger.get("axis", "")),
                str(event_trigger.get("axis", "")),
            )
            and str(binding_trigger.get("direction")) == str(event_trigger.get("direction"))
        )
    return False


def format_capture_error(stderr_text: str) -> str:
    text = stderr_text.strip()
    if not text:
        return "录制失败"
    if "no mouse device found" in text or "no_device" in text:
        return "未找到鼠标设备。请先在上方选择输入设备，或点击「刷新设备」。"
    if "permission" in text.lower() or "permission_denied" in text:
        return "无 /dev/input 读取权限。请执行: sudo usermod -aG input $USER，然后重新登录。"
    if text.startswith("ERROR:"):
        payload = text[6:].strip()
        colon = payload.find(":")
        if colon > 0:
            return payload[colon + 1 :].strip()
        return payload
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description="Input device utilities")
    parser.add_argument("--list-json", action="store_true", help="Print scanned devices as JSON array")
    parser.add_argument("--choose", metavar="PATH", default="", help="Choose device path (empty = auto)")
    args = parser.parse_args()

    try:
        if args.list_json:
            print(json.dumps([d.to_dict() for d in scan_devices()], ensure_ascii=False), flush=True)
            return 0
        path = choose_device(args.choose)
        print(path, flush=True)
        return 0
    except DeviceSelectionError as exc:
        print(f"ERROR:{exc.code}:{exc}", file=sys.stderr, flush=True)
        return 1
    except Exception as exc:
        print(f"ERROR:unknown:{exc}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
