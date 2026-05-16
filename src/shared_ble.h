#pragma once

#include <BLEAdvertising.h>
#include <BLEServer.h>

constexpr const char* kSharedBleDeviceName = "StickS3 Link";
constexpr const char* kCodeBuddyBleDeviceName = "Codex-StickS3";

using SharedBleConnectionCallback = void (*)(bool connected);

void sharedBleBegin(const char* deviceName);
BLEServer* sharedBleServer();
bool sharedBleConnected();
void sharedBleAddAdvertisedService(const char* serviceUuid);
void sharedBleStartAdvertising();
void sharedBleSetDeviceName(const char* deviceName);
void sharedBleUseAdvertisement(const char* deviceName, const char* serviceUuid);
void sharedBleHandoffToDeviceName(const char* deviceName);
void sharedBleHandoffToAdvertisement(const char* deviceName,
                                     const char* serviceUuid);
bool sharedBleRegisterConnectionCallback(SharedBleConnectionCallback callback);
bool sharedBleSecure();
void sharedBleClearBonds();
