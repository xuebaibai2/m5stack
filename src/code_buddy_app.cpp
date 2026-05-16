#include "code_buddy_app.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLECharacteristic.h>
#include <BLEServer.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <mbedtls/base64.h>
#include <time.h>

#include "codebuddy_runtime/about_info.h"
#include "codebuddy_runtime/board_compat.h"
#include "codebuddy_runtime/buddy.h"
#include "codebuddy_runtime/character.h"
#include "codebuddy_runtime/persona_logic.h"
#include "codebuddy_runtime/stats.h"
#include "codebuddy_runtime/utf8_text_logic.h"
#include "code_buddy_protocol.h"
#include "shared_ble.h"
#include "stick_link_protocol.h"

TFT_eSprite spr = TFT_eSprite(&M5.Lcd);

namespace {

constexpr const char* kCodeBuddyServiceUuid =
    "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kCodeBuddyRxUuid =
    "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* kCodeBuddyTxUuid =
    "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
constexpr int kCodeBuddyWidth = 135;
constexpr int kCodeBuddyHeight = 240;
constexpr size_t kRxQueueSize = 2048;
constexpr size_t kRxProcessChunkSize = 128;
constexpr uint32_t kShakeCheckMs = 50;
constexpr float kShakeDeltaThreshold = 0.8f;
constexpr uint8_t kPetPageCount = 2;
constexpr uint8_t kInfoPageCount = 6;
constexpr uint16_t kPanelColor = 0x2104;
constexpr uint16_t kHotColor = 0xFA20;

BLECharacteristic* txCharacteristic = nullptr;
Preferences preferences;
bool bleStarted = false;
bool appVisible = false;
bool screenDirty = false;
bool responseSent = false;
bool fsReady = false;
bool runtimeReady = false;
bool spriteCreated = false;
bool buddyMode = true;
bool gifAvailable = false;
bool screenOff = false;
bool exitToLauncher = false;
bool napping = false;
enum class CodeBuddyView : uint8_t { Home, Pet, Info };
enum class CodeBuddyOverlay : uint8_t { None, Menu, Settings, Reset };
CodeBuddyView currentView = CodeBuddyView::Home;
CodeBuddyOverlay overlay = CodeBuddyOverlay::None;
uint8_t petPage = 0;
uint8_t infoPage = 0;
uint8_t menuSel = 0;
uint8_t settingsSel = 0;
uint8_t resetSel = 0;
uint8_t brightnessLevel = 4;
uint8_t resetConfirmIdx = 0xFF;
uint8_t transcriptScroll = 0;
uint16_t lastEntryGeneration = 0;
CodeBuddyState state;
char deviceName[24] = "Buddy";
char ownerDisplayName[32] = "";
char lastPromptId[sizeof(state.promptId)] = "";
char lineBuffer[1024] = "";
size_t lineLength = 0;
uint8_t rxQueue[kRxQueueSize] = {};
size_t rxHead = 0;
size_t rxTail = 0;
bool rxOverflow = false;
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

File transferFile;
bool transferActive = false;
uint32_t transferExpected = 0;
uint32_t transferWritten = 0;
uint32_t transferTotal = 0;
uint32_t transferTotalWritten = 0;
char transferName[24] = "";
PersonaState baseState = P_SLEEP;
PersonaState activeState = P_SLEEP;
uint32_t oneShotUntil = 0;
uint32_t promptArrivedMs = 0;
uint32_t lastInteractMs = 0;
uint32_t lastShakeCheckMs = 0;
uint32_t resetConfirmUntil = 0;
uint32_t napStartMs = 0;
int8_t faceDownFrames = 0;
float accelBaseline = 1.0f;

bool ensureFilesystem();
void wakeScreen();
bool promptPending();
void closeOverlays();
void drawOverlays(const Palette& palette);
void releaseRuntime();

bool ensureRuntimeReady() {
  if (runtimeReady) {
    return true;
  }

  ensureFilesystem();
  statsLoad();
  settingsLoad();
  petNameLoad();
  compatSetBrightnessPercent(20 + brightnessLevel * 20);
  if (deviceName[0] != '\0') {
    petNameSet(deviceName);
  }
  if (ownerDisplayName[0] != '\0') {
    ownerSet(ownerDisplayName);
  }
  buddyInit();
  if (!spriteCreated) {
    spr.createSprite(kCodeBuddyWidth, kCodeBuddyHeight);
    spriteCreated = true;
  }
  gifAvailable = characterInit(nullptr);
  buddyMode = !(gifAvailable && speciesIdxLoad() == 0xFF);
  buddySetPeek(false);
  characterSetPeek(false);
  runtimeReady = true;
  Serial.printf("[codebuddy] runtime ready heap=%u\n", ESP.getFreeHeap());
  return true;
}

void releaseRuntime() {
  if (transferFile) {
    transferFile.close();
  }
  transferActive = false;
  transferExpected = 0;
  transferWritten = 0;
  transferTotal = 0;
  transferTotalWritten = 0;
  transferName[0] = '\0';

  portENTER_CRITICAL(&rxMux);
  rxHead = 0;
  rxTail = 0;
  rxOverflow = false;
  portEXIT_CRITICAL(&rxMux);
  lineLength = 0;

  if (runtimeReady) {
    characterClose();
  }
  if (spriteCreated) {
    spr.deleteSprite();
    spriteCreated = false;
  }

  runtimeReady = false;
  gifAvailable = false;
  buddyMode = true;
  screenDirty = false;
  Serial.printf("[codebuddy] runtime released heap=%u\n", ESP.getFreeHeap());
}

void safeCopyName(char* dest, size_t destSize, const char* source) {
  if (destSize == 0) {
    return;
  }

  size_t j = 0;
  if (source != nullptr) {
    for (size_t i = 0; source[i] != '\0' && j < destSize - 1; ++i) {
      const char c = source[i];
      if (c != '"' && c != '\\' && c >= 0x20) {
        dest[j++] = c;
      }
    }
  }
  dest[j] = '\0';
}

void loadPreferences() {
  preferences.begin("codebuddy", false);
  if (preferences.isKey("name")) {
    preferences.getString("name", deviceName, sizeof(deviceName));
  }
  if (preferences.isKey("owner")) {
    preferences.getString("owner", ownerDisplayName, sizeof(ownerDisplayName));
  }
  preferences.end();
  if (deviceName[0] == '\0') {
    strlcpy(deviceName, "Buddy", sizeof(deviceName));
  }
}

void saveDeviceName(const char* name) {
  safeCopyName(deviceName, sizeof(deviceName), name);
  preferences.begin("codebuddy", false);
  preferences.putString("name", deviceName);
  preferences.end();
}

void saveOwnerName(const char* name) {
  safeCopyName(ownerDisplayName, sizeof(ownerDisplayName), name);
  preferences.begin("codebuddy", false);
  preferences.putString("owner", ownerDisplayName);
  preferences.end();
}

bool ensureFilesystem() {
  if (fsReady) {
    return true;
  }

  fsReady = LittleFS.begin(false);
  if (!fsReady) {
    Serial.println("[codebuddy] LittleFS mount failed, formatting");
    if (LittleFS.format()) {
      fsReady = LittleFS.begin(false);
    }
  }

  if (fsReady) {
    LittleFS.mkdir("/characters");
  }
  return fsReady;
}

uint32_t wipeDir(const char* dir) {
  File root = LittleFS.open(dir);
  if (!root || !root.isDirectory()) {
    LittleFS.mkdir(dir);
    return 0;
  }

  uint32_t freed = 0;
  File entry = root.openNextFile();
  while (entry) {
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", dir, entry.name());
    if (entry.isDirectory()) {
      entry.close();
      freed += wipeDir(path);
      LittleFS.rmdir(path);
    } else {
      freed += entry.size();
      entry.close();
      LittleFS.remove(path);
    }
    entry = root.openNextFile();
  }
  root.close();
  return freed;
}

uint32_t reclaimableCharacterBytes() {
  if (!ensureFilesystem()) {
    return 0;
  }

  File root = LittleFS.open("/characters");
  if (!root || !root.isDirectory()) {
    return 0;
  }

  uint32_t bytes = 0;
  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      File child = entry.openNextFile();
      while (child) {
        bytes += child.size();
        child.close();
        child = entry.openNextFile();
      }
    } else {
      bytes += entry.size();
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  return bytes;
}

void markPromptChange() {
  if (strcmp(lastPromptId, state.promptId) == 0) {
    return;
  }

  strlcpy(lastPromptId, state.promptId, sizeof(lastPromptId));
  responseSent = false;
}

void notifyHost(const String& payload);

bool promptPending() {
  return state.promptId[0] != '\0' && !responseSent;
}

void wakeScreen() {
  if (!screenOff) {
    return;
  }
  screenOff = false;
  M5.Display.wakeup();
  screenDirty = true;
  lastInteractMs = millis();
}

uint8_t wrapTextRows(const char* text, char rows[][48], uint8_t maxRows,
                     uint8_t width) {
  if (text == nullptr || text[0] == '\0' || maxRows == 0 || width == 0) {
    return 0;
  }

  uint8_t row = 0;
  size_t col = 0;
  rows[row][0] = '\0';
  for (const char* p = text; *p != '\0' && row < maxRows; ++p) {
    if (*p == '\r') {
      continue;
    }
    if (*p == '\n' || col >= width) {
      rows[row][col] = '\0';
      ++row;
      if (row >= maxRows) {
        break;
      }
      col = 0;
      rows[row][0] = '\0';
      if (*p == '\n') {
        continue;
      }
    }
    rows[row][col++] = *p;
    rows[row][col] = '\0';
  }

  if (row < maxRows && col > 0) {
    ++row;
  }
  return row;
}

void clipText(char* dest, size_t destSize, const char* source,
              uint8_t maxChars) {
  if (destSize == 0) {
    return;
  }
  if (source == nullptr) {
    dest[0] = '\0';
    return;
  }
  size_t n = 0;
  while (source[n] != '\0' && n < destSize - 1 && n < maxChars) {
    dest[n] = source[n];
    ++n;
  }
  dest[n] = '\0';
}

void nextPet() {
  if (!runtimeReady) {
    return;
  }
  if (!buddyMode && gifAvailable) {
    buddyMode = true;
    speciesIdxSave(buddySpeciesIdx());
  } else if (buddyMode &&
             buddySpeciesIdx() + 1 < buddySpeciesCount()) {
    buddyNextSpecies();
  } else if (gifAvailable) {
    buddyMode = false;
    speciesIdxSave(0xFF);
  } else {
    buddyNextSpecies();
  }
  buddySetPeek(currentView != CodeBuddyView::Home);
  characterSetPeek(currentView != CodeBuddyView::Home);
  screenDirty = true;
}

void deleteCharacters() {
  if (!ensureFilesystem()) {
    return;
  }
  if (runtimeReady) {
    characterClose();
  }
  wipeDir("/characters");
  gifAvailable = false;
  buddyMode = true;
  speciesIdxSave(buddySpeciesIdx());
  screenDirty = true;
}

void factoryReset() {
  preferences.begin("codebuddy", false);
  preferences.clear();
  preferences.end();
  Preferences buddyPrefs;
  buddyPrefs.begin("buddy", false);
  buddyPrefs.clear();
  buddyPrefs.end();
  if (ensureFilesystem()) {
    LittleFS.format();
  }
  sharedBleClearBonds();
  delay(300);
  ESP.restart();
}

void setLocalTime(JsonArray timeArray) {
  if (timeArray.size() != 2) {
    return;
  }

  const time_t localEpoch =
      static_cast<time_t>(timeArray[0].as<uint32_t>() +
                          timeArray[1].as<int32_t>());
  struct tm localTime;
  if (gmtime_r(&localEpoch, &localTime) == nullptr) {
    return;
  }

  m5::rtc_time_t rtcTime(static_cast<int8_t>(localTime.tm_hour),
                         static_cast<int8_t>(localTime.tm_min),
                         static_cast<int8_t>(localTime.tm_sec));
  m5::rtc_date_t rtcDate(static_cast<int16_t>(localTime.tm_year + 1900),
                         static_cast<int8_t>(localTime.tm_mon + 1),
                         static_cast<int8_t>(localTime.tm_mday),
                         static_cast<int8_t>(localTime.tm_wday));
  M5.Rtc.setTime(&rtcTime);
  M5.Rtc.setDate(&rtcDate);
}

void notifyAck(const char* command, bool ok, uint32_t n = 0,
               const char* error = nullptr) {
  notifyHost(codeBuddyEncodeAck(command, ok, n, error));
}

void notifyStatus() {
  JsonDocument doc;
  doc["ack"] = "status";
  doc["ok"] = true;
  doc["n"] = 0;
  JsonObject data = doc["data"].to<JsonObject>();
  data["name"] = deviceName;
  data["owner"] = ownerDisplayName;
  data["sec"] = sharedBleSecure();

  const int batteryMv = M5.Power.getBatteryVoltage();
  const int batteryMa = M5.Power.getBatteryCurrent();
  const int vbusMv = M5.Power.getVBUSVoltage();
  int batteryPct = (batteryMv - 3200) / 10;
  if (batteryPct < 0) {
    batteryPct = 0;
  } else if (batteryPct > 100) {
    batteryPct = 100;
  }

  JsonObject battery = data["bat"].to<JsonObject>();
  battery["pct"] = batteryPct;
  battery["mV"] = batteryMv;
  battery["mA"] = batteryMa;
  battery["usb"] = vbusMv > 4000;

  JsonObject sys = data["sys"].to<JsonObject>();
  sys["up"] = millis() / 1000;
  sys["heap"] = ESP.getFreeHeap();
  if (ensureFilesystem()) {
    sys["fsFree"] = LittleFS.totalBytes() - LittleFS.usedBytes();
    sys["fsTotal"] = LittleFS.totalBytes();
  }

  JsonObject statsObject = data["stats"].to<JsonObject>();
  statsObject["appr"] = stats().approvals;
  statsObject["deny"] = stats().denials;
  statsObject["vel"] = statsMedianVelocity();
  statsObject["nap"] = stats().napSeconds;
  statsObject["lvl"] = stats().level;

  String output;
  serializeJson(doc, output);
  notifyHost(output);
}

bool handleTransferCommand(JsonDocument& doc, const char* command) {
  if (strcmp(command, "char_begin") == 0) {
    if (!ensureFilesystem()) {
      notifyAck("char_begin", false, 0, "filesystem unavailable");
      return true;
    }

    const uint32_t total = doc["total"] | 0;
    const uint32_t available =
        (LittleFS.totalBytes() - LittleFS.usedBytes()) + reclaimableCharacterBytes();
    if (total > 0 && total + 4096 > available) {
      notifyAck("char_begin", false, available, "not enough storage");
      return true;
    }

    safeCopyName(transferName, sizeof(transferName), doc["name"] | "pet");
    if (runtimeReady) {
      characterClose();
    }
    wipeDir("/characters");
    char dir[80];
    snprintf(dir, sizeof(dir), "/characters/%s", transferName);
    LittleFS.mkdir(dir);
    transferActive = true;
    transferTotal = total;
    transferTotalWritten = 0;
    transferWritten = 0;
    transferExpected = 0;
    notifyAck("char_begin", true);
    screenDirty = true;
    return true;
  }

  if (!transferActive) {
    return false;
  }

  if (strcmp(command, "file") == 0) {
    const char* path = doc["path"];
    transferExpected = doc["size"] | 0;
    transferWritten = 0;
    if (!codeBuddyIsSafeTransferPath(path)) {
      notifyAck("file", false, 0, "unsafe path");
      return true;
    }

    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "/characters/%s/%s",
             transferName, path);
    transferFile = LittleFS.open(fullPath, "w");
    notifyAck("file", static_cast<bool>(transferFile));
    return true;
  }

  if (strcmp(command, "chunk") == 0) {
    const char* encoded = doc["d"];
    if (encoded == nullptr || !transferFile) {
      notifyAck("chunk", false);
      return true;
    }

    uint8_t buffer[300];
    size_t outLength = 0;
    const int rc = mbedtls_base64_decode(
        buffer, sizeof(buffer), &outLength,
        reinterpret_cast<const uint8_t*>(encoded), strlen(encoded));
    if (rc != 0) {
      notifyAck("chunk", false, transferWritten, "base64 decode failed");
      return true;
    }

    const size_t written = transferFile.write(buffer, outLength);
    transferWritten += written;
    transferTotalWritten += written;
    notifyAck("chunk", written == outLength, transferWritten);
    screenDirty = true;
    return true;
  }

  if (strcmp(command, "file_end") == 0) {
    const bool ok = transferFile &&
                    (transferExpected == 0 || transferExpected == transferWritten);
    if (transferFile) {
      transferFile.close();
    }
    notifyAck("file_end", ok, transferWritten);
    return true;
  }

  if (strcmp(command, "char_end") == 0) {
    transferActive = false;
    ensureRuntimeReady();
    gifAvailable = characterInit(transferName);
    if (gifAvailable) {
      buddyMode = false;
      speciesIdxSave(0xFF);
    }
    notifyAck("char_end", gifAvailable, transferTotalWritten);
    screenDirty = true;
    return true;
  }

  return false;
}

bool handleCommand(JsonDocument& doc, const char* command) {
  if (strcmp(command, "owner") == 0) {
    const char* name = doc["name"];
    if (name != nullptr) {
      saveOwnerName(name);
      if (runtimeReady) {
        ownerSet(name);
      }
    }
    notifyAck("owner", name != nullptr);
    screenDirty = true;
    return true;
  }

  if (strcmp(command, "name") == 0) {
    const char* name = doc["name"];
    if (name != nullptr) {
      saveDeviceName(name);
      if (runtimeReady) {
        petNameSet(name);
      }
    }
    notifyAck("name", name != nullptr);
    screenDirty = true;
    return true;
  }

  if (strcmp(command, "status") == 0) {
    notifyStatus();
    return true;
  }

  if (strcmp(command, "unpair") == 0) {
    sharedBleClearBonds();
    notifyAck("unpair", true);
    return true;
  }

  if (handleTransferCommand(doc, command)) {
    return true;
  }

  return false;
}

void applyLine(const char* line) {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, line);
  if (error) {
    Serial.printf("[codebuddy] json error: %s\n", error.c_str());
    return;
  }

  const char* command = doc["cmd"];
  if (command != nullptr && handleCommand(doc, command)) {
    return;
  }

  JsonArray timeArray = doc["time"];
  if (!timeArray.isNull()) {
    setLocalTime(timeArray);
    return;
  }

  if (codeBuddyApplyJson(line, state, millis())) {
    if (doc["tokens"].is<uint32_t>()) {
      statsOnBridgeTokens(doc["tokens"].as<uint32_t>());
    }
    markPromptChange();
    screenDirty = true;
  }
}

void feedBytes(const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    const char c = static_cast<char>(data[i]);
    if (c == '\n' || c == '\r') {
      if (lineLength > 0) {
        lineBuffer[lineLength] = '\0';
        applyLine(lineBuffer);
        lineLength = 0;
      }
    } else if (lineLength < sizeof(lineBuffer) - 1) {
      lineBuffer[lineLength++] = c;
    }
  }

}

void enqueueRxBytes(const uint8_t* data, size_t length) {
  portENTER_CRITICAL(&rxMux);
  for (size_t i = 0; i < length; ++i) {
    const size_t nextHead = (rxHead + 1) % kRxQueueSize;
    if (nextHead == rxTail) {
      rxOverflow = true;
      break;
    }
    rxQueue[rxHead] = data[i];
    rxHead = nextHead;
  }
  portEXIT_CRITICAL(&rxMux);
}

void processPendingRx() {
  uint8_t chunk[kRxProcessChunkSize];

  while (true) {
    size_t count = 0;
    bool overflow = false;
    portENTER_CRITICAL(&rxMux);
    overflow = rxOverflow;
    rxOverflow = false;
    while (rxTail != rxHead && count < sizeof(chunk)) {
      chunk[count++] = rxQueue[rxTail];
      rxTail = (rxTail + 1) % kRxQueueSize;
    }
    portEXIT_CRITICAL(&rxMux);

    if (overflow) {
      Serial.println("[codebuddy] RX queue overflow");
      lineLength = 0;
      notifyAck("rx", false, 0, "rx queue overflow");
    }

    if (count == 0) {
      return;
    }
    feedBytes(chunk, count);
  }
}

class CodeBuddyRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    const std::string value = characteristic->getValue();
    if (!value.empty()) {
      enqueueRxBytes(reinterpret_cast<const uint8_t*>(value.data()),
                     value.size());
    }
  }
};

void handleSharedBleConnection(bool) {
  screenDirty = true;
}

void notifyHost(const String& payload) {
  if (txCharacteristic == nullptr) {
    return;
  }

  String line = payload + "\n";
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(line.c_str());
  size_t sent = 0;
  while (sent < line.length()) {
    size_t chunk = line.length() - sent;
    if (chunk > 180) {
      chunk = 180;
    }
    txCharacteristic->setValue(const_cast<uint8_t*>(bytes + sent), chunk);
    sent += chunk;
    if (sharedBleConnected()) {
      txCharacteristic->notify();
      delay(4);
    }
  }
  Serial.print("[codebuddy] ");
  Serial.print(line);
}

void drawApproval(const Palette& palette) {
  const int area = 84;
  spr.fillRect(0, kCodeBuddyHeight - area, kCodeBuddyWidth, area, palette.bg);
  spr.drawFastHLine(0, kCodeBuddyHeight - area, kCodeBuddyWidth,
                    palette.textDim);
  spr.setTextSize(1);
  spr.setTextColor(palette.textDim, palette.bg);
  spr.setCursor(4, kCodeBuddyHeight - area + 4);
  spr.printf("approve? %lus",
             static_cast<unsigned long>((millis() - promptArrivedMs) / 1000));

  char toolLine[48];
  codeBuddyCopy(toolLine, sizeof(toolLine), state.promptTool);
  spr.setTextColor(palette.text, palette.bg);
  spr.setTextSize(utf8DisplayCells(toolLine) <= 10 ? 2 : 1);
  spr.setCursor(4, kCodeBuddyHeight - area + 18);
  spr.print(toolLine);

  char hintLines[6][48] = {};
  const uint8_t rows = utf8WrapInto(state.promptHint, hintLines, 6, 20, false);
  spr.setTextSize(1);
  spr.setTextColor(palette.textDim, palette.bg);
  for (uint8_t i = 0; i < rows && i < 2; ++i) {
    spr.setCursor(4, kCodeBuddyHeight - area + 42 + i * 12);
    spr.print(hintLines[i]);
  }

  if (responseSent) {
    spr.setTextColor(palette.textDim, palette.bg);
    spr.setCursor(4, kCodeBuddyHeight - 12);
    spr.print("sent...");
  } else {
    spr.setTextColor(TFT_GREEN, palette.bg);
    spr.setCursor(4, kCodeBuddyHeight - 12);
    spr.print("A: approve");
    spr.setTextColor(TFT_RED, palette.bg);
    spr.setCursor(kCodeBuddyWidth - 48, kCodeBuddyHeight - 12);
    spr.print("B: deny");
  }
}

void drawHud(const Palette& palette) {
  const int area = 42;
  constexpr uint8_t kVisibleRows = 3;
  constexpr uint8_t kRowWidth = 20;
  spr.fillRect(0, kCodeBuddyHeight - area, kCodeBuddyWidth, area, palette.bg);
  spr.drawFastHLine(0, kCodeBuddyHeight - area, kCodeBuddyWidth,
                    palette.textDim);

  if (state.entryGeneration != lastEntryGeneration) {
    transcriptScroll = 0;
    lastEntryGeneration = state.entryGeneration;
  }

  char rows[32][48] = {};
  uint8_t rowCount = 0;
  if (state.entryCount > 0) {
    for (uint8_t i = 0; i < state.entryCount && rowCount < 32; ++i) {
      rowCount += wrapTextRows(state.entries[i], &rows[rowCount],
                               32 - rowCount, kRowWidth);
    }
  }
  if (rowCount == 0) {
    rowCount = wrapTextRows(state.message, rows, 32, kRowWidth);
  }

  const uint8_t maxScroll =
      rowCount > kVisibleRows ? rowCount - kVisibleRows : 0;
  if (transcriptScroll > maxScroll) {
    transcriptScroll = maxScroll;
  }

  if (transferActive) {
    spr.setTextSize(1);
    spr.setTextColor(palette.text, palette.bg);
    spr.setCursor(4, kCodeBuddyHeight - area + 20);
    spr.printf("installing %luK/%luK",
               static_cast<unsigned long>(transferTotalWritten / 1024),
               static_cast<unsigned long>(transferTotal / 1024));
  } else {
    const int end = static_cast<int>(rowCount) - transcriptScroll;
    int start = end - kVisibleRows;
    if (start < 0) {
      start = 0;
    }
    spr.setTextSize(1);
    for (int row = start; row < end; ++row) {
      const bool newest = row == static_cast<int>(rowCount) - 1 &&
                          transcriptScroll == 0;
      spr.setTextColor(newest ? palette.text : palette.textDim, palette.bg);
      spr.setCursor(4, kCodeBuddyHeight - area + 4 + (row - start) * 12);
      spr.print(rows[row]);
    }
    if (transcriptScroll > 0) {
      spr.setTextColor(palette.body, palette.bg);
      spr.setCursor(kCodeBuddyWidth - 18, kCodeBuddyHeight - 14);
      spr.printf("-%u", transcriptScroll);
    }
  }
}

void tinyHeart(int x, int y, bool filled, uint16_t color) {
  if (filled) {
    spr.fillCircle(x - 2, y, 2, color);
    spr.fillCircle(x + 2, y, 2, color);
    spr.fillTriangle(x - 4, y + 1, x + 4, y + 1, x, y + 5, color);
  } else {
    spr.drawCircle(x - 2, y, 2, color);
    spr.drawCircle(x + 2, y, 2, color);
    spr.drawLine(x - 4, y + 1, x, y + 5, color);
    spr.drawLine(x + 4, y + 1, x, y + 5, color);
  }
}

void drawPetStats(const Palette& palette) {
  int y = 42;
  spr.setTextColor(palette.textDim, palette.bg);
  spr.setCursor(6, y - 2);
  spr.print("mood");
  const uint8_t mood = statsMoodTier();
  const uint16_t moodColor =
      mood >= 3 ? TFT_RED : mood >= 2 ? kHotColor : palette.textDim;
  for (int i = 0; i < 4; ++i) {
    tinyHeart(54 + i * 16, y + 2, i < mood, moodColor);
  }

  y += 20;
  spr.setCursor(6, y - 2);
  spr.print("fed");
  const uint8_t fed = statsFedProgress();
  for (int i = 0; i < 10; ++i) {
    const int x = 38 + i * 9;
    if (i < fed) {
      spr.fillCircle(x, y + 1, 2, palette.body);
    } else {
      spr.drawCircle(x, y + 1, 2, palette.textDim);
    }
  }

  y += 20;
  spr.setCursor(6, y - 2);
  spr.print("energy");
  const uint8_t energy = statsEnergyTier();
  const uint16_t energyColor =
      energy >= 4 ? TFT_CYAN : energy >= 2 ? TFT_YELLOW : kHotColor;
  for (int i = 0; i < 5; ++i) {
    const int x = 54 + i * 13;
    if (i < energy) {
      spr.fillRect(x, y - 2, 9, 6, energyColor);
    } else {
      spr.drawRect(x, y - 2, 9, 6, palette.textDim);
    }
  }

  y += 24;
  spr.fillRoundRect(6, y - 2, 42, 14, 3, palette.body);
  spr.setTextColor(palette.bg, palette.body);
  spr.setCursor(11, y + 1);
  spr.printf("Lv %u", stats().level);

  y += 20;
  spr.setTextColor(palette.textDim, palette.bg);
  spr.setCursor(6, y);
  spr.printf("approved %u", stats().approvals);
  spr.setCursor(6, y + 10);
  spr.printf("denied   %u", stats().denials);
  const uint32_t nap = stats().napSeconds;
  spr.setCursor(6, y + 20);
  spr.printf("napped   %luh%02lum", nap / 3600, (nap / 60) % 60);

  auto printTokens = [&](const char* label, uint32_t value, int yPx) {
    spr.setCursor(6, yPx);
    if (value >= 1000000) {
      spr.printf("%s%lu.%luM", label,
                 static_cast<unsigned long>(value / 1000000),
                 static_cast<unsigned long>((value / 100000) % 10));
    } else if (value >= 1000) {
      spr.printf("%s%lu.%luK", label,
                 static_cast<unsigned long>(value / 1000),
                 static_cast<unsigned long>((value / 100) % 10));
    } else {
      spr.printf("%s%lu", label, static_cast<unsigned long>(value));
    }
  };
  printTokens("tokens   ", stats().tokens, y + 30);
  printTokens("today    ", state.tokensToday, y + 40);
}

void drawPetHowTo(const Palette& palette) {
  int y = 42;
  auto line = [&](uint16_t color, const char* text) {
    spr.setTextColor(color, palette.bg);
    spr.setCursor(6, y);
    spr.print(text);
    y += 9;
  };
  auto gap = [&]() { y += 4; };

  line(palette.body, "MOOD");
  line(palette.textDim, " approve fast = up");
  line(palette.textDim, " deny lots = down");
  gap();
  line(palette.body, "FED");
  line(palette.textDim, " 50K tokens =");
  line(palette.textDim, " level up + confetti");
  gap();
  line(palette.body, "ENERGY");
  line(palette.textDim, " face-down to nap");
  line(palette.textDim, " refills to full");
  gap();
  line(palette.textDim, "idle 30s = off");
  line(palette.textDim, "any button = wake");
  gap();
  line(palette.textDim, "A: screens  B: page");
  line(palette.textDim, "hold A: menu");
}

void drawPetView(const Palette& palette) {
  spr.fillSprite(palette.bg);
  spr.setTextSize(1);
  spr.setTextColor(palette.text, palette.bg);
  spr.setCursor(4, 8);
  if (ownerName()[0] != '\0') {
    spr.printf("%s's %s", ownerName(), petName());
  } else {
    spr.printf("%s", petName());
  }
  spr.setTextColor(palette.textDim, palette.bg);
  spr.setCursor(108, 8);
  spr.printf("%u/%u", petPage + 1, kPetPageCount);

  if (petPage == 0) {
    drawPetStats(palette);
  } else {
    drawPetHowTo(palette);
  }
}

void drawInfoView(const Palette& palette) {
  spr.fillSprite(palette.bg);
  spr.setTextSize(1);
  spr.setTextColor(palette.text, palette.bg);
  spr.setCursor(4, 8);
  spr.print("Info");
  spr.setTextColor(palette.textDim, palette.bg);
  spr.setCursor(108, 8);
  spr.printf("%u/%u", infoPage + 1, kInfoPageCount);

  int y = 26;
  auto heading = [&](const char* text) {
    spr.setTextColor(palette.body, palette.bg);
    spr.setCursor(6, y);
    spr.print(text);
    y += 16;
  };
  auto line = [&](uint16_t color, const char* text) {
    char clipped[80];
    clipText(clipped, sizeof(clipped), text, 21);
    spr.setTextColor(color, palette.bg);
    spr.setCursor(6, y);
    spr.print(clipped);
    y += 10;
  };
  auto printfLine = [&](uint16_t color, const char* fmt, ...) {
    char buffer[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    line(color, buffer);
  };

  if (infoPage == 0) {
    heading("ABOUT");
    line(palette.textDim, "I watch your Codex");
    line(palette.textDim, "desktop sessions.");
    y += 4;
    line(palette.textDim, "I sleep when");
    line(palette.textDim, "nothing's happening,");
    line(palette.textDim, "wake when you work,");
    line(palette.textDim, "and ask for approvals.");
    y += 4;
    line(palette.text, "Press A on a prompt");
    line(palette.text, "to approve from here.");
  } else if (infoPage == 1) {
    heading("BUTTONS");
    line(palette.text, "A   front");
    line(palette.textDim, "    next screen");
    line(palette.textDim, "    approve prompt");
    y += 4;
    line(palette.text, "B   right side");
    line(palette.textDim, "    next page");
    line(palette.textDim, "    deny prompt");
    y += 4;
    line(palette.text, "hold A");
    line(palette.textDim, "    menu");
    line(palette.text, "Power");
    line(palette.textDim, "    screen off");
  } else if (infoPage == 2) {
    heading("CODEX");
    printfLine(palette.textDim, "sessions  %u", state.sessionsTotal);
    printfLine(palette.textDim, "running   %u", state.sessionsRunning);
    printfLine(palette.textDim, "waiting   %u", state.sessionsWaiting);
    y += 6;
    heading("LINK");
    printfLine(palette.textDim, "ble       %s",
               sharedBleConnected() ? "linked" : "discover");
    const uint32_t age = state.lastUpdatedMs == 0
                             ? 0
                             : (millis() - state.lastUpdatedMs) / 1000;
    printfLine(palette.textDim, "last msg  %lus",
               static_cast<unsigned long>(age));
    static const char* const kStateNames[] = {
        "sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart"};
    printfLine(palette.textDim, "state     %s", kStateNames[activeState]);
  } else if (infoPage == 3) {
    heading("DEVICE");
    const int batteryMv = M5.Power.getBatteryVoltage();
    const int batteryMa = M5.Power.getBatteryCurrent();
    const int vbusMv = M5.Power.getVBUSVoltage();
    int batteryPct = (batteryMv - 3200) / 10;
    if (batteryPct < 0) {
      batteryPct = 0;
    } else if (batteryPct > 100) {
      batteryPct = 100;
    }
    printfLine(palette.text, "%d%% battery", batteryPct);
    printfLine(palette.textDim, "battery  %d.%02dV", batteryMv / 1000,
               (batteryMv % 1000) / 10);
    printfLine(palette.textDim, "current  %+dmA", batteryMa);
    if (vbusMv > 4000) {
      printfLine(palette.textDim, "usb in   %d.%02dV", vbusMv / 1000,
                 (vbusMv % 1000) / 10);
    }
    y += 6;
    heading("SYSTEM");
    if (ownerName()[0] != '\0') {
      printfLine(palette.textDim, "owner    %s", ownerName());
    }
    const uint32_t uptime = millis() / 1000;
    printfLine(palette.textDim, "uptime   %luh %02lum",
               static_cast<unsigned long>(uptime / 3600),
               static_cast<unsigned long>((uptime / 60) % 60));
    printfLine(palette.textDim, "heap     %uKB", ESP.getFreeHeap() / 1024);
  } else if (infoPage == 4) {
    heading("BLUETOOTH");
    line(sharedBleConnected() ? TFT_GREEN : kHotColor,
         sharedBleConnected() ? "linked" : "discover");
    y += 4;
    line(palette.text, kCodeBuddyBleDeviceName);
    line(palette.textDim, "Nordic UART Service");
    if (sharedBleConnected()) {
      const uint32_t age = state.lastUpdatedMs == 0
                               ? 0
                               : (millis() - state.lastUpdatedMs) / 1000;
      printfLine(palette.textDim, "last msg  %lus",
                 static_cast<unsigned long>(age));
    } else {
      y += 4;
      line(palette.text, "TO PAIR");
      line(palette.textDim, "code-buddy repair");
      y += 4;
      line(palette.text, "TO STAY LINKED");
      line(palette.textDim, "run codex");
    }
  } else {
    heading("CREDITS");
    const AboutInfo about = currentAboutInfo();
    line(palette.textDim, "made by");
    line(palette.text, about.made_by);
    y += 6;
    line(palette.textDim, "source");
    line(palette.text, about.source_line_1);
    line(palette.text, about.source_line_2);
    y += 6;
    line(palette.textDim, "hardware");
    line(palette.text, about.hardware_line_1);
    line(palette.text, about.hardware_line_2);
  }
}

PersonaState deriveActiveState() {
  const bool connected =
      state.lastUpdatedMs != 0 && millis() - state.lastUpdatedMs < 30000;
  PersonaInputs input = {};
  input.connected = connected || sharedBleConnected();
  input.sessionsRunning = state.sessionsRunning;
  input.sessionsWaiting = state.sessionsWaiting;
  return derivePersonaState(input);
}

void triggerOneShot(PersonaState persona, uint32_t durationMs) {
  activeState = persona;
  oneShotUntil = millis() + durationMs;
}

bool checkShake() {
  if (!M5.Imu.isEnabled()) {
    return false;
  }

  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  if (!M5.Imu.getAccelData(&ax, &ay, &az)) {
    return false;
  }

  const float magnitude = sqrtf(ax * ax + ay * ay + az * az);
  const float delta = fabsf(magnitude - accelBaseline);
  accelBaseline = accelBaseline * 0.95f + magnitude * 0.05f;
  return delta > kShakeDeltaThreshold;
}

bool isFaceDown() {
  if (!M5.Imu.isEnabled()) {
    return false;
  }

  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  if (!M5.Imu.getAccelData(&ax, &ay, &az)) {
    return false;
  }
  return az < -0.7f && fabsf(ax) < 0.4f && fabsf(ay) < 0.4f;
}

void pollShake() {
  const uint32_t now = millis();
  if (now - lastShakeCheckMs < kShakeCheckMs) {
    return;
  }
  lastShakeCheckMs = now;

  if (screenOff || promptPending() || (int32_t)(now - oneShotUntil) < 0) {
    return;
  }

  if (checkShake()) {
    triggerOneShot(P_DIZZY, 2000);
    screenDirty = true;
    lastInteractMs = now;
    Serial.println("[codebuddy] shake: dizzy");
  }
}

void pollNap() {
  if (promptPending() || screenOff) {
    return;
  }

  if (isFaceDown()) {
    if (faceDownFrames < 20) {
      ++faceDownFrames;
    }
  } else if (faceDownFrames > -10) {
    --faceDownFrames;
  }

  const uint32_t now = millis();
  if (!napping && faceDownFrames >= 15) {
    napping = true;
    napStartMs = now;
    compatSetBrightnessPercent(8);
    triggerOneShot(P_SLEEP, 60000);
    screenDirty = true;
    Serial.println("[codebuddy] face-down nap");
  } else if (napping && faceDownFrames <= -8) {
    napping = false;
    statsOnNapEnd((now - napStartMs) / 1000);
    statsOnWake();
    compatSetBrightnessPercent(20 + brightnessLevel * 20);
    oneShotUntil = 0;
    screenDirty = true;
    lastInteractMs = now;
    Serial.println("[codebuddy] wake from nap");
  }
}

constexpr const char* kMenuItems[] = {
    "settings", "turn off", "help", "about", "launcher", "close"};
constexpr uint8_t kMenuCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);

constexpr const char* kSettingsItems[] = {
    "brightness", "sound", "bluetooth", "wifi", "led",
    "transcript", "clock rot", "ascii pet", "reset", "back"};
constexpr uint8_t kSettingsCount =
    sizeof(kSettingsItems) / sizeof(kSettingsItems[0]);

constexpr const char* kResetItems[] = {
    "delete char", "factory reset", "back"};
constexpr uint8_t kResetCount = sizeof(kResetItems) / sizeof(kResetItems[0]);

void closeOverlays() {
  overlay = CodeBuddyOverlay::None;
  resetConfirmIdx = 0xFF;
  screenDirty = true;
}

void drawMenuHints(const Palette& palette, int x, int width, int y,
                   const char* downLabel = "A",
                   const char* rightLabel = "B") {
  spr.drawFastHLine(x + 6, y - 4, width - 12, palette.textDim);
  spr.setTextColor(palette.textDim, kPanelColor);
  int cursor = x + 8;
  spr.setCursor(cursor, y);
  spr.print(downLabel);
  cursor += strlen(downLabel) * 6 + 4;
  spr.fillTriangle(cursor, y + 1, cursor + 6, y + 1, cursor + 3, y + 6,
                   palette.textDim);
  cursor = x + width / 2 + 4;
  spr.setCursor(cursor, y);
  spr.print(rightLabel);
  cursor += strlen(rightLabel) * 6 + 4;
  spr.fillTriangle(cursor, y, cursor, y + 6, cursor + 5, y + 3,
                   palette.textDim);
}

void drawMenuOverlay(const Palette& palette) {
  const int width = 118;
  const int height = 16 + kMenuCount * 14 + 14;
  const int x = (kCodeBuddyWidth - width) / 2;
  const int y = (kCodeBuddyHeight - height) / 2;
  spr.fillRoundRect(x, y, width, height, 4, kPanelColor);
  spr.drawRoundRect(x, y, width, height, 4, palette.textDim);
  spr.setTextSize(1);
  for (uint8_t i = 0; i < kMenuCount; ++i) {
    const bool selected = i == menuSel;
    spr.setTextColor(selected ? palette.text : palette.textDim, kPanelColor);
    spr.setCursor(x + 6, y + 8 + i * 14);
    spr.print(selected ? "> " : "  ");
    spr.print(kMenuItems[i]);
  }
  drawMenuHints(palette, x, width, y + height - 12);
}

void drawSettingsOverlay(const Palette& palette) {
  const int width = 122;
  const int height = 16 + kSettingsCount * 14 + 14;
  const int x = (kCodeBuddyWidth - width) / 2;
  const int y = (kCodeBuddyHeight - height) / 2;
  spr.fillRoundRect(x, y, width, height, 4, kPanelColor);
  spr.drawRoundRect(x, y, width, height, 4, palette.textDim);
  spr.setTextSize(1);
  Settings& s = settings();
  for (uint8_t i = 0; i < kSettingsCount; ++i) {
    const bool selected = i == settingsSel;
    spr.setTextColor(selected ? palette.text : palette.textDim, kPanelColor);
    spr.setCursor(x + 6, y + 8 + i * 14);
    spr.print(selected ? "> " : "  ");
    spr.print(kSettingsItems[i]);
    spr.setCursor(x + width - 38, y + 8 + i * 14);
    spr.setTextColor(palette.textDim, kPanelColor);
    if (i == 0) {
      spr.printf("%u/4", brightnessLevel);
    } else if (i == 1) {
      spr.setTextColor(s.sound ? TFT_GREEN : palette.textDim, kPanelColor);
      spr.print(s.sound ? " on" : "off");
    } else if (i == 2) {
      spr.setTextColor(s.bt ? TFT_GREEN : palette.textDim, kPanelColor);
      spr.print(s.bt ? " on" : "off");
    } else if (i == 3) {
      spr.setTextColor(s.wifi ? TFT_GREEN : palette.textDim, kPanelColor);
      spr.print(s.wifi ? " on" : "off");
    } else if (i == 4) {
      spr.setTextColor(s.led ? TFT_GREEN : palette.textDim, kPanelColor);
      spr.print(s.led ? " on" : "off");
    } else if (i == 5) {
      spr.setTextColor(s.hud ? TFT_GREEN : palette.textDim, kPanelColor);
      spr.print(s.hud ? " on" : "off");
    } else if (i == 6) {
      static const char* const kRotationLabels[] = {"auto", "port", "land"};
      spr.print(kRotationLabels[s.clockRot]);
    } else if (i == 7) {
      const uint8_t total = buddySpeciesCount() + (gifAvailable ? 1 : 0);
      const uint8_t pos = buddyMode ? buddySpeciesIdx() + 1 : total;
      spr.printf("%u/%u", pos, total);
    }
  }
  drawMenuHints(palette, x, width, y + height - 12, "Next", "Change");
}

void drawResetOverlay(const Palette& palette) {
  const int width = 118;
  const int height = 16 + kResetCount * 14 + 14;
  const int x = (kCodeBuddyWidth - width) / 2;
  const int y = (kCodeBuddyHeight - height) / 2;
  spr.fillRoundRect(x, y, width, height, 4, kPanelColor);
  spr.drawRoundRect(x, y, width, height, 4, kHotColor);
  spr.setTextSize(1);
  for (uint8_t i = 0; i < kResetCount; ++i) {
    const bool selected = i == resetSel;
    const bool armed = i == resetConfirmIdx &&
                       (int32_t)(millis() - resetConfirmUntil) < 0;
    spr.setTextColor(armed ? kHotColor
                           : selected ? palette.text : palette.textDim,
                     kPanelColor);
    spr.setCursor(x + 6, y + 8 + i * 14);
    spr.print(selected ? "> " : "  ");
    spr.print(armed ? "really?" : kResetItems[i]);
  }
  drawMenuHints(palette, x, width, y + height - 12);
}

void drawOverlays(const Palette& palette) {
  if (overlay == CodeBuddyOverlay::Menu) {
    drawMenuOverlay(palette);
  } else if (overlay == CodeBuddyOverlay::Settings) {
    drawSettingsOverlay(palette);
  } else if (overlay == CodeBuddyOverlay::Reset) {
    drawResetOverlay(palette);
  }
}

void applySetting(uint8_t index) {
  Settings& s = settings();
  switch (index) {
    case 0:
      brightnessLevel = (brightnessLevel + 1) % 5;
      compatSetBrightnessPercent(20 + brightnessLevel * 20);
      return;
    case 1:
      s.sound = !s.sound;
      break;
    case 2:
      s.bt = !s.bt;
      break;
    case 3:
      s.wifi = !s.wifi;
      break;
    case 4:
      s.led = !s.led;
      break;
    case 5:
      s.hud = !s.hud;
      break;
    case 6:
      s.clockRot = (s.clockRot + 1) % 3;
      break;
    case 7:
      nextPet();
      return;
    case 8:
      overlay = CodeBuddyOverlay::Reset;
      resetSel = 0;
      resetConfirmIdx = 0xFF;
      screenDirty = true;
      return;
    case 9:
      closeOverlays();
      return;
  }
  settingsSave();
  screenDirty = true;
}

void applyReset(uint8_t index) {
  const uint32_t now = millis();
  const bool armed = resetConfirmIdx == index &&
                     (int32_t)(now - resetConfirmUntil) < 0;
  if (index == 2) {
    overlay = CodeBuddyOverlay::Settings;
    resetConfirmIdx = 0xFF;
    screenDirty = true;
    return;
  }
  if (!armed) {
    resetConfirmIdx = index;
    resetConfirmUntil = now + 3000;
    screenDirty = true;
    return;
  }
  if (index == 0) {
    deleteCharacters();
    overlay = CodeBuddyOverlay::Settings;
    resetConfirmIdx = 0xFF;
  } else {
    factoryReset();
  }
  screenDirty = true;
}

void confirmMenu() {
  switch (menuSel) {
    case 0:
      overlay = CodeBuddyOverlay::Settings;
      settingsSel = 0;
      break;
    case 1:
      compatPowerOff();
      break;
    case 2:
      currentView = CodeBuddyView::Info;
      infoPage = 1;
      closeOverlays();
      break;
    case 3:
      currentView = CodeBuddyView::Info;
      infoPage = 0;
      closeOverlays();
      break;
    case 4:
      exitToLauncher = true;
      break;
    case 5:
      closeOverlays();
      break;
  }
  screenDirty = true;
}

void renderRuntime() {
  if (!appVisible || !runtimeReady || screenOff) {
    return;
  }

  baseState = deriveActiveState();
  if ((int32_t)(millis() - oneShotUntil) >= 0) {
    activeState = baseState;
  }

  const Palette& palette = characterPalette();
  if (currentView == CodeBuddyView::Pet) {
    drawPetView(palette);
    drawOverlays(palette);
    spr.pushSprite(0, 0);
    screenDirty = false;
    return;
  }
  if (currentView == CodeBuddyView::Info) {
    drawInfoView(palette);
    drawOverlays(palette);
    spr.pushSprite(0, 0);
    screenDirty = false;
    return;
  }

  if (buddyMode) {
    buddyTick(activeState);
  } else if (characterLoaded()) {
    characterSetState(activeState);
    characterTick();
  } else if (state.promptId[0] != '\0' && !responseSent) {
    spr.fillSprite(palette.bg);
  } else {
    spr.fillSprite(palette.bg);
    spr.setTextSize(1);
    spr.setTextColor(palette.textDim, palette.bg);
    spr.setCursor(8, 100);
    spr.print("no character loaded");
  }

  if (state.promptId[0] != '\0' && !responseSent) {
    drawApproval(palette);
  } else {
    drawHud(palette);
  }
  drawOverlays(palette);
  spr.pushSprite(0, 0);
  screenDirty = false;
}

void respondToPrompt(const char* decision) {
  if (state.promptId[0] == '\0' || responseSent) {
    return;
  }

  notifyHost(codeBuddyEncodePermission(state.promptId, decision));
  if (strcmp(decision, "deny") == 0) {
    statsOnDenial();
    triggerOneShot(P_DIZZY, 1500);
  } else {
    statsOnApproval((millis() - promptArrivedMs) / 1000);
    triggerOneShot(P_HEART, 1800);
  }
  responseSent = true;
  screenDirty = true;
}

}  // namespace

void codeBuddyAppBegin() {
  if (bleStarted) {
    return;
  }

  Serial.println("[codebuddy] begin service setup");
  sharedBleBegin(kSharedBleDeviceName);
  sharedBleRegisterConnectionCallback(handleSharedBleConnection);
  loadPreferences();

  BLEService* service = sharedBleServer()->createService(kCodeBuddyServiceUuid);
  txCharacteristic = service->createCharacteristic(
      kCodeBuddyTxUuid, BLECharacteristic::PROPERTY_NOTIFY);
  txCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic* rxCharacteristic = service->createCharacteristic(
      kCodeBuddyRxUuid,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxCharacteristic->setCallbacks(new CodeBuddyRxCallbacks());

  service->start();
  bleStarted = true;
  Serial.println("[codebuddy] NUS service ready");
}

void codeBuddyAppStart() {
  Serial.println("[codebuddy] start app");
  appVisible = true;
  screenOff = false;
  exitToLauncher = false;
  napping = false;
  faceDownFrames = 0;
  overlay = CodeBuddyOverlay::None;
  sharedBleHandoffToAdvertisement(kCodeBuddyBleDeviceName,
                                  kCodeBuddyServiceUuid);
  M5.Display.wakeup();
  M5.Display.setRotation(0);
  ensureRuntimeReady();
  screenDirty = true;
  lastInteractMs = millis();
  renderRuntime();
}

void codeBuddyAppUpdate() {
  processPendingRx();
  pollShake();
  pollNap();
  renderRuntime();
}

void codeBuddyAppButtonA() {
  if (screenOff) {
    wakeScreen();
    renderRuntime();
    return;
  }

  if (overlay == CodeBuddyOverlay::Menu) {
    menuSel = (menuSel + 1) % kMenuCount;
    screenDirty = true;
    return;
  }
  if (overlay == CodeBuddyOverlay::Settings) {
    settingsSel = (settingsSel + 1) % kSettingsCount;
    screenDirty = true;
    return;
  }
  if (overlay == CodeBuddyOverlay::Reset) {
    resetSel = (resetSel + 1) % kResetCount;
    resetConfirmIdx = 0xFF;
    screenDirty = true;
    return;
  }

  if (state.promptId[0] != '\0' && !responseSent) {
    respondToPrompt("once");
    return;
  }

  currentView = currentView == CodeBuddyView::Home
                    ? CodeBuddyView::Pet
                    : currentView == CodeBuddyView::Pet ? CodeBuddyView::Info
                                                        : CodeBuddyView::Home;
  buddySetPeek(currentView != CodeBuddyView::Home);
  characterSetPeek(currentView != CodeBuddyView::Home);
  triggerOneShot(P_HEART, 1200);
  lastInteractMs = millis();
  screenDirty = true;
}

void codeBuddyAppButtonB() {
  if (screenOff) {
    wakeScreen();
    renderRuntime();
    return;
  }

  if (overlay == CodeBuddyOverlay::Menu) {
    confirmMenu();
    return;
  }
  if (overlay == CodeBuddyOverlay::Settings) {
    applySetting(settingsSel);
    return;
  }
  if (overlay == CodeBuddyOverlay::Reset) {
    applyReset(resetSel);
    return;
  }

  if (state.promptId[0] != '\0' && !responseSent) {
    respondToPrompt("deny");
    return;
  }

  if (currentView == CodeBuddyView::Home) {
    char rows[32][48] = {};
    uint8_t rowCount = 0;
    if (state.entryCount > 0) {
      for (uint8_t i = 0; i < state.entryCount && rowCount < 32; ++i) {
        rowCount += wrapTextRows(state.entries[i], &rows[rowCount],
                                 32 - rowCount, 20);
      }
    }
    if (rowCount == 0) {
      rowCount = wrapTextRows(state.message, rows, 32, 20);
    }
    const uint8_t maxScroll = rowCount > 3 ? rowCount - 3 : 0;
    transcriptScroll = maxScroll == 0 ? 0 : (transcriptScroll + 1) % (maxScroll + 1);
  } else if (currentView == CodeBuddyView::Pet) {
    petPage = (petPage + 1) % kPetPageCount;
  } else if (currentView == CodeBuddyView::Info) {
    infoPage = (infoPage + 1) % kInfoPageCount;
  } else {
    triggerOneShot(P_HEART, 1200);
  }
  lastInteractMs = millis();
  screenDirty = true;
}

void codeBuddyAppButtonALong() {
  if (screenOff) {
    codeBuddyAppWake();
    return;
  }
  if (overlay == CodeBuddyOverlay::Reset) {
    overlay = CodeBuddyOverlay::Settings;
    resetConfirmIdx = 0xFF;
  } else if (overlay == CodeBuddyOverlay::Settings) {
    closeOverlays();
  } else if (overlay == CodeBuddyOverlay::Menu) {
    closeOverlays();
  } else {
    overlay = CodeBuddyOverlay::Menu;
    menuSel = 0;
  }
  lastInteractMs = millis();
  screenDirty = true;
}

bool codeBuddyAppScreenOff() {
  return screenOff;
}

void codeBuddyAppWake() {
  wakeScreen();
  renderRuntime();
}

void codeBuddyAppToggleScreen() {
  if (screenOff) {
    codeBuddyAppWake();
    return;
  }
  screenOff = true;
  M5.Display.sleep();
  lastInteractMs = millis();
}

bool codeBuddyAppWantsLauncher() {
  return exitToLauncher;
}

void codeBuddyAppStop() {
  Serial.println("[codebuddy] stop app");
  appVisible = false;
  exitToLauncher = false;
  if (napping) {
    statsOnNapEnd((millis() - napStartMs) / 1000);
    statsOnWake();
    napping = false;
  }
  compatSetBrightnessPercent(20 + brightnessLevel * 20);
  overlay = CodeBuddyOverlay::None;
  if (screenOff) {
    M5.Display.wakeup();
    screenOff = false;
  }
  releaseRuntime();
  sharedBleHandoffToAdvertisement(kSharedBleDeviceName, kStickLinkServiceUuid);
  M5.Display.setRotation(1);
}
