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

- G.711 mu-law, 8-bit
- mono
- 16000 Hz
- 160 decoded samples per BLE notification by default
- 160 bytes per BLE notification by default
- streamed live while Button A is held

The Mac app decodes the mu-law stream to little-endian signed 16-bit PCM before
feeding macOS Speech or writing WAV recordings.

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

While Button A is held, mu-law audio chunks are sent over the audio
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
