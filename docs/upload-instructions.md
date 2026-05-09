# StickS3 Upload Instructions

This project targets an M5Stack StickS3 with Arduino C++ and PlatformIO.

## Build

Compile the firmware before uploading:

```bash
pio run
```

## Upload

The local PlatformIO `espressif32` board package does not currently expose a
`m5stack-sticks3` board ID, so `platformio.ini` uses the closest M5Stack ESP32-S3
fallback board definition:

```ini
board = m5stack-stamps3
```

On macOS, the StickS3 native USB serial port may disconnect when esptool tries
to auto-reset the device. This project therefore uploads at a lower baud rate
and disables the pre-upload reset:

```ini
upload_speed = 115200
board_upload.before_reset = no_reset
board_upload.after_reset = hard_reset
```

Use this upload flow:

1. Close anything that may hold the serial port, including PlatformIO serial
   monitor, Chrome Web Serial tabs, M5Burner, UIFlow, and ESP Web Tools.
2. Put the StickS3 into download mode: hold the side reset button for about 2
   seconds, then release it when the green LED blinks.
3. Check the current serial port:

   ```bash
   ls /dev/cu.*
   ```

4. Upload using the detected `/dev/cu.usbmodem...` port:

   ```bash
   pio run -t upload --upload-port /dev/cu.usbmodem101
   ```

   If the device appears with a different suffix, replace
   `/dev/cu.usbmodem101` with the actual path.

5. Open the serial monitor only after upload completes:

   ```bash
   pio device monitor -p /dev/cu.usbmodem101 -b 115200
   ```

## Troubleshooting

If upload reports that the port is busy:

```bash
lsof /dev/cu.usbmodem101
```

Quit the process that owns the port, then upload again.

If upload reports `No serial data received`, the device is usually not in
bootloader/download mode. Re-enter download mode and retry the same upload
command.

If upload reports `Device not configured` or the `/dev/cu.usbmodem...` port
disappears, unplug/replug the StickS3, re-enter download mode, confirm the port
with `ls /dev/cu.*`, and upload again.
