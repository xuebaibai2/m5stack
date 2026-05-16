# Stick Link Bluetooth Protocol

Stick Link uses Bluetooth Low Energy. The StickS3 advertises as a BLE
peripheral, and the macOS app connects as a CoreBluetooth central.

## BLE Identifiers

Default device name:

```text
StickS3 Link
```

Service UUID:

```text
6f7d9f10-2c3b-4e7a-9a1f-1b2c3d4e5f60
```

Message characteristic UUID:

```text
6f7d9f11-2c3b-4e7a-9a1f-1b2c3d4e5f60
```

Properties:

- `read`
- `notify`
- `write`

Device info characteristic UUID:

```text
6f7d9f12-2c3b-4e7a-9a1f-1b2c3d4e5f60
```

Properties:

- `read`

Audio characteristic UUID:

```text
6f7d9f13-2c3b-4e7a-9a1f-1b2c3d4e5f60
```

Properties:

- `notify`

Audio payload format:

- packed unsigned linear PCM, 12-bit little-endian nibbles
- mono
- 8000 Hz
- 100 decoded samples per BLE notification by default
- 150 bytes per BLE notification by default
- streamed live while Button A is held

The Mac app decodes the PCM12 stream to little-endian signed 16-bit PCM before
feeding macOS Speech or writing WAV recordings.

Remote Mic captures from the StickS3 microphone at 16 kHz and downsamples to
the 8 kHz BLE stream rate before packing the audio payload. Firmware-side
codec gain control and filtered downsampling keep normal speech below clipping
before PCM12 packing; current codec volume and downsample filter metadata are
reported through the device-info JSON when available.

## Message Envelope

Messages are UTF-8 JSON objects. Version 1 messages use this envelope:

```json
{
  "v": 1,
  "id": "000001",
  "app": "remote_mic",
  "type": "voice",
  "name": "start",
  "text": "Remote Mic recording started",
  "ts_ms": 123456,
  "seq": 1
}
```

Fields:

- `v`: protocol version.
- `id`: display/debug id generated from the sequence number.
- `app`: logical source app.
- `type`: event category.
- `name`: event name.
- `text`: human-readable log message.
- `ts_ms`: device uptime from `millis()`.
- `seq`: monotonically increasing device-side sequence number.

Consumers must ignore unknown fields. Audio uses the separate audio
characteristic; the message characteristic is intended for small control events
and metadata.

## Mac To StickS3 Commands

The macOS app may write a small UTF-8 JSON command to the message
characteristic. The StickS3 responds with a normal notification event on the
same characteristic.

Weather location update:

```json
{
  "v": 1,
  "app": "weather",
  "type": "config",
  "name": "set_location",
  "text": "Set weather location",
  "location_name": "Melbourne",
  "latitude": "-37.8136",
  "longitude": "144.9631",
  "timezone": "Australia/Melbourne"
}
```

The StickS3 persists the location in NVS and refreshes the Weather app
immediately when it is visible. Future Weather app launches use the saved NVS
location before falling back to `generated_config.h`.

## Remote Mic Events

When Button A is pressed and held in Remote Mic, the StickS3 sends:

```json
{
  "v": 1,
  "id": "000001",
  "app": "remote_mic",
  "type": "voice",
  "name": "start",
  "text": "Remote Mic recording started",
  "ts_ms": 123456,
  "seq": 1
}
```

While Button A is held, PCM12 audio chunks are sent over the audio
characteristic. When Button A is released, the StickS3 sends:

```json
{
  "v": 1,
  "id": "000002",
  "app": "remote_mic",
  "type": "voice",
  "name": "stop",
  "text": "Remote Mic recording stopped",
  "ts_ms": 125000,
  "seq": 2
}
```

The Mac app can filter JSON events by `app` and `type` through runtime config.

## Shared BLE Server And CodeBuddy

Firmware now owns BLE through one shared server. The same StickS3 peripheral
advertises both:

- the Stick Link service used by Remote Mic and weather configuration
- the CodeBuddy Nordic UART Service

The shared advertising name is:

```text
StickS3 Link
```

The Stick Link name preserves compatibility with the existing macOS companion
app's `StickS3` name-prefix filter. The macOS Stick Link app scans by that
name prefix, then discovers the Stick Link service after connecting. This avoids
depending on the Stick Link service UUID being present in the advertisement
payload when the shared firmware also exposes the CodeBuddy Nordic UART service.

CodeBuddy Nordic UART identifiers:

```text
Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
RX:      6e400002-b5a3-f393-e0a9-e50e24dcca9e
TX:      6e400003-b5a3-f393-e0a9-e50e24dcca9e
```

CodeBuddy RX receives newline-delimited UTF-8 JSON from the desktop. CodeBuddy
TX sends newline-delimited UTF-8 JSON notifications back to the desktop.

Implemented CodeBuddy BLE support:

- heartbeat snapshot parsing
- prompt approve/deny responses
- `time` sync
- `owner`, `name`, `status`, and `unpair` commands
- folder-push commands: `char_begin`, `file`, `chunk`, `file_end`, `char_end`

Folder-push files are stored under LittleFS at `/characters`, matching the
upstream CodeBuddy firmware layout. `char_end` attempts to load the transferred
character immediately through the CodeBuddy GIF renderer.
