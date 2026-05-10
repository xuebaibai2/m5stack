#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

constexpr const char* kStickLinkBleName = "StickS3 Link";
constexpr const char* kStickLinkServiceUuid =
    "6f7d9f10-2c3b-4e7a-9a1f-1b2c3d4e5f60";
constexpr const char* kStickLinkMessageCharacteristicUuid =
    "6f7d9f11-2c3b-4e7a-9a1f-1b2c3d4e5f60";
constexpr const char* kStickLinkDeviceInfoCharacteristicUuid =
    "6f7d9f12-2c3b-4e7a-9a1f-1b2c3d4e5f60";
constexpr const char* kStickLinkAudioCharacteristicUuid =
    "6f7d9f13-2c3b-4e7a-9a1f-1b2c3d4e5f60";

constexpr uint32_t kStickLinkAudioSampleRate = 16000;
constexpr size_t kStickLinkAudioSamplesPerChunk = 320;
constexpr size_t kStickLinkAudioBytesPerChunk =
    kStickLinkAudioSamplesPerChunk / 2;

inline String stickLinkEncodeEvent(const char* app, const char* type,
                                   const char* name, const char* text,
                                   uint32_t tsMs, uint32_t seq) {
  JsonDocument doc;
  doc["v"] = 1;

  char id[12];
  snprintf(id, sizeof(id), "%06lu", static_cast<unsigned long>(seq));
  doc["id"] = id;
  doc["app"] = app;
  doc["type"] = type;
  doc["name"] = name;
  doc["text"] = text;
  doc["ts_ms"] = tsMs;
  doc["seq"] = seq;

  String output;
  serializeJson(doc, output);
  return output;
}

inline String stickLinkEncodeButtonEvent(const char* app, const char* buttonName,
                                         const char* text, uint32_t tsMs,
                                         uint32_t seq) {
  return stickLinkEncodeEvent(app, "button", buttonName, text, tsMs, seq);
}

inline String stickLinkEncodeVoiceEvent(const char* name, const char* text,
                                        uint32_t tsMs, uint32_t seq) {
  return stickLinkEncodeEvent("remote_mic", "voice", name, text, tsMs, seq);
}
