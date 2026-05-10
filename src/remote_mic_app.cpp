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

constexpr uint32_t kStatusRedrawMs = 1000;

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
uint32_t lastRedrawAt = 0;
uint32_t lastSendAt = 0;
uint32_t audioChunkCount = 0;
char lastStatus[96] = "Advertising";
int16_t audioBuffer[kStickLinkAudioSamplesPerChunk] = {};

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

  String output;
  serializeJson(doc, output);
  return output;
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

  M5.Display.fillCircle(M5.Display.width() - 18, 16, 5,
                        bleConnected ? TFT_GREEN : TFT_ORANGE);

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
  M5.Display.setCursor(8, 86);
  M5.Display.setTextColor(bleConnected ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  M5.Display.print(lastStatus);

  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setCursor(8, 110);
  M5.Display.printf("Hold A: talk  Seq: %lu",
                    static_cast<unsigned long>(sequenceNumber));

  if (lastSendAt > 0) {
    M5.Display.setCursor(8, 124);
    M5.Display.printf("Chunks: %lu", static_cast<unsigned long>(audioChunkCount));
  }

  lastRedrawAt = millis();
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
    ++audioChunkCount;
    if (bleConnected && audioCharacteristic != nullptr) {
      audioCharacteristic->setValue(
          reinterpret_cast<uint8_t*>(audioBuffer),
          kStickLinkAudioSamplesPerChunk * sizeof(int16_t));
      audioCharacteristic->notify();
    }
    screenDirty = true;
  }

  const uint32_t now = millis();
  if (screenDirty || (appVisible && now - lastRedrawAt >= kStatusRedrawMs)) {
    drawRemoteMicScreen();
  }
}

void remoteMicAppStartRecording() {
  if (recording) {
    return;
  }

  ++sequenceNumber;
  audioChunkCount = 0;
  lastSendAt = millis();

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

  snprintf(lastStatus, sizeof(lastStatus), bleConnected ? "Recording" : "Recording, no Mac");
  Serial.println(payload);
  screenDirty = true;
  drawRemoteMicScreen();
}

void remoteMicAppStopRecording() {
  if (!recording) {
    return;
  }

  recording = false;
  ++sequenceNumber;
  lastSendAt = millis();

  while (M5.Mic.isRecording()) {
    delay(1);
  }

  const String payload =
      stickLinkEncodeVoiceEvent("stop", "Remote Mic recording stopped",
                                lastSendAt, sequenceNumber);

  if (messageCharacteristic != nullptr) {
    messageCharacteristic->setValue(payload.c_str());
    if (bleConnected) {
      messageCharacteristic->notify();
    }
  }

  snprintf(lastStatus, sizeof(lastStatus), bleConnected ? "Sent recording" : "No Mac connected");
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
