#include "sensor_app.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <M5GFX.h>
#include <M5Unified.h>

#include "stick_link_protocol.h"

namespace {

constexpr uint32_t kStatusRedrawMs = 1000;

BLEServer* bleServer = nullptr;
BLECharacteristic* messageCharacteristic = nullptr;
BLECharacteristic* deviceInfoCharacteristic = nullptr;

bool bleStarted = false;
bool bleConnected = false;
bool appVisible = false;
bool screenDirty = false;
uint32_t sequenceNumber = 0;
uint32_t lastRedrawAt = 0;
uint32_t lastSendAt = 0;
char lastStatus[96] = "Advertising";

class SensorServerCallbacks : public BLEServerCallbacks {
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

  String output;
  serializeJson(doc, output);
  return output;
}

void drawSensorScreen() {
  if (!appVisible) {
    return;
  }

  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.print("Sensor App");

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
  M5.Display.printf("A: send  Seq: %lu", static_cast<unsigned long>(sequenceNumber));

  if (lastSendAt > 0) {
    M5.Display.setCursor(8, 124);
    M5.Display.printf("Last send: %lums", static_cast<unsigned long>(lastSendAt));
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

void sensorAppBegin() {
  if (bleStarted) {
    return;
  }

  BLEDevice::init(kStickLinkBleName);
  BLEDevice::setMTU(185);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new SensorServerCallbacks());

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

  service->start();
  startAdvertising();

  bleStarted = true;
  snprintf(lastStatus, sizeof(lastStatus), "Advertising");
  Serial.println("Sensor BLE advertising started");
}

void sensorAppStart() {
  appVisible = true;
  screenDirty = true;
  drawSensorScreen();
}

void sensorAppUpdate() {
  const uint32_t now = millis();
  if (screenDirty || (appVisible && now - lastRedrawAt >= kStatusRedrawMs)) {
    drawSensorScreen();
  }
}

void sensorAppSendButtonA() {
  ++sequenceNumber;
  lastSendAt = millis();

  const String payload = stickLinkEncodeButtonEvent(
      "sensor", "ButtonA", "ButtonA pressed from Sensor App", lastSendAt,
      sequenceNumber);

  Serial.print("Sensor event: ");
  Serial.println(payload);

  if (messageCharacteristic == nullptr) {
    snprintf(lastStatus, sizeof(lastStatus), "BLE not ready");
  } else if (!bleConnected) {
    snprintf(lastStatus, sizeof(lastStatus), "No Mac connected");
    messageCharacteristic->setValue(payload.c_str());
  } else {
    messageCharacteristic->setValue(payload.c_str());
    messageCharacteristic->notify();
    snprintf(lastStatus, sizeof(lastStatus), "Sent ButtonA event");
  }

  screenDirty = true;
  drawSensorScreen();
}

void sensorAppStop() {
  appVisible = false;
}

bool sensorAppConnected() {
  return bleConnected;
}
