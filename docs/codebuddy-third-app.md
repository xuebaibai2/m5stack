# CodeBuddy Third App Integration Notes

## Goal

Add CodeBuddy as a third StickS3 launcher app while keeping Weather and Remote
Mic launchable from the existing menu. CodeBuddy uses the firmware under
`external/CodeBuddy/firmware` as the source reference, but runs inside this
PlatformIO firmware image rather than as a separate flashed image.

## Existing Implementation Constraints

- `docs/code-structure.md` defines the launcher extension points:
  - app names live in `src/main.cpp` `kApps`
  - app launch routing is in `drawApp()`
  - app update/input routing is in `loop()` and `handleAppInput()`
- `docs/bluetooth-protocol.md` defines the existing Stick Link BLE service used
  by Remote Mic and weather configuration.
- `docs/remote-mic-bluetooth.md` documents Remote Mic as launcher app index `1`
  and says BLE is initialized at boot with the `StickS3 Link` advertising name.
- CodeBuddy's firmware uses Nordic UART Service UUIDs and advertises a name
  starting with `Codex`.
- CodeBuddy's upstream firmware is a complete Arduino firmware. Its
  `main.cpp` owns `setup()`, `loop()`, display rotation, BLE init, LittleFS,
  sprite rendering, stats/settings, and button behavior. The launcher port
  replaces those ownership points with `codeBuddyAppBegin()`,
  `codeBuddyAppStart()`, `codeBuddyAppUpdate()`, and `codeBuddyAppStop()`.

## Integration Direction

Use one shared BLE server at boot and attach both services to it:

- Stick Link service for Remote Mic and weather commands.
- Nordic UART Service for CodeBuddy host integration.

Remote Mic and CodeBuddy must not call `BLEDevice::init()` independently. The
shared server owns advertising and connection callbacks, while app modules own
their app-specific characteristics and message parsing.

## Running Log

- Read official M5Stack StickS3 and M5Unified docs before analysis.
- Verified `external/CodeBuddy/firmware` builds successfully with `pio run`.
- Checkpointed existing repo changes in commit `3ebdd7e`.
- Started implementation with a testable CodeBuddy protocol/state layer.
- Added `test/test_code_buddy_protocol`; first run failed because
  `code_buddy_protocol.h` did not exist.
- Added `src/code_buddy_protocol.h`; focused `pio test` advanced past compile
  and failed at upload because embedded tests require a connected StickS3.
- Added `src/shared_ble.*` as the single BLE owner. It initializes
  `BLEDevice`, creates one `BLEServer`, tracks connection state, restarts
  advertising after disconnects, and lets app modules add advertised services.
- Refactored Remote Mic to attach its existing Stick Link service to the shared
  BLE server. Its audio capture, message schema, weather command handling, and
  launcher behavior remain unchanged.
- Added `src/code_buddy_app.*` as launcher app index `2`. It attaches the
  CodeBuddy Nordic UART Service to the shared BLE server, parses newline
  delimited heartbeat JSON, renders a compact status/prompt screen, and sends
  CodeBuddy permission decisions.
- Extended CodeBuddy BLE support to the full documented firmware-side wire
  protocol: time sync, `owner`, `name`, `status`, `unpair`, and folder-push
  commands.
- Added LittleFS as the PlatformIO filesystem setting because CodeBuddy
  folder-push support stores assets under `/codebuddy/characters`.
- Copied the upstream CodeBuddy runtime sources into `src/codebuddy_runtime/`:
  board compatibility, ASCII buddy renderer, all buddy species, GIF character
  renderer, persona logic, UTF-8 helpers, clock helpers, about info, and
  stats/preferences.
- Added the upstream `AnimatedGIF` dependency and include path in
  `platformio.ini`.
- Reworked the third app to initialize the upstream runtime:
  - 135x240 sprite canvas
  - portrait display while CodeBuddy is open
  - ASCII buddy species from upstream
  - GIF character loading from LittleFS `/characters`
  - upstream stats/settings/name/owner persistence
  - CodeBuddy HUD and approval overlay
- CodeBuddy returns the display to launcher landscape rotation on stop.
- After hardware feedback, the shared BLE name was restored to `StickS3 Link`
  because the existing macOS Remote Mic companion filters discovered devices by
  the `StickS3` prefix.
- CodeBuddy app buttons now interact when no prompt is pending:
  - Button A cycles Home, Pet, and Info views.
  - Button B changes ASCII pet on Home, advances Pet page, or advances Info
    page.
  - During a pending prompt, Button A still approves and Button B denies.
- Replaced the placeholder third launcher item with `CodeBuddy`.
- The shared BLE advertised device name is `StickS3 Link`. This preserves the
  existing macOS companion app's `StickS3` name-prefix filter.
- Updated the macOS Stick Link scanner to discover by `StickS3` name prefix
  first, then discover the Stick Link service after connection. This keeps
  Remote Mic connectable when the shared firmware exposes both Stick Link and
  CodeBuddy BLE services and the advertisement cannot reliably carry every
  128-bit service UUID.
- Moved heavy CodeBuddy runtime initialization from boot to CodeBuddy launch.
  Boot now registers the BLE service and loads small preferences only; sprite,
  GIF character, stats/settings, and runtime render state initialize when the
  third app is opened. This keeps heap available for Weather TLS requests and
  avoids `HTTP status -1` failures caused by early CodeBuddy sprite allocation.
- Guarded Remote Mic stop handling so a Button A release only stops recording
  if the matching Button A press was observed while Remote Mic was active.
- Local CodeBuddy venv note: this workspace's Python 3.13 venv did not process
  the editable-install `.pth`, and the venv also lacks `setuptools` for an
  offline non-editable reinstall. The local workaround was to link
  `external/CodeBuddy/src/codex_buddy` into the venv `site-packages`; after
  that `.venv/bin/code-buddy --version` reports `0.1.4`.
- Added `run-code-buddy.sh` and `scripts/run_code_buddy_compat.py` so the local
  CodeBuddy host accepts this firmware's `StickS3 Link` name. The wrapper
  defaults `CODEX_BUDDY_BLE_BACKEND=bleak` because Bleak exposes advertisement
  local names, while the upstream native helper can miss the shared firmware
  name before connection.
- CodeBuddy app launch now switches the BLE advertised name to `Codex-StickS3`,
  matching the upstream host's `Codex-*` discovery filter. Leaving CodeBuddy
  switches the name back to `StickS3 Link` for the existing Remote Mic app.
- BLE handoff is active: switching into CodeBuddy disconnects any currently
  connected central before advertising as `Codex-StickS3`; switching back to
  the launcher disconnects again before advertising as `StickS3 Link`. This
  lets the existing Remote Mic app and CodeBuddy host reconnect to the intended
  mode instead of sharing a stale central connection.
- Advertisement payloads are mode-specific. Normal launcher/Remote Mic mode
  advertises only the Stick Link service UUID with `StickS3 Link` in scan
  response. CodeBuddy mode advertises only the Nordic UART Service UUID with
  `Codex-StickS3` in scan response. Serial logs now print `[ble] configured
  advertisement ...`, `[ble] handoff ...`, and `[ble] start advertising` to
  diagnose discovery failures from a monitor transcript.
- First-run optional NVS keys are checked before reads so missing CodeBuddy
  stats/owner/name preferences do not emit noisy `NOT_FOUND` logs.
- `run-code-buddy.sh` prints discovered BLE names and service UUIDs by default
  during CodeBuddy discovery. Set `CODE_BUDDY_DEBUG_DISCOVERY=0` to silence it.
- `run-code-buddy.sh` now terminates stale `CodeBuddyBLEHelper` processes
  before each discovery scan. A stale helper can keep the StickS3 connected,
  which prevents the peripheral from advertising and produces a scan with no
  discovered BLE advertisements.
- When the runner sees scan events but no discovered advertisements, it now
  points the next check at `./monitor.sh` and the expected CodeBuddy BLE serial
  markers.
- Added firmware serial markers for CodeBuddy service setup, app start, and app
  stop: `[codebuddy] begin service setup`, `[codebuddy] NUS service ready`,
  `[codebuddy] start app`, and `[codebuddy] stop app`.
- Moved CodeBuddy RX command processing out of the BLE `onWrite` callback and
  into `codeBuddyAppUpdate()`. The callback now only queues bytes; JSON
  parsing, Preferences writes, acks, and UI changes run from the main Arduino
  loop to avoid overflowing the ESP32 BLE host task stack.
- Added a short settle delay after raw BLE advertisement reconfiguration before
  restarting advertising. This avoids scans briefly seeing the new CodeBuddy
  name with the previous Stick Link service UUID.
- `pio run` passed after the shared BLE and CodeBuddy app wiring.
- `pio test -e m5stack-sticks3 -f test_code_buddy_protocol --without-uploading`
  built the test firmware but still entered PlatformIO's embedded serial test
  runner and waited for hardware, so it was stopped.

## Implemented Files

- `src/shared_ble.h` / `src/shared_ble.cpp`: shared BLE server ownership.
- `src/code_buddy_protocol.h`: testable CodeBuddy heartbeat parser and
  permission/ack command encoders plus transfer path validation.
- `src/code_buddy_app.h` / `src/code_buddy_app.cpp`: CodeBuddy app lifecycle,
  BLE NUS service, upstream runtime initialization, sprite rendering, approval
  actions, command acks, time sync, status response, owner/name persistence,
  unpair, and LittleFS folder-push storage.
- `src/codebuddy_runtime/`: copied and lightly integrated upstream CodeBuddy
  runtime source files.
- `test/test_code_buddy_protocol/test_main.cpp`: parser/encoder tests.
- `src/main.cpp`: launcher entry, app lifecycle routing, and Button A/B
  approve/deny routing for app index `2`.
- `src/remote_mic_app.cpp`: minimal BLE ownership refactor onto `shared_ble`.
- `mac/StickLinkMenuBar/Sources/StickLinkMenuBar/Bluetooth/StickBluetoothClient.swift`:
  name-prefix BLE discovery for Remote Mic compatibility with the shared BLE
  advertisement.

## Manual StickS3 Test

- [ ] Firmware uploads successfully.
- [ ] Device boots to the launcher.
- [ ] Button B cycles through Weather App, Remote Mic, and CodeBuddy.
- [ ] Button A launches CodeBuddy in portrait orientation.
- [ ] CodeBuddy shows an animated ASCII buddy when no GIF character is
  installed.
- [ ] Long Button A or Button B returns from CodeBuddy to the launcher.
- [ ] Returning to the launcher restores landscape orientation.
- [ ] Device advertises as `StickS3 Link`.
- [ ] Existing macOS Stick Link app discovers `StickS3 Link` by name prefix,
  connects, and subscribes to the Stick Link service for Remote Mic.
- [ ] Remote Mic Button A hold starts a voice event and audio chunks in the
  existing macOS companion app.
- [ ] Open CodeBuddy on the StickS3 before running the Mac host.
- [ ] CodeBuddy advertises as `Codex-StickS3` while open.
- [ ] CodeBuddy host discovers the Nordic UART Service.
- [ ] `code-buddy` helper/agent is installed/running on the Mac; otherwise
  CodeBuddy remains in the no-Codex/sleeping state.
- [ ] CodeBuddy heartbeat JSON updates the CodeBuddy screen.
- [ ] In CodeBuddy with no prompt, Button A cycles Home/Pet/Info views.
- [ ] In CodeBuddy with no prompt, Button B changes pet/page.
- [ ] When a CodeBuddy prompt is pending, Button A sends `decision:"once"`.
- [ ] When a CodeBuddy prompt is pending, Button B sends `decision:"deny"`.
- [ ] CodeBuddy desktop receives valid acks for `owner`, `name`, `status`, and
  `unpair`.
- [ ] CodeBuddy desktop folder push reaches `char_end` with `ok:true`.
- [ ] After folder push, CodeBuddy loads the pushed GIF character from
  `/characters/<name>/manifest.json`.
- [ ] Remote Mic still streams audio after returning from CodeBuddy.
- [ ] Serial monitor has no repeated crash or reboot loop.

## Current Limitations

- This is a firmware-side integration. It does not merge the existing macOS
  Stick Link app and the CodeBuddy host process into a single desktop client.
- CodeBuddy pet stats UI, settings menu UI, clock mode, face-down nap,
  shake-to-dizzy, screen auto-off, and secure pairing/passkey UI from
  `external/CodeBuddy/firmware` are not fully ported yet.
- Folder push is stored under the same `/characters` path expected by the
  upstream character renderer, and `char_end` attempts to load the pushed
  character immediately.
- Physical BLE interoperability still needs real StickS3 and macOS validation.
