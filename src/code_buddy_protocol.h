#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct CodeBuddyState {
  uint8_t sessionsTotal = 0;
  uint8_t sessionsRunning = 0;
  uint8_t sessionsWaiting = 0;
  uint32_t tokensToday = 0;
  uint32_t lastUpdatedMs = 0;
  uint16_t approvals = 0;
  uint16_t denials = 0;
  char message[128] = "No Codex connected";
  char promptId[40] = "";
  char promptTool[96] = "";
  char promptHint[256] = "";
};

inline void codeBuddyCopy(char* dest, size_t destSize, const char* source) {
  if (destSize == 0) {
    return;
  }
  strlcpy(dest, source != nullptr ? source : "", destSize);
}

inline bool codeBuddyApplyJson(const char* json, CodeBuddyState& state,
                               uint32_t nowMs) {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, json);
  if (error) {
    return false;
  }

  state.sessionsTotal = doc["total"] | state.sessionsTotal;
  state.sessionsRunning = doc["running"] | state.sessionsRunning;
  state.sessionsWaiting = doc["waiting"] | state.sessionsWaiting;
  state.tokensToday = doc["tokens_today"] | state.tokensToday;
  state.lastUpdatedMs = nowMs;
  codeBuddyCopy(state.message, sizeof(state.message), doc["msg"] | state.message);

  JsonObject prompt = doc["prompt"];
  if (prompt.isNull()) {
    state.promptId[0] = '\0';
    state.promptTool[0] = '\0';
    state.promptHint[0] = '\0';
  } else {
    codeBuddyCopy(state.promptId, sizeof(state.promptId), prompt["id"] | "");
    codeBuddyCopy(state.promptTool, sizeof(state.promptTool),
                  prompt["tool"] | "");
    codeBuddyCopy(state.promptHint, sizeof(state.promptHint),
                  prompt["hint"] | "");
  }

  return true;
}

inline String codeBuddyEncodePermission(const char* promptId,
                                        const char* decision) {
  JsonDocument doc;
  doc["cmd"] = "permission";
  doc["id"] = promptId;
  doc["decision"] = decision;

  String output;
  serializeJson(doc, output);
  return output;
}

inline String codeBuddyEncodeAck(const char* command, bool ok, uint32_t n = 0,
                                 const char* error = nullptr) {
  JsonDocument doc;
  doc["ack"] = command;
  doc["ok"] = ok;
  doc["n"] = n;
  if (error != nullptr && error[0] != '\0') {
    doc["error"] = error;
  }

  String output;
  serializeJson(doc, output);
  return output;
}

inline bool codeBuddyIsSafeTransferPath(const char* path) {
  if (path == nullptr || path[0] == '\0' || path[0] == '/') {
    return false;
  }

  for (const char* p = path; *p != '\0'; ++p) {
    if (*p == '\\') {
      return false;
    }
    if (p[0] == '.' && p[1] == '.') {
      return false;
    }
  }

  return true;
}
