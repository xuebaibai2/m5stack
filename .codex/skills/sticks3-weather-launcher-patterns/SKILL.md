---
name: sticks3-weather-launcher-patterns
description: Use when working on M5Stack StickS3 launcher/weather app code that needs .env-based configuration, boot-time Wi-Fi, Open-Meteo fetching, button mappings, and partial redraw animation on M5Unified/M5GFX displays.
---

# StickS3 Weather Launcher Patterns

Use this skill when editing the StickS3 launcher or weather app.

## Config

- Keep secrets and site settings in `.env`.
- Generate `src/generated_config.h` from `.env` before build.
- Treat `WIFI_SSID` and `WIFI_PASSWORD` as required for upload.
- Keep weather location settings configurable, not hard-coded.

## Boot Flow

- Start Wi-Fi as early as possible after `M5.begin()`.
- Do not break the app if Wi-Fi is unavailable.
- Show a visible Wi-Fi connected indicator when connected.

## App Behavior

- Launcher menu uses larger, readable option text.
- Button B cycles apps.
- Button A launches the selected app.
- In the weather app, short press Button A refreshes data.
- Long press Button A or Button B returns to the menu.

## Weather Fetching

- Use Open-Meteo without an API key when only current weather is needed.
- Request only the fields required by the UI.
- Parse the response defensively and surface the actual failure reason.
- Keep the weather screen usable in offline or error states.

## Rendering

- Draw the full weather screen only when the state changes.
- Animate only the icon region after the first render.
- Keep text and static widgets outside the animated box.
- Redraw the smallest possible rectangle when animating moving content.

## M5Unified / M5GFX

- Call `M5.update()` in `loop()`.
- Use `M5.Display` for all UI drawing.
- Prefer small functions and separate modules for launcher and weather logic.
