#include <Arduino.h>
#include <M5GFX.h>
#include <M5Unified.h>

#include "code_buddy_app.h"
#include "remote_mic_app.h"
#include "status_bar.h"
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
    {"Remote Mic"},
    {"CodeBuddy"},
};

constexpr size_t kAppCount = sizeof(kApps) / sizeof(kApps[0]);

Screen currentScreen = Screen::Menu;
size_t selectedApp = 0;
size_t runningApp = 0;
bool buttonAHoldHandled = false;
bool buttonBHoldHandled = false;
bool remoteMicButtonAActive = false;
bool codeBuddyLaunchButtonAActive = false;

void drawMenu() {
  M5.Display.clear(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.println("App Launcher");
  statusBarReset();
  statusBarDraw();

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
    remoteMicAppStart();
    return;
  }

  if (appIndex == 2) {
    codeBuddyAppStart();
    return;
  }

  drawPlaceholderApp(appIndex);
}

void launchSelectedApp() {
  runningApp = selectedApp;
  currentScreen = Screen::App;
  buttonAHoldHandled = false;
  buttonBHoldHandled = false;
  remoteMicButtonAActive = false;
  codeBuddyLaunchButtonAActive = runningApp == 2 && M5.BtnA.isPressed();
  drawApp(runningApp);
}

void returnToMenu() {
  if (runningApp == 0) {
    weatherAppStop();
  } else if (runningApp == 1) {
    remoteMicAppStop();
  } else if (runningApp == 2) {
    codeBuddyAppStop();
  }

  currentScreen = Screen::Menu;
  buttonAHoldHandled = false;
  buttonBHoldHandled = false;
  remoteMicButtonAActive = false;
  codeBuddyLaunchButtonAActive = false;
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
  if (runningApp == 1) {
    if (consumeLongPress(M5.BtnB, buttonBHoldHandled)) {
      remoteMicAppStopRecording();
      returnToMenu();
      return;
    }

    if (M5.BtnA.wasPressed()) {
      remoteMicButtonAActive = true;
      remoteMicAppStartRecording();
    }

    if (remoteMicButtonAActive && M5.BtnA.wasReleased()) {
      remoteMicButtonAActive = false;
      remoteMicAppStopRecording();
    }

    return;
  }

  if (runningApp == 2) {
    if (codeBuddyLaunchButtonAActive) {
      if (!M5.BtnA.isPressed()) {
        codeBuddyLaunchButtonAActive = false;
      }
      return;
    }

    if (M5.BtnPWR.wasClicked() || M5.Power.getKeyState() == 0x02) {
      codeBuddyAppToggleScreen();
      return;
    }

    if (codeBuddyAppScreenOff()) {
      if (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
        codeBuddyAppWake();
      }
      return;
    }

    if (consumeLongPress(M5.BtnA, buttonAHoldHandled)) {
      codeBuddyAppButtonALong();
      if (codeBuddyAppWantsLauncher()) {
        returnToMenu();
      }
      return;
    }

    if (M5.BtnA.wasClicked()) {
      codeBuddyAppButtonA();
    } else if (M5.BtnB.wasClicked()) {
      codeBuddyAppButtonB();
    }
    if (codeBuddyAppWantsLauncher()) {
      returnToMenu();
    }
    return;
  }

  if (consumeLongPress(M5.BtnA, buttonAHoldHandled) ||
      consumeLongPress(M5.BtnB, buttonBHoldHandled)) {
    returnToMenu();
    return;
  }

  if (runningApp == 0 && M5.BtnA.wasClicked()) {
    weatherAppRefresh();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
  cfg.internal_imu = true;
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.setTextWrap(false);
  weatherAppBegin();
  remoteMicAppBegin();
  codeBuddyAppBegin();
  drawMenu();
}

void loop() {
  M5.update();
  remoteMicAppPoll();

  switch (currentScreen) {
    case Screen::Menu:
      handleMenuInput();
      if (currentScreen == Screen::Menu) {
        statusBarUpdate();
      }
      break;
    case Screen::App:
      handleAppInput();
      if (runningApp == 0) {
        weatherAppUpdate();
      } else if (runningApp == 1) {
        remoteMicAppUpdate();
      } else if (runningApp == 2) {
        codeBuddyAppUpdate();
      }
      break;
  }
}
