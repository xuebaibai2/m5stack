#include "status_bar.h"

#include <Arduino.h>
#include <M5GFX.h>
#include <M5Unified.h>

#include "remote_mic_app.h"
#include "weather_app.h"

namespace {

constexpr uint32_t kStatusBarRefreshMs = 2000;
constexpr int kStatusBarX = 142;
constexpr int kStatusBarY = 4;
constexpr int kStatusBarW = 96;
constexpr int kStatusBarH = 18;
constexpr int kWifiIconX = 150;
constexpr int kBluetoothIconX = 179;
constexpr int kIconY = 13;
constexpr int kBatteryTextX = 196;
constexpr int kBatteryTextY = 8;

bool displayedWifiConnected = false;
bool displayedBluetoothConnected = false;
int32_t displayedBatteryLevel = -1;
bool hasDrawnStatusBar = false;
uint32_t lastStatusBarRefreshAt = 0;

void drawWifiIcon(bool connected) {
  const uint16_t color = connected ? TFT_GREEN : TFT_DARKGREY;
  M5.Display.drawCircle(kWifiIconX, kIconY, 7, color);
  M5.Display.drawCircle(kWifiIconX, kIconY, 4, color);
  M5.Display.fillRect(kWifiIconX - 8, kIconY, 17, 8, TFT_BLACK);
  M5.Display.fillCircle(kWifiIconX, kIconY + 3, 2, color);
}

void drawBluetoothIcon(bool connected) {
  const uint16_t color = connected ? TFT_BLUE : TFT_DARKGREY;
  M5.Display.drawLine(kBluetoothIconX, kIconY - 7, kBluetoothIconX,
                      kIconY + 7, color);
  M5.Display.drawLine(kBluetoothIconX, kIconY - 7, kBluetoothIconX + 6,
                      kIconY - 2, color);
  M5.Display.drawLine(kBluetoothIconX + 6, kIconY - 2, kBluetoothIconX - 5,
                      kIconY + 6, color);
  M5.Display.drawLine(kBluetoothIconX - 5, kIconY - 6, kBluetoothIconX + 6,
                      kIconY + 2, color);
  M5.Display.drawLine(kBluetoothIconX + 6, kIconY + 2, kBluetoothIconX,
                      kIconY + 7, color);
}

void drawBatteryText(int32_t batteryLevel) {
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(kBatteryTextX, kBatteryTextY);
  if (batteryLevel >= 0 && batteryLevel <= 100) {
    M5.Display.printf("%3ld%%", static_cast<long>(batteryLevel));
  } else {
    M5.Display.print(" --%");
  }
}

void drawStatusBar(bool force) {
  const bool wifiConnected = weatherAppWifiConnected();
  const bool bluetoothConnected = remoteMicAppConnected();
  const int32_t batteryLevel = M5.Power.getBatteryLevel();

  if (force || !hasDrawnStatusBar ||
      wifiConnected != displayedWifiConnected ||
      bluetoothConnected != displayedBluetoothConnected ||
      batteryLevel != displayedBatteryLevel) {
    M5.Display.fillRect(kStatusBarX, kStatusBarY, kStatusBarW, kStatusBarH,
                        TFT_BLACK);
    drawWifiIcon(wifiConnected);
    drawBluetoothIcon(bluetoothConnected);
    drawBatteryText(batteryLevel);

    displayedWifiConnected = wifiConnected;
    displayedBluetoothConnected = bluetoothConnected;
    displayedBatteryLevel = batteryLevel;
    hasDrawnStatusBar = true;
  }

  lastStatusBarRefreshAt = millis();
}

}  // namespace

void statusBarDraw() {
  drawStatusBar(true);
}

void statusBarUpdate() {
  const uint32_t now = millis();
  if (!hasDrawnStatusBar || now - lastStatusBarRefreshAt >= kStatusBarRefreshMs) {
    drawStatusBar(false);
  }
}

void statusBarReset() {
  hasDrawnStatusBar = false;
  displayedBatteryLevel = -1;
}
