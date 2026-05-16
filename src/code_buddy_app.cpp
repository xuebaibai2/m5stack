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
enum class CodeBuddyView : uint8_t { Home, Pet, Info };
CodeBuddyView currentView = CodeBuddyView::Home;
uint8_t petPage = 0;
uint8_t infoPage = 0;
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
float accelBaseline = 1.0f;

bool ensureFilesystem();
void wakeScreen();
bool promptPending();

bool ensureRuntimeReady() {
  if (runtimeReady) {
    return true;
  }

  ensureFilesystem();
  statsLoad();
  settingsLoad();
  petNameLoad();
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
  return true;
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

void drawPetView(const Palette& palette) {
  spr.fillSprite(palette.bg);
  spr.setTextSize(1);
  spr.setTextColor(palette.text, palette.bg);
  spr.setCursor(4, 8);
  spr.printf("%s", petName());
  spr.setTextColor(palette.textDim, palette.bg);
  spr.setCursor(108, 8);
  spr.printf("%u/2", petPage + 1);

  if (petPage == 0) {
    spr.setTextColor(palette.body, palette.bg);
    spr.setCursor(6, 36);
    spr.printf("Level %u", stats().level);
    spr.setTextColor(palette.textDim, palette.bg);
    spr.setCursor(6, 58);
    spr.printf("approved %u", stats().approvals);
    spr.setCursor(6, 72);
    spr.printf("denied   %u", stats().denials);
    spr.setCursor(6, 86);
    spr.printf("tokens   %lu", static_cast<unsigned long>(stats().tokens));
    spr.setCursor(6, 100);
    spr.printf("today    %lu", static_cast<unsigned long>(state.tokensToday));
  } else {
    spr.setTextColor(palette.body, palette.bg);
    spr.setCursor(6, 36);
    spr.print("A screens");
    spr.setTextColor(palette.textDim, palette.bg);
    spr.setCursor(6, 52);
    spr.print("B next page");
    spr.setCursor(6, 68);
    spr.print("hold A menu");
    spr.setCursor(6, 92);
    spr.print("Prompt:");
    spr.setCursor(6, 108);
    spr.print("A approve");
    spr.setCursor(6, 124);
    spr.print("B deny");
  }
}

void drawInfoView(const Palette& palette) {
  spr.fillSprite(palette.bg);
  spr.setTextSize(1);
  spr.setTextColor(palette.text, palette.bg);
  spr.setCursor(4, 8);
  spr.print("CodeBuddy");
  spr.setTextColor(palette.textDim, palette.bg);
  spr.setCursor(108, 8);
  spr.printf("%u/3", infoPage + 1);

  spr.setCursor(6, 36);
  if (infoPage == 0) {
    spr.setTextColor(palette.body, palette.bg);
    spr.print("Bluetooth");
    spr.setTextColor(palette.textDim, palette.bg);
    spr.setCursor(6, 56);
    spr.printf("%s", kSharedBleDeviceName);
    spr.setCursor(6, 72);
    spr.printf("CodeBuddy %s", sharedBleConnected() ? "linked" : "discover");
    spr.setCursor(6, 88);
    spr.print("NUS advertised");
  } else if (infoPage == 1) {
    spr.setTextColor(palette.body, palette.bg);
    spr.print("Codex");
    spr.setTextColor(palette.textDim, palette.bg);
    spr.setCursor(6, 56);
    spr.printf("sessions %u", state.sessionsTotal);
    spr.setCursor(6, 72);
    spr.printf("running  %u", state.sessionsRunning);
    spr.setCursor(6, 88);
    spr.printf("waiting  %u", state.sessionsWaiting);
  } else {
    spr.setTextColor(palette.body, palette.bg);
    spr.print("Character");
    spr.setTextColor(palette.textDim, palette.bg);
    spr.setCursor(6, 56);
    spr.printf("%s", buddyMode ? buddySpeciesName() : "GIF loaded");
    spr.setCursor(6, 72);
    spr.print("B changes ASCII pet");
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
    spr.pushSprite(0, 0);
    screenDirty = false;
    return;
  }
  if (currentView == CodeBuddyView::Info) {
    drawInfoView(palette);
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
  renderRuntime();
}

void codeBuddyAppButtonA() {
  if (screenOff) {
    wakeScreen();
    renderRuntime();
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
    petPage = (petPage + 1) % 2;
  } else if (currentView == CodeBuddyView::Info) {
    infoPage = (infoPage + 1) % 3;
  } else {
    triggerOneShot(P_HEART, 1200);
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

void codeBuddyAppStop() {
  Serial.println("[codebuddy] stop app");
  appVisible = false;
  if (screenOff) {
    M5.Display.wakeup();
    screenOff = false;
  }
  sharedBleHandoffToAdvertisement(kSharedBleDeviceName, kStickLinkServiceUuid);
  M5.Display.setRotation(1);
}
