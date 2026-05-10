# Sensor App Bluetooth

The second launcher app, `Sensor App`, now starts a BLE event channel for the
Mac menu bar app.

## Firmware Behavior

- The StickS3 initializes BLE at boot with the advertising name `StickS3 Link`.
- Sensor App is still launcher app index `1`.
- Opening Sensor App shows BLE connection status and the current message
  sequence.
- Short Button A sends a JSON event through the BLE notify characteristic.
- If no Mac is connected, Button A still updates the display and serial log.
- Long Button A or Button B returns to the launcher.

The BLE protocol is documented in `docs/bluetooth-protocol.md`.

## Build

Compile the firmware:

```bash
pio run
```

## Upload

Put the StickS3 into download mode, then upload with the detected serial port:

```bash
pio run -t upload --upload-port /dev/cu.usbmodem101
```

If the port differs, check:

```bash
ls /dev/cu.*
```

## Pair And Connect

No separate macOS Bluetooth pairing step is required for this prototype. The Mac
app scans for the custom BLE service and connects directly through
CoreBluetooth.

1. Flash and boot the StickS3 firmware.
2. Open the Mac app from `mac/StickLinkMenuBar`.
3. Click `Scan` in the menu bar app.
4. Wait for the app to reach `Subscribed`.
5. Open Sensor App on the StickS3.
6. Press Button A.
7. Confirm the Mac app log shows `ButtonA pressed from Sensor App`.

## Manual StickS3 Test

- [ ] Firmware uploads successfully.
- [ ] Device boots to the launcher.
- [ ] Button B selects Sensor App.
- [ ] Button A opens Sensor App.
- [ ] Sensor App shows BLE advertising/connection status.
- [ ] Mac app connects and reaches subscribed state.
- [ ] Short Button A sends `ButtonA pressed from Sensor App`.
- [ ] Long Button A returns to the launcher.
- [ ] Long Button B returns to the launcher.
- [ ] Serial monitor has no repeated crash or reboot loop.

## Limitations

- BLE notification delivery requires real hardware and was not automated here.
- BLE is suitable for short events and metadata. Voice/audio streaming needs a
  separate chunking design or another transport.
- Flashing Arduino firmware replaces UIFlow2/MicroPython firmware on the device.
