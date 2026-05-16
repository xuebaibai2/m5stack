#!/usr/bin/env python3
"""Run upstream CodeBuddy with this firmware's shared BLE device name.

The upstream host accepts devices named ``Codex-*`` or devices whose Nordic UART
Service UUID is visible in the advertisement. This firmware advertises as
``StickS3 Link`` so the existing Remote Mic Mac app can still find it. On macOS,
the advertisement may not reliably carry both 128-bit service UUIDs, so the
runner accepts the StickS3 name prefix before delegating to CodeBuddy. Current
firmware also advertises as ``Codex-StickS3`` while the third app is open, which
matches upstream discovery directly.
"""

from __future__ import annotations

import asyncio
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from codex_buddy import ble_transport
from codex_buddy.cli import main


_original_matches_buddy_discovery = ble_transport._matches_buddy_discovery
_original_discover = ble_transport.BleBuddyTransport.discover.__func__
_saw_discovery_event = False


def _matches_shared_sticks3_name(name: object) -> bool:
    return str(name or "").strip().startswith("StickS3")


def _matches_buddy_discovery(payload: dict) -> bool:
    return (
        _original_matches_buddy_discovery(payload)
        or _matches_shared_sticks3_name(payload.get("name"))
        or _matches_shared_sticks3_name(payload.get("local_name"))
    )


def _debug_enabled() -> bool:
    return os.environ.get("CODE_BUDDY_DEBUG_DISCOVERY", "1") != "0"


def _debug_event(payload: dict) -> None:
    if not _debug_enabled():
        return

    event = payload.get("event", "")
    if event == "discovered":
        global _saw_discovery_event
        _saw_discovery_event = True
        print(
            "[code-buddy] discovered "
            f"name={payload.get('name', '')!r} "
            f"local_name={payload.get('local_name', '')!r} "
            f"identifier={payload.get('identifier', '')!r} "
            f"rssi={payload.get('rssi', '')!r} "
            f"services={payload.get('service_uuids', [])!r}",
            file=sys.stderr,
        )
    elif event == "central_state":
        print(f"[code-buddy] central_state={payload.get('value')!r}", file=sys.stderr)
    elif event == "scan_started":
        print("[code-buddy] scan_started", file=sys.stderr)
    elif event == "error":
        print(f"[code-buddy] error={payload.get('message', payload)!r}", file=sys.stderr)
    elif event:
        print(f"[code-buddy] event={event!r} payload={payload!r}", file=sys.stderr)


def _discover_with_native_helper_compat(timeout: float) -> list[ble_transport.DiscoveredBuddy]:
    try:
        ble_transport._terminate_native_helper_processes(timeout=1.5)
        if _debug_enabled():
            print("[code-buddy] stale native helpers terminated", file=sys.stderr)
    except Exception as exc:
        if _debug_enabled():
            print(f"[code-buddy] stale helper cleanup skipped: {exc}", file=sys.stderr)

    session_dir = Path(tempfile.mkdtemp(prefix="codebuddy-ble-discover-"))
    commands_dir = session_dir / "commands"
    events_path = session_dir / "events.jsonl"
    commands_dir.mkdir(parents=True, exist_ok=True)
    events_path.write_text("")

    discovered: dict[str, ble_transport.DiscoveredBuddy] = {}
    buffer = bytearray()
    event_count = 0

    try:
        try:
            subprocess.run(
                ble_transport._native_helper_open_command(
                    app_path=ble_transport._native_helper_app_path(),
                    session_dir=session_dir,
                    device_id="__SCAN_ONLY__",
                    device_name="",
                ),
                check=True,
                capture_output=True,
                text=True,
            )
        except subprocess.CalledProcessError as exc:
            print(
                "[code-buddy] native helper launch failed "
                f"rc={exc.returncode} stdout={exc.stdout!r} stderr={exc.stderr!r}",
                file=sys.stderr,
            )
            return []

        deadline = time.monotonic() + timeout
        offset = 0
        while time.monotonic() < deadline:
            if events_path.exists():
                with events_path.open("rb") as handle:
                    handle.seek(offset)
                    chunk = handle.read()
                if chunk:
                    offset += len(chunk)
                    buffer.extend(chunk)
                    while b"\n" in buffer:
                        line, _, rest = buffer.partition(b"\n")
                        buffer = bytearray(rest)
                        if not line:
                            continue
                        try:
                            payload = json.loads(line.decode("utf-8"))
                        except json.JSONDecodeError:
                            continue
                        event_count += 1
                        _debug_event(payload)
                        if payload.get("event") != "discovered" or not _matches_buddy_discovery(payload):
                            continue
                        device_id = str(payload.get("identifier", "")).strip()
                        name = str(payload.get("name", "")).strip() or device_id
                        if device_id:
                            discovered[device_id] = ble_transport.DiscoveredBuddy(
                                device_id=device_id,
                                name=name,
                            )
            time.sleep(0.05)
    finally:
        try:
            ble_transport._terminate_native_helper_processes(session_dir=session_dir)
        except Exception as exc:
            if _debug_enabled():
                print(f"[code-buddy] helper cleanup skipped: {exc}", file=sys.stderr)
        shutil.rmtree(session_dir, ignore_errors=True)

    if _debug_enabled() and event_count == 0:
        print("[code-buddy] no native helper scan events received", file=sys.stderr)
    elif _debug_enabled() and not _saw_discovery_event:
        print("[code-buddy] scan completed but no BLE advertisements were reported", file=sys.stderr)
        print(
            "[code-buddy] next check: run ./monitor.sh, launch CodeBuddy on the "
            "StickS3, and confirm serial shows [codebuddy] start app plus "
            "[ble] configured advertisement name='Codex-StickS3'",
            file=sys.stderr,
        )

    return sorted(discovered.values(), key=lambda item: item.name)


async def _discover(cls, *, timeout: float = 4.0):
    matches = await _original_discover(cls, timeout=timeout)
    if matches or ble_transport._default_use_native_helper():
        return matches

    _, scanner = ble_transport._require_bleak()
    discovered = await scanner.discover(timeout=timeout, return_adv=True)
    by_id = {match.device_id: match for match in matches}
    for _, (device, adv) in discovered.items():
        name = device.name or adv.local_name or ""
        if _matches_shared_sticks3_name(name):
            by_id[device.address] = ble_transport.DiscoveredBuddy(
                device_id=device.address,
                name=name or device.address,
            )
    return sorted(by_id.values(), key=lambda item: item.name)


ble_transport._matches_buddy_discovery = _matches_buddy_discovery
ble_transport._discover_with_native_helper = _discover_with_native_helper_compat
ble_transport.BleBuddyTransport.discover = classmethod(_discover)

_original_argument_parser_parse_args = argparse.ArgumentParser.parse_args


def _parse_args_with_longer_default_timeout(self, args=None, namespace=None):
    parsed = _original_argument_parser_parse_args(self, args, namespace)
    if getattr(parsed, "timeout", None) == 4.0:
        parsed.timeout = float(os.environ.get("CODE_BUDDY_DISCOVERY_TIMEOUT", "12"))
    return parsed


argparse.ArgumentParser.parse_args = _parse_args_with_longer_default_timeout


if __name__ == "__main__":
    sys.exit(main())
