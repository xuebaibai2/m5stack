#include "remote_mic_app.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <M5GFX.h>
#include <M5Unified.h>

#include "stick_link_protocol.h"

namespace {

constexpr uint32_t kChunkRedrawMs = 250;
constexpr uint32_t kBleAudioSendIntervalMs = 15;
constexpr size_t kRecordingHeapReserveBytes = 48 * 1024;
constexpr int kConnectionX = 222;
constexpr int kConnectionY = 16;
constexpr int kStatusValueX = 8;
constexpr int kStatusValueY = 86;
constexpr int kStatusValueW = 216;
constexpr int kStatusValueH = 14;
constexpr int kChunkValueX = 8;
constexpr int kChunkValueY = 124;
constexpr int kChunkValueW = 216;
constexpr int kChunkValueH = 14;

BLEServer* bleServer = nullptr;
BLECharacteristic* messageCharacteristic = nullptr;
BLECharacteristic* deviceInfoCharacteristic = nullptr;
BLECharacteristic* audioCharacteristic = nullptr;

bool bleStarted = false;
bool bleConnected = false;
bool appVisible = false;
bool screenDirty = false;
bool recording = false;
bool sendingAudio = false;
uint32_t sequenceNumber = 0;
uint32_t lastChunkRedrawAt = 0;
uint32_t lastAudioSendAt = 0;
uint32_t lastSendAt = 0;
uint32_t audioChunkCount = 0;
size_t recordedSampleCount = 0;
size_t recordingSampleCapacity = 0;
size_t sendSampleOffset = 0;
uint32_t displayedAudioChunkCount = UINT32_MAX;
bool displayedBleConnected = false;
char displayedStatus[96] = "";
char lastStatus[96] = "Advertising";
int16_t audioBuffer[kStickLinkAudioSamplesPerChunk] = {};
int16_t* recordedAudio = nullptr;

class RemoteMicServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleConnected = true;
    snprintf(lastStatus, sizeof(lastStatus), "Mac connected");
    screenDirty = true;
  }

  void onDisconnect(BLEServer* server) override {
    bleConnected = false;
    snprintf(lastStatus, sizeof(lastStatus), "Disconnected, advertising");
    screenDirty = true;
    server->startAdvertising();
  }
};

String deviceInfoJson() {
  JsonDocument doc;
  doc["v"] = 1;
  doc["name"] = kStickLinkBleName;
  doc["device"] = "M5Stack StickS3";
  doc["role"] = "ble_peripheral";
  doc["service"] = kStickLinkServiceUuid;
  doc["message_characteristic"] = kStickLinkMessageCharacteristicUuid;
  doc["device_info_characteristic"] = kStickLinkDeviceInfoCharacteristicUuid;
  doc["audio_characteristic"] = kStickLinkAudioCharacteristicUuid;
  doc["audio_sample_rate"] = kStickLinkAudioSampleRate;
  doc["audio_format"] = "pcm_s16le_mono";
  doc["audio_mode"] = "buffered_send_after_release";
  doc["target_recording_seconds"] = kStickLinkTargetRecordingSeconds;

  String output;
  serializeJson(doc, output);
  return output;
}

void drawConnectionIndicator() {
  if (!appVisible) {
    return;
  }

  M5.Display.fillRect(kConnectionX - 8, kConnectionY - 8, 20, 20, TFT_BLACK);
  M5.Display.fillCircle(kConnectionX, kConnectionY, 5,
                        bleConnected ? TFT_GREEN : TFT_ORANGE);
  displayedBleConnected = bleConnected;
}

void drawStatusValue() {
  if (!appVisible) {
    return;
  }

  M5.Display.fillRect(kStatusValueX, kStatusValueY, kStatusValueW,
                      kStatusValueH, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(bleConnected ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  M5.Display.setCursor(kStatusValueX, kStatusValueY);
  M5.Display.print(lastStatus);
  strlcpy(displayedStatus, lastStatus, sizeof(displayedStatus));
}

void drawChunkValue() {
  if (!appVisible) {
    return;
  }

  M5.Display.fillRect(kChunkValueX, kChunkValueY, kChunkValueW, kChunkValueH,
                      TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setCursor(kChunkValueX, kChunkValueY);
  M5.Display.printf("Chunks: %lu  %.1fs/%.1fs",
                    static_cast<unsigned long>(audioChunkCount),
                    recordedSampleCount /
                        static_cast<float>(kStickLinkAudioSampleRate),
                    recordingSampleCapacity /
                        static_cast<float>(kStickLinkAudioSampleRate));
  displayedAudioChunkCount = audioChunkCount;
}

void drawDynamicRegions(bool force = false) {
  if (force || displayedBleConnected != bleConnected) {
    drawConnectionIndicator();
  }

  if (force || strcmp(displayedStatus, lastStatus) != 0) {
    drawStatusValue();
  }

  const uint32_t now = millis();
  if (force || displayedAudioChunkCount != audioChunkCount) {
    if (force || !recording || now - lastChunkRedrawAt >= kChunkRedrawMs) {
      drawChunkValue();
      lastChunkRedrawAt = now;
    }
  }
}

void drawRemoteMicScreen() {
  if (!appVisible) {
    return;
  }

  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.print("Remote Mic");

  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, 36);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.print("BLE device");
  M5.Display.setCursor(8, 50);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.print(kStickLinkBleName);

  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setCursor(8, 72);
  M5.Display.print("Status");

  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setCursor(8, 110);
  M5.Display.printf("Hold A: talk  Seq: %lu",
                    static_cast<unsigned long>(sequenceNumber));

  drawDynamicRegions(true);
  screenDirty = false;
}

void startAdvertising() {
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(kStickLinkServiceUuid);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
}

bool ensureRecordingBuffer() {
  if (recordedAudio != nullptr && recordingSampleCapacity > 0) {
    return true;
  }

  const size_t targetBytes =
      kStickLinkAudioSampleRate * kStickLinkTargetRecordingSeconds *
      sizeof(int16_t);
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (largestBlock <= kRecordingHeapReserveBytes) {
    recordingSampleCapacity = 0;
    return false;
  }

  size_t candidateBytes = targetBytes;
  const size_t maxSafeBytes = largestBlock - kRecordingHeapReserveBytes;
  if (candidateBytes > maxSafeBytes) {
    candidateBytes = maxSafeBytes;
  }
  candidateBytes -= candidateBytes % (kStickLinkAudioSamplesPerChunk *
                                      sizeof(int16_t));

  while (candidateBytes >= kStickLinkAudioSamplesPerChunk * sizeof(int16_t)) {
    recordedAudio = static_cast<int16_t*>(
        heap_caps_malloc(candidateBytes, MALLOC_CAP_8BIT));
    if (recordedAudio != nullptr) {
      recordingSampleCapacity = candidateBytes / sizeof(int16_t);
      Serial.printf("Remote Mic buffer: %u bytes, %.1fs\n",
                    static_cast<unsigned>(candidateBytes),
                    recordingSampleCapacity /
                        static_cast<float>(kStickLinkAudioSampleRate));
      return true;
    }
    candidateBytes -= kStickLinkAudioSamplesPerChunk * sizeof(int16_t);
  }

  recordingSampleCapacity = 0;
  return false;
}

}  // namespace

void remoteMicAppBegin() {
  if (bleStarted) {
    return;
  }

  BLEDevice::init(kStickLinkBleName);
  BLEDevice::setMTU(185);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new RemoteMicServerCallbacks());

  BLEService* service = bleServer->createService(kStickLinkServiceUuid);

  messageCharacteristic = service->createCharacteristic(
      kStickLinkMessageCharacteristicUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  messageCharacteristic->addDescriptor(new BLE2902());
  messageCharacteristic->setValue("ready");

  deviceInfoCharacteristic = service->createCharacteristic(
      kStickLinkDeviceInfoCharacteristicUuid, BLECharacteristic::PROPERTY_READ);
  const String info = deviceInfoJson();
  deviceInfoCharacteristic->setValue(info.c_str());

  audioCharacteristic = service->createCharacteristic(
      kStickLinkAudioCharacteristicUuid, BLECharacteristic::PROPERTY_NOTIFY);
  audioCharacteristic->addDescriptor(new BLE2902());

  service->start();
  startAdvertising();

  bleStarted = true;
  snprintf(lastStatus, sizeof(lastStatus), "Advertising");
  Serial.println("Remote Mic BLE advertising started");
}

void remoteMicAppStart() {
  appVisible = true;
  screenDirty = true;
  drawRemoteMicScreen();
}

void remoteMicAppUpdate() {
  if (recording && M5.Mic.isEnabled() &&
      M5.Mic.record(audioBuffer, kStickLinkAudioSamplesPerChunk,
                    kStickLinkAudioSampleRate)) {
    const size_t remaining = recordingSampleCapacity - recordedSampleCount;
    const size_t copyCount =
        remaining < kStickLinkAudioSamplesPerChunk ? remaining
                                                   : kStickLinkAudioSamplesPerChunk;
    if (copyCount > 0) {
      memcpy(&recordedAudio[recordedSampleCount], audioBuffer,
             copyCount * sizeof(int16_t));
      recordedSampleCount += copyCount;
      ++audioChunkCount;
    } else {
      snprintf(lastStatus, sizeof(lastStatus), "Buffer full");
      remoteMicAppStopRecording();
    }
  }

  if (sendingAudio && bleConnected && audioCharacteristic != nullptr) {
    const uint32_t now = millis();
    if (now - lastAudioSendAt >= kBleAudioSendIntervalMs) {
      lastAudioSendAt = now;
      const size_t remaining = recordedSampleCount - sendSampleOffset;
      const size_t sendCount =
          remaining < kStickLinkAudioSamplesPerChunk ? remaining
                                                     : kStickLinkAudioSamplesPerChunk;
      if (sendCount > 0) {
        audioCharacteristic->setValue(
            reinterpret_cast<uint8_t*>(&recordedAudio[sendSampleOffset]),
            sendCount * sizeof(int16_t));
        audioCharacteristic->notify();
        sendSampleOffset += sendCount;
      }

      if (sendSampleOffset >= recordedSampleCount) {
        sendingAudio = false;
        ++sequenceNumber;
        const String payload =
            stickLinkEncodeVoiceEvent("stop", "Remote Mic recording stopped",
                                      millis(), sequenceNumber);
        if (messageCharacteristic != nullptr) {
          messageCharacteristic->setValue(payload.c_str());
          messageCharacteristic->notify();
        }
        snprintf(lastStatus, sizeof(lastStatus), "Sent recording");
        Serial.println(payload);
        screenDirty = true;
      }
    }
  }

  if (screenDirty) {
    drawRemoteMicScreen();
  } else {
    drawDynamicRegions();
  }
}

void remoteMicAppStartRecording() {
  if (recording) {
    return;
  }

  ++sequenceNumber;
  audioChunkCount = 0;
  recordedSampleCount = 0;
  sendSampleOffset = 0;
  sendingAudio = false;
  lastSendAt = millis();

  if (!ensureRecordingBuffer()) {
    snprintf(lastStatus, sizeof(lastStatus), "No audio buffer");
    screenDirty = true;
    drawRemoteMicScreen();
    return;
  }

  if (!M5.Mic.isEnabled()) {
    M5.Speaker.end();
    M5.Mic.begin();
  }

  recording = true;
  const String payload = stickLinkEncodeVoiceEvent(
      "start", "Remote Mic recording started", lastSendAt, sequenceNumber);

  if (messageCharacteristic != nullptr) {
    messageCharacteristic->setValue(payload.c_str());
    if (bleConnected) {
      messageCharacteristic->notify();
    }
  }

  snprintf(lastStatus, sizeof(lastStatus),
           bleConnected ? "Recording" : "Recording, no Mac");
  Serial.println(payload);
  screenDirty = true;
  drawRemoteMicScreen();
}

void remoteMicAppStopRecording() {
  if (!recording) {
    return;
  }

  recording = false;
  lastSendAt = millis();

  while (M5.Mic.isRecording()) {
    delay(1);
  }

  if (bleConnected && audioCharacteristic != nullptr && recordedSampleCount > 0) {
    sendingAudio = true;
    sendSampleOffset = 0;
    lastAudioSendAt = 0;
    snprintf(lastStatus, sizeof(lastStatus), "Sending audio");
  } else {
    ++sequenceNumber;
    const String payload =
        stickLinkEncodeVoiceEvent("stop", "Remote Mic recording stopped",
                                  lastSendAt, sequenceNumber);
    if (messageCharacteristic != nullptr) {
      messageCharacteristic->setValue(payload.c_str());
      if (bleConnected) {
        messageCharacteristic->notify();
      }
    }
    snprintf(lastStatus, sizeof(lastStatus), "No Mac connected");
    Serial.println(payload);
  }
  screenDirty = true;
  drawRemoteMicScreen();
}

void remoteMicAppStop() {
  remoteMicAppStopRecording();
  appVisible = false;
}

bool remoteMicAppConnected() {
  return bleConnected;
}
