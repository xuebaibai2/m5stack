# Remote Mic Bluetooth

The second launcher app, `Remote Mic`, streams StickS3 microphone audio to the
macOS menu bar app over BLE.

## Firmware Behavior

- The StickS3 initializes BLE at boot with the advertising name `StickS3 Link`.
- Remote Mic is launcher app index `1`.
- Opening Remote Mic shows BLE connection and audio chunk status.
- Hold Button A to record the StickS3 microphone.
- While Button A is held, the device records PCM audio locally.
- Releasing Button A sends the buffered PCM audio over BLE at a controlled pace,
  then sends a voice stop event so the Mac can finish transcription and output
  the text.
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
- Audio uses 16 kHz mono PCM for short push-to-talk utterances.
- Recording buffer is allocated from heap at runtime. The firmware targets up to
  about 10 seconds, but the actual limit depends on available StickS3 RAM and is
  shown on the Remote Mic screen while recording.
- Saved WAV files are the first debugging artifact for poor recognition:
  inspect whether the received audio itself is intelligible.
- macOS text output requires Accessibility permission because the app sends a
  Cmd+V keyboard event after writing the transcript to the clipboard.
- Flashing Arduino firmware replaces UIFlow2/MicroPython firmware on the device.
