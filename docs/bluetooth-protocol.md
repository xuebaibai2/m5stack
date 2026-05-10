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

## Message Envelope

Messages are UTF-8 JSON objects. Version 1 messages use this envelope:

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

Fields:

- `v`: protocol version.
- `id`: display/debug id generated from the sequence number.
- `app`: logical source app.
- `type`: event category.
- `name`: event name.
- `text`: human-readable log message.
- `ts_ms`: device uptime from `millis()`.
- `seq`: monotonically increasing device-side sequence number.

Consumers must ignore unknown fields. Future audio or large binary payloads
should use a separate characteristic or an explicit chunking protocol; the
message characteristic is intended for small events and metadata.

## Current Event

Inside Sensor App, short Button A sends:

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

The Mac app can filter by `app` and `type` through runtime config.
