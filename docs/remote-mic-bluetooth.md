# Remote Mic Bluetooth

The second launcher app, `Remote Mic`, streams StickS3 microphone audio to the
macOS menu bar app over BLE.

## Firmware Behavior

- The StickS3 initializes BLE at boot with the advertising name `StickS3 Link`.
- Remote Mic is launcher app index `1`.
- Opening Remote Mic shows BLE connection and audio chunk status.
- Hold Button A to record the StickS3 microphone.
- While Button A is held, the device streams packed 12-bit unsigned PCM audio chunks over
  BLE.
- Releasing Button A sends a voice stop event so the Mac can finish
  transcription and output the text.
- Long Button B returns to the launcher. Button A long press is reserved for
  push-to-talk while Remote Mic is open.

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

No separate macOS Bluetooth pairing step is required. The Mac app scans for the
custom BLE service and connects directly through CoreBluetooth.

1. Flash and boot the StickS3 firmware.
2. Open the Mac app from `mac/StickLinkMenuBar`.
3. Grant Bluetooth, Speech Recognition, and Accessibility permissions when
   macOS prompts.
4. Click `Scan` in the menu bar app.
5. Wait for the app to reach `Subscribed`.
6. Open Remote Mic on the StickS3.
7. Click into any text editor on the Mac.
8. Hold Button A, speak into the StickS3, then release Button A.
9. Confirm the Mac app log shows a transcript and the focused editor receives
   the text.
10. Check `~/Downloads/StickLink-RemoteMic-*.wav` if the transcript is wrong.

## Manual StickS3 Test

- [ ] Firmware uploads successfully.
- [ ] Device boots to the launcher.
- [ ] Button B selects Remote Mic.
- [ ] Button A opens Remote Mic.
- [ ] Remote Mic shows BLE advertising/connection status.
- [ ] Mac app connects and reaches subscribed state.
- [ ] Holding Button A sends audio chunks.
- [ ] Releasing Button A triggers Mac transcription.
- [ ] Transcript is pasted into the focused text editor.
- [ ] A WAV recording is saved in `~/Downloads`.
- [ ] Long Button B returns to the launcher.
- [ ] Serial monitor has no repeated crash or reboot loop.

## Limitations

- BLE audio and macOS Speech delivery require real hardware and were not
  automated here.
- Audio capture runs at 16 kHz on the StickS3, then firmware downsamples to an
  8 kHz mono packed 12-bit unsigned PCM BLE stream. The Mac app decodes it to
  16-bit PCM for transcription and saved WAV files.
- The stream uses 150-byte BLE notifications, representing 12.5 ms of speech at
  8 kHz. This keeps packet size safely under the BLE MTU while reducing the
  quantization noise of the earlier 8-bit stream.
- The firmware applies light M5Unified mic conditioning before PCM12 encoding:
  very low input magnification, higher oversampling, automatic level control,
  and a final soft limiter for speech peaks. These values are also exposed in
  the BLE device-info JSON for debugging recordings.
- There is no fixed firmware recording-duration cap. Recording continues while
  Button A is held, subject to BLE link quality, battery, and macOS Speech
  session behavior. The StickS3 only holds the current mic chunk and outgoing
  BLE packet briefly.
- Saved WAV files are the first debugging artifact for poor recognition:
  inspect whether the decoded received audio itself is intelligible.
- macOS text output requires Accessibility permission because the app sends a
  Cmd+V keyboard event after writing the transcript to the clipboard.
- Flashing Arduino firmware replaces UIFlow2/MicroPython firmware on the device.
