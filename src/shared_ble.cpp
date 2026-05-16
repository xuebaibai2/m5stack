#include "shared_ble.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <esp_gap_ble_api.h>
#include <map>

namespace {

constexpr size_t kMaxConnectionCallbacks = 4;

BLEServer* server = nullptr;
bool connected = false;
bool started = false;
SharedBleConnectionCallback connectionCallbacks[kMaxConnectionCallbacks] = {};

void configureAdvertisement(const char* deviceName, const char* serviceUuid) {
  if (deviceName == nullptr || deviceName[0] == '\0' || serviceUuid == nullptr ||
      serviceUuid[0] == '\0') {
    return;
  }

  BLEAdvertisementData advertisement;
  advertisement.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC |
                         ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  advertisement.setCompleteServices(BLEUUID(serviceUuid));

  BLEAdvertisementData scanResponse;
  scanResponse.setName(deviceName);

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->setAdvertisementData(advertisement);
  advertising->setScanResponseData(scanResponse);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
  esp_ble_gap_set_device_name(deviceName);
  Serial.printf("[ble] configured advertisement name='%s' service=%s\n",
                deviceName, serviceUuid);
  delay(50);
}

class SharedBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    connected = true;
    for (SharedBleConnectionCallback callback : connectionCallbacks) {
      if (callback != nullptr) {
        callback(true);
      }
    }
  }

  void onDisconnect(BLEServer*) override {
    connected = false;
    for (SharedBleConnectionCallback callback : connectionCallbacks) {
      if (callback != nullptr) {
        callback(false);
      }
    }
    sharedBleStartAdvertising();
  }
};

}  // namespace

void sharedBleBegin(const char* deviceName) {
  if (started) {
    return;
  }

  BLEDevice::init(deviceName);
  BLEDevice::setMTU(185);
  server = BLEDevice::createServer();
  server->setCallbacks(new SharedBleServerCallbacks());
  started = true;
}

BLEServer* sharedBleServer() {
  return server;
}

bool sharedBleConnected() {
  return connected;
}

void sharedBleAddAdvertisedService(const char* serviceUuid) {
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(serviceUuid);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMaxPreferred(0x12);
}

void sharedBleStartAdvertising() {
  if (!started) {
    return;
  }

  Serial.println("[ble] start advertising");
  BLEDevice::startAdvertising();
}

void sharedBleSetDeviceName(const char* deviceName) {
  if (!started || deviceName == nullptr || deviceName[0] == '\0') {
    return;
  }

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->stop();
  esp_ble_gap_set_device_name(deviceName);
  if (!connected) {
    sharedBleStartAdvertising();
  }
}

void sharedBleUseAdvertisement(const char* deviceName, const char* serviceUuid) {
  if (!started) {
    return;
  }

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->stop();
  configureAdvertisement(deviceName, serviceUuid);
  if (!connected) {
    Serial.printf("[ble] advertise mode name='%s'\n", deviceName);
    sharedBleStartAdvertising();
  }
}

void sharedBleHandoffToDeviceName(const char* deviceName) {
  if (!started || deviceName == nullptr || deviceName[0] == '\0') {
    return;
  }

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->stop();
  esp_ble_gap_set_device_name(deviceName);

  if (server == nullptr || !connected) {
    Serial.printf("[ble] handoff advertise name='%s' no active central\n",
                  deviceName);
    sharedBleStartAdvertising();
    return;
  }

  const std::map<uint16_t, conn_status_t> peers = server->getPeerDevices(false);
  if (peers.empty()) {
    connected = false;
    Serial.printf("[ble] handoff advertise name='%s' no peer records\n",
                  deviceName);
    sharedBleStartAdvertising();
    return;
  }

  Serial.printf("[ble] handoff disconnect %u central(s), next name='%s'\n",
                static_cast<unsigned>(peers.size()), deviceName);
  for (const auto& peer : peers) {
    server->disconnect(peer.first);
  }
}

void sharedBleHandoffToAdvertisement(const char* deviceName,
                                     const char* serviceUuid) {
  if (!started || deviceName == nullptr || deviceName[0] == '\0' ||
      serviceUuid == nullptr || serviceUuid[0] == '\0') {
    return;
  }

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->stop();
  configureAdvertisement(deviceName, serviceUuid);

  if (server == nullptr || !connected) {
    Serial.printf("[ble] handoff advertise name='%s' no active central\n",
                  deviceName);
    sharedBleStartAdvertising();
    return;
  }

  const std::map<uint16_t, conn_status_t> peers = server->getPeerDevices(false);
  if (peers.empty()) {
    connected = false;
    Serial.printf("[ble] handoff advertise name='%s' no peer records\n",
                  deviceName);
    sharedBleStartAdvertising();
    return;
  }

  Serial.printf("[ble] handoff disconnect %u central(s), next name='%s'\n",
                static_cast<unsigned>(peers.size()), deviceName);
  for (const auto& peer : peers) {
    server->disconnect(peer.first);
  }
}

bool sharedBleRegisterConnectionCallback(
    SharedBleConnectionCallback callback) {
  if (callback == nullptr) {
    return false;
  }

  for (SharedBleConnectionCallback existing : connectionCallbacks) {
    if (existing == callback) {
      return true;
    }
  }

  for (SharedBleConnectionCallback& slot : connectionCallbacks) {
    if (slot == nullptr) {
      slot = callback;
      return true;
    }
  }

  return false;
}

bool sharedBleSecure() {
  return false;
}

void sharedBleClearBonds() {
  int count = esp_ble_get_bond_device_num();
  if (count <= 0) {
    return;
  }

  esp_ble_bond_dev_t* devices = static_cast<esp_ble_bond_dev_t*>(
      malloc(count * sizeof(esp_ble_bond_dev_t)));
  if (devices == nullptr) {
    return;
  }

  esp_ble_get_bond_device_list(&count, devices);
  for (int i = 0; i < count; ++i) {
    esp_ble_remove_bond_device(devices[i].bd_addr);
  }
  free(devices);
}
