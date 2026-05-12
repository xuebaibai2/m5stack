#include "remote_mic_app.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <cstring>
#include <M5GFX.h>
#include <M5Unified.h>

#include "stick_link_protocol.h"

namespace {

constexpr uint32_t kChunkRedrawMs = 250;
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
constexpr uint32_t kRemoteMicCaptureSampleRate = 16000;
constexpr size_t kRemoteMicCaptureSamplesPerChunk =
    kStickLinkAudioSamplesPerChunk * 2;
constexpr uint8_t kRemoteMicMagnification = 1;
constexpr uint8_t kRemoteMicNoiseFilterLevel = 0;
constexpr uint8_t kRemoteMicOverSampling = 4;
constexpr int32_t kAgcTargetPeak = 12000;
constexpr int32_t kAgcSoftLimitStart = 18000;
constexpr int32_t kAgcSoftLimitMax = 24000;
constexpr int32_t kAgcMinGainQ15 = 16384;
constexpr int32_t kAgcMaxGainQ15 = 65536;
constexpr int32_t kAgcAttackStepQ15 = 4096;
constexpr int32_t kAgcReleaseStepQ15 = 256;

BLEServer* bleServer = nullptr;
BLECharacteristic* messageCharacteristic = nullptr;
BLECharacteristic* deviceInfoCharacteristic = nullptr;
BLECharacteristic* audioCharacteristic = nullptr;

bool bleStarted = false;
bool bleConnected = false;
bool appVisible = false;
bool screenDirty = false;
bool recording = false;
uint32_t sequenceNumber = 0;
uint32_t lastChunkRedrawAt = 0;
uint32_t lastSendAt = 0;
uint32_t audioChunkCount = 0;
uint32_t displayedAudioChunkCount = UINT32_MAX;
bool displayedBleConnected = false;
char displayedStatus[96] = "";
char lastStatus[96] = "Advertising";
int16_t captureBuffer[kRemoteMicCaptureSamplesPerChunk] = {};
int16_t audioBuffer[kStickLinkAudioSamplesPerChunk] = {};
uint8_t pcm12Buffer[kStickLinkAudioBytesPerChunk] = {};
int32_t agcGainQ15 = 32768;

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
  doc["audio_format"] = "pcm_u12le_packed_mono";
  doc["audio_mode"] = "live_compressed_stream";
  doc["mic_capture_sample_rate"] = kRemoteMicCaptureSampleRate;
  doc["mic_magnification"] = kRemoteMicMagnification;
  doc["mic_noise_filter_level"] = kRemoteMicNoiseFilterLevel;
  doc["mic_over_sampling"] = kRemoteMicOverSampling;
  doc["agc_target_peak"] = kAgcTargetPeak;
  doc["agc_soft_limit_start"] = kAgcSoftLimitStart;
  doc["agc_soft_limit_max"] = kAgcSoftLimitMax;

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
  M5.Display.printf("Chunks: %lu  %.1fs",
                    static_cast<unsigned long>(audioChunkCount),
                    (audioChunkCount * kStickLinkAudioSamplesPerChunk) /
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

void downsampleCaptureChunk(const int16_t* input, int16_t* output,
                            size_t outputCount) {
  for (size_t i = 0; i < outputCount; ++i) {
    const int32_t first = input[i * 2];
    const int32_t second = input[i * 2 + 1];
    output[i] = static_cast<int16_t>((first + second) / 2);
  }
}

int32_t chunkPeak(const int16_t* samples, size_t sampleCount) {
  int32_t peak = 0;
  for (size_t i = 0; i < sampleCount; ++i) {
    const int32_t value = samples[i];
    const int32_t magnitude = value < 0 ? -value : value;
    if (magnitude > peak) {
      peak = magnitude;
    }
  }
  return peak;
}

void updateAgcGain(int32_t peak) {
  if (peak <= 0) {
    return;
  }

  const int32_t targetGainQ15 =
      constrain((kAgcTargetPeak << 15) / peak, kAgcMinGainQ15,
                kAgcMaxGainQ15);

  if (targetGainQ15 < agcGainQ15) {
    agcGainQ15 -= min(kAgcAttackStepQ15, agcGainQ15 - targetGainQ15);
  } else if (targetGainQ15 > agcGainQ15) {
    agcGainQ15 += min(kAgcReleaseStepQ15, targetGainQ15 - agcGainQ15);
  }
}

int16_t applyAgcAndLimit(int16_t sample) {
  int32_t scaled = (static_cast<int32_t>(sample) * agcGainQ15) >> 15;
  if (scaled > INT16_MAX) {
    scaled = INT16_MAX;
  } else if (scaled < INT16_MIN) {
    scaled = INT16_MIN;
  }

  const int32_t sampleValue = scaled;
  const int32_t magnitude = sampleValue < 0 ? -sampleValue : sampleValue;
  if (magnitude <= kAgcSoftLimitStart) {
    return static_cast<int16_t>(sampleValue);
  }

  int32_t limited =
      kAgcSoftLimitStart + ((magnitude - kAgcSoftLimitStart) >> 2);
  if (limited > kAgcSoftLimitMax) {
    limited = kAgcSoftLimitMax;
  }

  return static_cast<int16_t>(sampleValue < 0 ? -limited : limited);
}

void applyAutomaticLevelControl(int16_t* samples, size_t sampleCount) {
  updateAgcGain(chunkPeak(samples, sampleCount));
  for (size_t i = 0; i < sampleCount; ++i) {
    samples[i] = applyAgcAndLimit(samples[i]);
  }
}

uint16_t encodePcm12Sample(int16_t sample) {
  return static_cast<uint16_t>((static_cast<int32_t>(sample) + 32768) >> 4);
}

void encodePcm12Chunk(const int16_t* samples, size_t sampleCount, uint8_t* out) {
  for (size_t i = 0; i < sampleCount; i += 2) {
    const uint16_t first = encodePcm12Sample(samples[i]);
    const uint16_t second =
        (i + 1 < sampleCount) ? encodePcm12Sample(samples[i + 1]) : 2048;
    *out++ = static_cast<uint8_t>(first & 0xFF);
    *out++ = static_cast<uint8_t>(((first >> 8) & 0x0F) |
                                  ((second & 0x0F) << 4));
    *out++ = static_cast<uint8_t>((second >> 4) & 0xFF);
  }
}

void configureRemoteMicInput() {
  auto cfg = M5.Mic.config();
  cfg.sample_rate = kRemoteMicCaptureSampleRate;
  cfg.magnification = kRemoteMicMagnification;
  cfg.noise_filter_level = kRemoteMicNoiseFilterLevel;
  cfg.over_sampling = kRemoteMicOverSampling;
  M5.Mic.config(cfg);
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
      M5.Mic.record(captureBuffer, kRemoteMicCaptureSamplesPerChunk,
                    kRemoteMicCaptureSampleRate)) {
    ++audioChunkCount;
    if (bleConnected && audioCharacteristic != nullptr) {
      downsampleCaptureChunk(captureBuffer, audioBuffer,
                             kStickLinkAudioSamplesPerChunk);
      applyAutomaticLevelControl(audioBuffer, kStickLinkAudioSamplesPerChunk);
      encodePcm12Chunk(audioBuffer, kStickLinkAudioSamplesPerChunk,
                       pcm12Buffer);
      audioCharacteristic->setValue(pcm12Buffer, sizeof(pcm12Buffer));
      audioCharacteristic->notify();
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
  agcGainQ15 = 32768;
  lastSendAt = millis();

  if (!M5.Mic.isEnabled()) {
    M5.Speaker.end();
    configureRemoteMicInput();
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
  snprintf(lastStatus, sizeof(lastStatus),
           bleConnected ? "Stopped" : "No Mac connected");
  Serial.println(payload);
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
