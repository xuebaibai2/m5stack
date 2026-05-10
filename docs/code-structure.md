# Code Structure

This project is a PlatformIO Arduino C++ app for M5Stack StickS3 using
M5Unified and M5GFX.

## Main Files

```text
.
├── platformio.ini
├── .env.example
├── scripts
│   └── generate_config.py
├── src
│   ├── main.cpp
│   ├── sensor_app.cpp
│   ├── sensor_app.h
│   ├── stick_link_protocol.h
│   ├── weather_app.cpp
│   ├── weather_app.h
│   └── generated_config.h
├── mac
│   └── StickLinkMenuBar
│       ├── Package.swift
│       ├── config
│       │   └── sticklink.json
│       └── Sources
│           ├── StickLinkMenuBar
│           ├── StickLinkMenuBarApp
│           └── StickLinkValidation
└── docs
    ├── bluetooth-protocol.md
    ├── code-structure.md
    ├── sensor-app-bluetooth.md
    └── upload-instructions.md
```

`src/generated_config.h` is generated during build and is ignored by git.
Do not edit it directly.

## Configuration

Local config lives in `.env`.

Start from the example:

```bash
cp .env.example .env
```

Then edit `.env`:

```dotenv
WIFI_SSID=your_wifi_name
WIFI_PASSWORD=your_wifi_password
WEATHER_LATITUDE=-33.8688
WEATHER_LONGITUDE=151.2093
WEATHER_TIMEZONE=Australia/Sydney
```

How config reaches C++:

1. `platformio.ini` runs `scripts/generate_config.py` before build.
2. The script reads `.env`.
3. The script writes `src/generated_config.h`.
4. `src/weather_app.cpp` includes `generated_config.h`.

Upload is blocked if `WIFI_SSID` or `WIFI_PASSWORD` are missing or still set to
placeholder values. Compile checks with `pio run` still work.

## Launcher

Launcher logic is in `src/main.cpp`.

Important sections:

- `kApps`: app names shown in the menu.
- `drawMenu()`: menu screen drawing.
- `handleMenuInput()`: Button B cycles apps, Button A launches.
- `handleAppInput()`: long press A or B returns to menu.
- `setup()`: initializes Serial, M5, display, starts Wi-Fi, then draws menu.
- `loop()`: calls `M5.update()` and routes input/update logic by screen state.

App positions are based on the `kApps` array order in `src/main.cpp`:

```cpp
const AppDefinition kApps[] = {
    {"Weather App"},   // first app, index 0
    {"Sensor App"},    // second app, index 1
    {"Settings App"},  // third app, index 2
};
```

The selected index is stored in `selectedApp`. When Button A launches an app,
`launchSelectedApp()` copies it into `runningApp`.

Current app handling in `drawApp()`:

```cpp
void drawApp(size_t appIndex) {
  if (appIndex == 0) {
    weatherAppStart();
    return;
  }

  if (appIndex == 1) {
    sensorAppStart();
    return;
  }

  drawPlaceholderApp(appIndex);
}
```

So:

- First app: `appIndex == 0`, implemented by `weather_app.cpp`.
- Second app: `appIndex == 1`, implemented by `sensor_app.cpp`.
- Third app: `appIndex == 2`, currently uses placeholder drawing.

Weather App and Sensor App also have update/input handling in `loop()` and
`handleAppInput()` by checking `runningApp == 0` or `runningApp == 1`.

To add another app:

1. Add its name to `kApps`.
2. Add a branch in `drawApp()`.
3. Add update/input handling in `loop()` or a small module like `weather_app`.

## Sensor App

Sensor app public functions are declared in `src/sensor_app.h`.

```cpp
void sensorAppBegin();
void sensorAppStart();
void sensorAppUpdate();
void sensorAppSendButtonA();
void sensorAppStop();
bool sensorAppConnected();
```

Responsibilities:

- `sensorAppBegin()`: starts BLE service and advertising during boot.
- `sensorAppStart()`: enters the Sensor App and draws BLE status.
- `sensorAppUpdate()`: refreshes the Sensor App status display when needed.
- `sensorAppSendButtonA()`: sends a BLE JSON event for Button A.
- `sensorAppStop()`: leaves Sensor App screen state while BLE remains available.
- `sensorAppConnected()`: exposes BLE connection state for UI/status logic.

The StickS3 acts as a BLE peripheral named `StickS3 Link`. It advertises the
custom Stick Link service from `src/stick_link_protocol.h`. The Mac app connects
as a CoreBluetooth central and subscribes to the message characteristic.

Current Button A payload:

```json
{
  "v": 1,
  "id": "000001",
  "app": "sensor",
  "type": "button",
  "name": "ButtonA",
  "text": "ButtonA pressed from Sensor App",
  "ts_ms": 123456,
  "seq": 1
}
```

The BLE protocol, UUIDs, and extension rules are documented in
`docs/bluetooth-protocol.md`.

## Stick Link Protocol

Shared firmware protocol constants and JSON encoding live in
`src/stick_link_protocol.h`.

Important constants:

- `kStickLinkBleName`
- `kStickLinkServiceUuid`
- `kStickLinkMessageCharacteristicUuid`
- `kStickLinkDeviceInfoCharacteristicUuid`

Message helpers:

- `stickLinkEncodeEvent(...)`
- `stickLinkEncodeButtonEvent(...)`

Keep the protocol generic by using the envelope fields `app`, `type`, and
`name`. Future voice-trigger or audio metadata events should add new message
types; raw audio should use a separate characteristic or chunking protocol.

## Weather App

Weather app public functions are declared in `src/weather_app.h`.

```cpp
void weatherAppBegin();
void weatherAppStart();
void weatherAppUpdate();
void weatherAppRefresh();
void weatherAppStop();
bool weatherAppWifiConnected();
```

Responsibilities:

- `weatherAppBegin()`: starts Wi-Fi early during boot.
- `weatherAppStart()`: enters the weather app and starts fetch flow.
- `weatherAppUpdate()`: advances Wi-Fi connection, fetch, error, ready, and animation states.
- `weatherAppRefresh()`: refreshes weather on short Button A press.
- `weatherAppStop()`: resets weather state when returning to menu.
- `weatherAppWifiConnected()`: used by the launcher Wi-Fi indicator.

The weather app uses Open-Meteo. The URL is assembled from generated config in
`weatherUrl()`, so location changes should be made in `.env`, not in source.

## Weather State Flow

Weather app state is kept in `WeatherState`:

```cpp
Idle -> Connecting -> Fetching -> Ready
                    -> Offline
                    -> Error
```

Normal path:

1. Wi-Fi starts at boot through `weatherAppBegin()`.
2. Opening Weather App calls `weatherAppStart()`.
3. If Wi-Fi is connected, weather fetch starts immediately.
4. If Wi-Fi is still connecting, the app shows a connecting state.
5. On successful API response, the app renders weather and animates the icon.

Failure handling:

- Missing Wi-Fi config shows setup message.
- Wi-Fi timeout shows offline message.
- HTTP or JSON failure shows a weather error with diagnostic text.
- The app keeps running and long-press menu return still works.

## Rendering Pattern

The full weather screen is drawn only when state or data changes.

For icon animation, only the icon rectangle is cleared and redrawn:

```cpp
M5.Display.fillRect(kIconBoxX, kIconBoxY, kIconBoxW, kIconBoxH, TFT_BLACK);
drawWeatherIcon(weather.icon, kIconCenterX, kIconCenterY, animationFrame);
```

Keep animated drawing inside that icon box. Static text should stay outside the
box so it does not flicker.

## Button Behavior

Menu:

- Button B: cycle app selection.
- Button A: launch selected app.

Weather App:

- Short Button A press: refresh weather.
- Long Button A press: return to menu.
- Long Button B press: return to menu.

Sensor App:

- Short Button A press: send BLE event to connected Mac.
- Long Button A press: return to menu.
- Long Button B press: return to menu.

Other apps:

- Long Button A or B returns to menu.

## Mac Companion App

The native SwiftUI macOS companion app lives in `mac/StickLinkMenuBar/`. It is
kept separate from the firmware root so it can be moved later.

Important files:

- `Package.swift`: Swift package definition.
- `config/sticklink.json`: runtime BLE/config filters.
- `Sources/StickLinkMenuBar/Protocol/StickMessage.swift`: v1 JSON message model.
- `Sources/StickLinkMenuBar/Configuration/StickLinkConfig.swift`: runtime config.
- `Sources/StickLinkMenuBar/Models/LogStore.swift`: in-memory log storage.
- `Sources/StickLinkMenuBar/Bluetooth/StickBluetoothClient.swift`:
  CoreBluetooth central implementation.
- `Sources/StickLinkMenuBar/Views/`: SwiftUI status, logs, and config views.
- `Sources/StickLinkMenuBarApp/main.swift`: menu bar app entry point.
- `Sources/StickLinkValidation/main.swift`: local validation executable.

Run locally:

```bash
cd mac/StickLinkMenuBar
swift run StickLinkMenuBar
```

Validate Mac-side protocol/config/log behavior:

```bash
cd mac/StickLinkMenuBar
swift build
swift run StickLinkValidation
```

## Common Changes

Change Wi-Fi:

- Edit `.env`.
- Run `pio run`.
- Upload again.

Change weather location:

- Edit `WEATHER_LATITUDE`, `WEATHER_LONGITUDE`, and `WEATHER_TIMEZONE` in `.env`.

Change app menu text:

- Edit `kApps` in `src/main.cpp`.

Change Weather App UI:

- Edit drawing functions in `src/weather_app.cpp`.
- Use full-screen redraw only for state/data changes.
- Use partial-region redraw for animation.

Change Sensor App BLE event behavior:

- Edit `src/sensor_app.cpp`.
- Keep JSON envelope construction in `src/stick_link_protocol.h`.
- Update `docs/bluetooth-protocol.md` if UUIDs or message schema change.

Change Mac app runtime filters:

- Edit `mac/StickLinkMenuBar/config/sticklink.json`.
- Use the in-app Reload Config action or restart the app.

Change weather icon mapping:

- Edit `iconForWeather()` and `conditionForWeather()` in `src/weather_app.cpp`.

Change API fields:

- Edit `weatherUrl()` and `fetchWeather()` in `src/weather_app.cpp`.

## Build And Upload

Compile:

```bash
pio run
```

Validate Mac app:

```bash
cd mac/StickLinkMenuBar
swift build
swift run StickLinkValidation
```

Upload after setting `.env` and putting StickS3 into download mode:

```bash
pio run -t upload --upload-port /dev/cu.usbmodem101
```

Open serial monitor:

```bash
pio device monitor -p /dev/cu.usbmodem101 -b 115200
```
