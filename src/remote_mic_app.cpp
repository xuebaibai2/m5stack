#include "remote_mic_app.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLECharacteristic.h>
#include <BLEServer.h>
#include <cstring>
#include <M5GFX.h>
#include <M5Unified.h>

#include "remote_mic_audio_config.h"
#include "shared_ble.h"
#include "stick_link_protocol.h"
#include "weather_app.h"

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
constexpr size_t kRemoteMicCaptureFramesPerChunk =
    kStickLinkAudioSamplesPerChunk * 2;
constexpr size_t kRemoteMicCaptureSamplesPerChunk =
    kRemoteMicCaptureFramesPerChunk * 2;
constexpr uint8_t kRemoteMicMagnification = 1;
constexpr uint8_t kRemoteMicNoiseFilterLevel = 0;
constexpr uint8_t kRemoteMicOverSampling = 4;
constexpr uint8_t kRemoteMicPrimeChunks = 3;

BLECharacteristic* messageCharacteristic = nullptr;
BLECharacteristic* deviceInfoCharacteristic = nullptr;
BLECharacteristic* audioCharacteristic = nullptr;

bool bleStarted = false;
bool appVisible = false;
bool screenDirty = false;
bool recording = false;
bool remoteMicInputReady = false;
uint32_t sequenceNumber = 0;
uint32_t lastChunkRedrawAt = 0;
uint32_t lastSendAt = 0;
uint32_t audioChunkCount = 0;
uint32_t displayedAudioChunkCount = UINT32_MAX;
bool displayedBleConnected = false;
char displayedStatus[96] = "";
char lastStatus[96] = "Advertising";
int16_t captureBuffers[2][kRemoteMicCaptureSamplesPerChunk] = {};
int16_t audioBuffer[kStickLinkAudioSamplesPerChunk] = {};
uint8_t pcm12Buffer[kStickLinkAudioBytesPerChunk] = {};
uint8_t captureWriteBuffer = 0;
int8_t completedCaptureBuffer = -1;
bool lastCodecInputLevelOk = false;
uint8_t lastCodecInputLevelReadback = 0;
uint8_t primingChunksRemaining = 0;
volatile bool weatherConfigCommandPending = false;
char pendingWeatherLocationName[32] = "";
char pendingWeatherLatitude[18] = "";
char pendingWeatherLongitude[18] = "";
char pendingWeatherTimezone[40] = "";
portMUX_TYPE commandMux = portMUX_INITIALIZER_UNLOCKED;

void handleSharedBleConnection(bool connected) {
  snprintf(lastStatus, sizeof(lastStatus),
           connected ? "Mac connected" : "Disconnected, advertising");
  screenDirty = true;
}

void sendStickLinkEvent(const char* app, const char* type, const char* name,
                        const char* text) {
  ++sequenceNumber;
  const String payload =
      stickLinkEncodeEvent(app, type, name, text, millis(), sequenceNumber);
  if (messageCharacteristic != nullptr) {
    messageCharacteristic->setValue(payload.c_str());
    if (sharedBleConnected()) {
      messageCharacteristic->notify();
    }
  }
  Serial.println(payload);
}

class StickLinkCommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    const std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }

    JsonDocument doc;
    const DeserializationError error =
        deserializeJson(doc, value.data(), value.size());
    if (error) {
      Serial.println("Stick Link command JSON decode failed");
      return;
    }

    const char* app = doc["app"] | "";
    const char* type = doc["type"] | "";
    const char* name = doc["name"] | "";
    if (strcmp(app, "weather") == 0 && strcmp(type, "config") == 0 &&
        strcmp(name, "set_location") == 0) {
      portENTER_CRITICAL(&commandMux);
      strlcpy(pendingWeatherLocationName, doc["location_name"] | "",
              sizeof(pendingWeatherLocationName));
      strlcpy(pendingWeatherLatitude, doc["latitude"] | "",
              sizeof(pendingWeatherLatitude));
      strlcpy(pendingWeatherLongitude, doc["longitude"] | "",
              sizeof(pendingWeatherLongitude));
      strlcpy(pendingWeatherTimezone, doc["timezone"] | "",
              sizeof(pendingWeatherTimezone));
      weatherConfigCommandPending = true;
      portEXIT_CRITICAL(&commandMux);
      return;
    }

    Serial.println("Unknown Stick Link BLE command");
  }
};

void processPendingCommands() {
  if (!weatherConfigCommandPending) {
    return;
  }

  char locationName[sizeof(pendingWeatherLocationName)];
  char latitude[sizeof(pendingWeatherLatitude)];
  char longitude[sizeof(pendingWeatherLongitude)];
  char timezone[sizeof(pendingWeatherTimezone)];

  portENTER_CRITICAL(&commandMux);
  strlcpy(locationName, pendingWeatherLocationName, sizeof(locationName));
  strlcpy(latitude, pendingWeatherLatitude, sizeof(latitude));
  strlcpy(longitude, pendingWeatherLongitude, sizeof(longitude));
  strlcpy(timezone, pendingWeatherTimezone, sizeof(timezone));
  weatherConfigCommandPending = false;
  portEXIT_CRITICAL(&commandMux);

  const bool ok =
      weatherAppApplyRemoteConfig(locationName, latitude, longitude, timezone);
  sendStickLinkEvent("weather", "config", ok ? "saved" : "save_failed",
                     ok ? "Weather location updated"
                        : "Weather location update failed");
}

String deviceInfoJson() {
  JsonDocument doc;
  doc["v"] = 1;
  doc["name"] = kStickLinkBleName;
  doc["service"] = kStickLinkServiceUuid;
  doc["audio_sample_rate"] = kStickLinkAudioSampleRate;
  doc["audio_format"] = "pcm12";
  doc["mic_capture_sample_rate"] = kRemoteMicCaptureSampleRate;
  doc["mic_magnification"] = kRemoteMicMagnification;
  doc["mic_noise_filter_level"] = kRemoteMicNoiseFilterLevel;
  doc["mic_over_sampling"] = kRemoteMicOverSampling;
  doc["codec_adc_volume_register"] = remoteMicCodecAdcVolumeRegister();
  doc["codec_adc_volume"] = remoteMicCodecAdcVolumeValue();
  doc["downsample_filter"] = "fir5";

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
                        sharedBleConnected() ? TFT_GREEN : TFT_ORANGE);
  displayedBleConnected = sharedBleConnected();
}

void drawStatusValue() {
  if (!appVisible) {
    return;
  }

  M5.Display.fillRect(kStatusValueX, kStatusValueY, kStatusValueW,
                      kStatusValueH, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(sharedBleConnected() ? TFT_GREEN : TFT_ORANGE,
                          TFT_BLACK);
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
  if (force || displayedBleConnected != sharedBleConnected()) {
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

int32_t captureFrameMono(const int16_t* input, size_t frame) {
  const int32_t left = input[frame * 2];
  const int32_t right = input[(frame * 2) + 1];
  return (left + right) / 2;
}

void downsampleCaptureChunk(const int16_t* input, int16_t* output,
                            size_t outputCount) {
  for (size_t i = 0; i < outputCount; ++i) {
    const size_t center = i * 2;
    const int32_t x0 = captureFrameMono(input, center);
    const int32_t xm2 = center >= 2 ? captureFrameMono(input, center - 2) : x0;
    const int32_t xm1 = center >= 1 ? captureFrameMono(input, center - 1) : x0;
    const int32_t xp1 = center + 1 < kRemoteMicCaptureFramesPerChunk
                            ? captureFrameMono(input, center + 1)
                            : x0;
    const int32_t xp2 = center + 2 < kRemoteMicCaptureFramesPerChunk
                            ? captureFrameMono(input, center + 2)
                            : x0;
    output[i] =
        static_cast<int16_t>((xm2 + (4 * xm1) + (6 * x0) + (4 * xp1) + xp2) /
                             16);
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

void resetCapturePipeline() {
  captureWriteBuffer = 0;
  completedCaptureBuffer = -1;
  primingChunksRemaining = kRemoteMicPrimeChunks;
  memset(captureBuffers, 0, sizeof(captureBuffers));
  memset(audioBuffer, 0, sizeof(audioBuffer));
  memset(pcm12Buffer, 0, sizeof(pcm12Buffer));
}

void sendCompletedCaptureChunk(const int16_t* samples) {
  if (!recording) {
    return;
  }

  if (primingChunksRemaining > 0) {
    --primingChunksRemaining;
    return;
  }

  ++audioChunkCount;
  if (sharedBleConnected() && audioCharacteristic != nullptr) {
    downsampleCaptureChunk(samples, audioBuffer, kStickLinkAudioSamplesPerChunk);
    encodePcm12Chunk(audioBuffer, kStickLinkAudioSamplesPerChunk, pcm12Buffer);
    audioCharacteristic->setValue(pcm12Buffer, sizeof(pcm12Buffer));
    audioCharacteristic->notify();
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

bool applyRemoteMicCodecInputLevel() {
  m5gfx::i2c::i2c_temporary_switcher_t backupI2cSetting(1, GPIO_NUM_47,
                                                         GPIO_NUM_48);
  bool ok = true;
  ok &= M5.In_I2C.writeRegister8(remoteMicCodecI2cAddress(), 0x00, 0x80,
                                 remoteMicCodecI2cFrequency());
  ok &= M5.In_I2C.writeRegister8(remoteMicCodecI2cAddress(), 0x01, 0xBA,
                                 remoteMicCodecI2cFrequency());
  ok &= M5.In_I2C.writeRegister8(remoteMicCodecI2cAddress(), 0x02, 0x18,
                                 remoteMicCodecI2cFrequency());
  ok &= M5.In_I2C.writeRegister8(remoteMicCodecI2cAddress(), 0x0D, 0x01,
                                 remoteMicCodecI2cFrequency());
  ok &= M5.In_I2C.writeRegister8(remoteMicCodecI2cAddress(), 0x0E, 0x02,
                                 remoteMicCodecI2cFrequency());
  ok &= M5.In_I2C.writeRegister8(remoteMicCodecI2cAddress(), 0x14, 0x10,
                                 remoteMicCodecI2cFrequency());
  ok &= M5.In_I2C.writeRegister8(
      remoteMicCodecI2cAddress(), remoteMicCodecAdcVolumeRegister(),
      remoteMicCodecAdcVolumeValue(), remoteMicCodecI2cFrequency());
  ok &= M5.In_I2C.writeRegister8(remoteMicCodecI2cAddress(), 0x1C, 0x6A,
                                 remoteMicCodecI2cFrequency());
  uint8_t value = 0;
  if (!ok) {
    Serial.println("Remote Mic codec ADC volume write failed");
  } else {
    value = M5.In_I2C.readRegister8(
        remoteMicCodecI2cAddress(), remoteMicCodecAdcVolumeRegister(),
        remoteMicCodecI2cFrequency());
    Serial.printf("Remote Mic codec ADC volume set/read: 0x%02X/0x%02X\n",
                  remoteMicCodecAdcVolumeValue(), value);
  }
  backupI2cSetting.restore();
  lastCodecInputLevelOk = ok;
  lastCodecInputLevelReadback = value;
  return ok;
}

void startRemoteMicInput() {
  if (remoteMicInputReady && M5.Mic.isRunning()) {
    return;
  }

  if (M5.Mic.isRunning()) {
    while (M5.Mic.isRecording()) {
      delay(1);
    }
    M5.Mic.end();
  }
  if (M5.Speaker.isRunning()) {
    M5.Speaker.end();
  }
  configureRemoteMicInput();
  M5.Mic.begin();
  applyRemoteMicCodecInputLevel();
  delay(80);
  remoteMicInputReady = M5.Mic.isRunning();
}

void stopRemoteMicInput() {
  if (M5.Mic.isRunning()) {
    while (M5.Mic.isRecording()) {
      delay(1);
    }
    M5.Mic.end();
  }
  remoteMicInputReady = false;
  resetCapturePipeline();
}

}  // namespace

void remoteMicAppBegin() {
  if (bleStarted) {
    return;
  }

  sharedBleBegin(kSharedBleDeviceName);
  sharedBleRegisterConnectionCallback(handleSharedBleConnection);

  BLEService* service = sharedBleServer()->createService(kStickLinkServiceUuid);

  messageCharacteristic = service->createCharacteristic(
      kStickLinkMessageCharacteristicUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_WRITE);
  messageCharacteristic->setCallbacks(new StickLinkCommandCallbacks());
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
  sharedBleUseAdvertisement(kSharedBleDeviceName, kStickLinkServiceUuid);

  bleStarted = true;
  snprintf(lastStatus, sizeof(lastStatus), "Advertising");
  Serial.println("Remote Mic BLE advertising started");
}

void remoteMicAppStart() {
  if (!sharedBleAdvertisementMatches(kSharedBleDeviceName,
                                     kStickLinkServiceUuid)) {
    Serial.println("[remote-mic] start app; requesting Stick Link BLE mode");
    sharedBleHandoffToAdvertisement(kSharedBleDeviceName, kStickLinkServiceUuid);
  } else {
    Serial.println("[remote-mic] start app; Stick Link BLE mode already active");
  }
  appVisible = true;
  screenDirty = true;
  startRemoteMicInput();
  drawRemoteMicScreen();
}

void remoteMicAppUpdate() {
  processPendingCommands();

  if (appVisible && !remoteMicInputReady) {
    startRemoteMicInput();
  }

  if (appVisible && M5.Mic.isEnabled()) {
    const uint8_t submittedBuffer = captureWriteBuffer;
    if (M5.Mic.record(captureBuffers[submittedBuffer],
                      kRemoteMicCaptureSamplesPerChunk,
                      kRemoteMicCaptureSampleRate, true)) {
      if (completedCaptureBuffer >= 0) {
        sendCompletedCaptureChunk(captureBuffers[completedCaptureBuffer]);
      }
      completedCaptureBuffer = submittedBuffer;
      captureWriteBuffer = 1 - submittedBuffer;
    }
  }

  if (screenDirty) {
    drawRemoteMicScreen();
  } else {
    drawDynamicRegions();
  }
}

void remoteMicAppPoll() {
  processPendingCommands();
}

void remoteMicAppStartRecording() {
  if (recording) {
    return;
  }

  ++sequenceNumber;
  audioChunkCount = 0;
  lastSendAt = millis();
  resetCapturePipeline();
  startRemoteMicInput();

  recording = true;
  char startText[96];
  snprintf(startText, sizeof(startText),
           lastCodecInputLevelOk
               ? "Remote Mic recording started codec 0x%02X/0x%02X"
               : "Remote Mic recording started codec write failed",
           remoteMicCodecAdcVolumeValue(), lastCodecInputLevelReadback);
  const String payload =
      stickLinkEncodeVoiceEvent("start", startText, lastSendAt, sequenceNumber);
  if (messageCharacteristic != nullptr) {
    messageCharacteristic->setValue(payload.c_str());
    if (sharedBleConnected()) {
      messageCharacteristic->notify();
    }
  }

  snprintf(lastStatus, sizeof(lastStatus),
           sharedBleConnected() ? "Recording" : "Recording, no Mac");
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
  if (completedCaptureBuffer >= 0) {
    sendCompletedCaptureChunk(captureBuffers[completedCaptureBuffer]);
    completedCaptureBuffer = -1;
  }

  ++sequenceNumber;
  const String payload =
      stickLinkEncodeVoiceEvent("stop", "Remote Mic recording stopped",
                                lastSendAt, sequenceNumber);
  if (messageCharacteristic != nullptr) {
    messageCharacteristic->setValue(payload.c_str());
    if (sharedBleConnected()) {
      messageCharacteristic->notify();
    }
  }
  snprintf(lastStatus, sizeof(lastStatus),
           sharedBleConnected() ? "Stopped" : "No Mac connected");
  Serial.println(payload);
  screenDirty = true;
  drawRemoteMicScreen();
}

void remoteMicAppStop() {
  remoteMicAppStopRecording();
  stopRemoteMicInput();
  appVisible = false;
}

bool remoteMicAppConnected() {
  return sharedBleConnected();
}
