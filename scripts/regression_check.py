#!/usr/bin/env python3
"""Static regression guardrails for the StickS3 launcher integration.

These checks cover behavior that cannot be fully exercised without a physical
StickS3 and macOS Bluetooth session. They intentionally assert the code paths
that previously regressed Weather, Remote Mic reconnect, repeat recording, and
CodeBuddy BLE discovery.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b(?:void|bool|String)\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
    require(match is not None, f"{name}() was not found")
    start = match.end()
    depth = 1
    index = start
    while index < len(source) and depth > 0:
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        index += 1
    require(depth == 0, f"{name}() body could not be parsed")
    return source[start : index - 1]


def test_remote_mic_ble_advertisement() -> None:
    shared_ble = read("src/shared_ble.cpp")
    remote_mic = read("src/remote_mic_app.cpp")
    mac_client = read(
        "mac/StickLinkMenuBar/Sources/StickLinkMenuBar/Bluetooth/StickBluetoothClient.swift"
    )

    require(
        "advertisement.setName(deviceName);" in shared_ble
        and "scanResponse.setCompleteServices(BLEUUID(serviceUuid));" in shared_ble,
        "normal Stick Link mode must advertise StickS3 Link in the primary advertisement",
    )
    require(
        "advertisement.setCompleteServices(BLEUUID(serviceUuid));" in shared_ble
        and "scanResponse.setName(deviceName);" in shared_ble,
        "CodeBuddy mode must keep service UUID primary for CodeBuddy host discovery",
    )

    start_body = function_body(remote_mic, "remoteMicAppStart")
    require(
        "sharedBleAdvertisementMatches(kSharedBleDeviceName" in start_body,
        "Remote Mic app start must check current BLE advertisement mode",
    )
    require(
        "sharedBleHandoffToAdvertisement(kSharedBleDeviceName, kStickLinkServiceUuid)" in start_body,
        "Remote Mic app start must recover Stick Link BLE mode after CodeBuddy",
    )

    require(
        "advertisedServices.contains(CBUUID(string: config.serviceUUID))" in mac_client,
        "Mac scanner must accept Stick Link service UUID as a discovery fallback",
    )


def test_remote_mic_repeat_recording() -> None:
    remote_mic = read("src/remote_mic_app.cpp")
    start_body = function_body(remote_mic, "remoteMicAppStartRecording")
    stop_body = function_body(remote_mic, "remoteMicAppStopRecording")
    app_stop_body = function_body(remote_mic, "remoteMicAppStop")

    require(
        start_body.find("resetCapturePipeline();") < start_body.find("recording = true;"),
        "recording start must reset capture pipeline before setting recording=true",
    )
    require(
        start_body.find("startRemoteMicInput();") < start_body.find("recording = true;"),
        "recording start must restart mic input before setting recording=true",
    )
    require(
        "if (!recording)" in stop_body and "return;" in stop_body,
        "recording stop must be idempotent so stale releases do not poison later sessions",
    )
    require(
        "remoteMicAppStopRecording();" in app_stop_body
        and "stopRemoteMicInput();" in app_stop_body,
        "leaving Remote Mic must stop any active recording and release mic input",
    )


def test_weather_code_path() -> None:
    weather = read("src/weather_app.cpp")
    code_buddy = read("src/code_buddy_app.cpp")

    fetch_body = function_body(weather, "fetchWeather")
    require("WiFiClientSecure client;" in fetch_body, "weather must use TLS client")
    require("client.setInsecure();" in fetch_body, "weather TLS mode must be explicitly configured")
    require("http.setTimeout(kHttpTimeoutMs);" in fetch_body, "weather HTTP request must have a timeout")
    require("http.GET()" in fetch_body, "weather must perform the Open-Meteo HTTP GET")
    require("HTTP_CODE_OK" in fetch_body, "weather must reject non-200 responses explicitly")
    require("deserializeJson(doc, payload)" in fetch_body, "weather must parse JSON response")
    require("Missing current" in fetch_body, "weather parser must guard missing current data")

    stop_body = function_body(code_buddy, "codeBuddyAppStop")
    require(
        "releaseRuntime();" in stop_body,
        "CodeBuddy stop must release heavy runtime so Weather has heap for HTTPS",
    )
    release_body = function_body(code_buddy, "releaseRuntime")
    require(
        "spr.deleteSprite();" in release_body and "characterClose();" in release_body,
        "CodeBuddy runtime release must free sprite and GIF state",
    )


def main() -> int:
    tests = [
        test_remote_mic_ble_advertisement,
        test_remote_mic_repeat_recording,
        test_weather_code_path,
    ]
    for test in tests:
        test()
        print(f"PASS {test.__name__}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
