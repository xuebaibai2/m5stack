#include <Arduino.h>
#include <M5GFX.h>
#include <M5Unified.h>

#include "sensor_app.h"
#include "weather_app.h"

namespace {

constexpr uint32_t kLongPressMs = 900;

enum class Screen {
  Menu,
  App,
};

struct AppDefinition {
  const char* name;
};

const AppDefinition kApps[] = {
    {"Weather App"},
    {"Sensor App"},
    {"Settings App"},
};

constexpr size_t kAppCount = sizeof(kApps) / sizeof(kApps[0]);

Screen currentScreen = Screen::Menu;
size_t selectedApp = 0;
size_t runningApp = 0;
bool buttonAHoldHandled = false;
bool buttonBHoldHandled = false;

void drawWifiIndicator() {
  const bool connected = weatherAppWifiConnected();
  const int x = M5.Display.width() - 48;
  M5.Display.fillCircle(x, 12, 4, connected ? TFT_GREEN : TFT_DARKGREY);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(x + 8, 8);
  M5.Display.print("WiFi");
}

void drawMenu() {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.println("App Launcher");
  drawWifiIndicator();

  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, 32);
  M5.Display.println("B: select  A: launch");

  for (size_t i = 0; i < kAppCount; ++i) {
    const int y = 52 + static_cast<int>(i) * 28;
    const bool isSelected = i == selectedApp;

    M5.Display.setCursor(8, y);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(isSelected ? TFT_BLACK : TFT_WHITE,
                            isSelected ? TFT_WHITE : TFT_BLACK);
    M5.Display.printf("%c %s", isSelected ? '>' : ' ', kApps[i].name);
  }

  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void drawPlaceholderApp(size_t appIndex) {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 32);
  M5.Display.println(kApps[appIndex].name);
}

void drawApp(size_t appIndex) {
  if (appIndex == 0) {
    weatherAppStart();
    return;
  }

  if (appIndex == 1) {
    sensorAppStart();
    return;
  }

  drawPlaceholderApp(appIndex);
}

void launchSelectedApp() {
  runningApp = selectedApp;
  currentScreen = Screen::App;
  buttonAHoldHandled = false;
  buttonBHoldHandled = false;
  drawApp(runningApp);
}

void returnToMenu() {
  if (runningApp == 0) {
    weatherAppStop();
  } else if (runningApp == 1) {
    sensorAppStop();
  }

  currentScreen = Screen::Menu;
  buttonAHoldHandled = false;
  buttonBHoldHandled = false;
  drawMenu();
}

void handleMenuInput() {
  if (M5.BtnB.wasPressed()) {
    selectedApp = (selectedApp + 1) % kAppCount;
    drawMenu();
  }

  if (M5.BtnA.wasPressed()) {
    launchSelectedApp();
  }
}

bool consumeLongPress(m5::Button_Class& button, bool& handled) {
  if (!button.isPressed()) {
    handled = false;
    return false;
  }

  if (!handled && button.pressedFor(kLongPressMs)) {
    handled = true;
    return true;
  }

  return false;
}

void handleAppInput() {
  if (consumeLongPress(M5.BtnA, buttonAHoldHandled) ||
      consumeLongPress(M5.BtnB, buttonBHoldHandled)) {
    returnToMenu();
    return;
  }

  if (runningApp == 0 && M5.BtnA.wasClicked()) {
    weatherAppRefresh();
  } else if (runningApp == 1 && M5.BtnA.wasClicked()) {
    sensorAppSendButtonA();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.setTextWrap(false);
  weatherAppBegin();
  sensorAppBegin();
  drawMenu();
}

void loop() {
  M5.update();

  switch (currentScreen) {
    case Screen::Menu:
      handleMenuInput();
      break;
    case Screen::App:
      handleAppInput();
      if (runningApp == 0) {
        weatherAppUpdate();
      } else if (runningApp == 1) {
        sensorAppUpdate();
      }
      break;
  }
}
