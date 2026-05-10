#include <Arduino.h>
#include <ArduinoJson.h>
#include <unity.h>

#include "stick_link_protocol.h"

void test_button_event_contains_v1_envelope_fields() {
  const String payload = stickLinkEncodeButtonEvent(
      "sensor", "ButtonA", "ButtonA pressed from Sensor App", 123456, 7);

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, payload);
  TEST_ASSERT_FALSE_MESSAGE(error, error.c_str());
  TEST_ASSERT_EQUAL_INT(1, doc["v"].as<int>());
  TEST_ASSERT_EQUAL_STRING("000007", doc["id"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("sensor", doc["app"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("button", doc["type"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("ButtonA", doc["name"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("ButtonA pressed from Sensor App",
                           doc["text"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(123456, doc["ts_ms"].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(7, doc["seq"].as<uint32_t>());
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_button_event_contains_v1_envelope_fields);
  UNITY_END();
}

void loop() {
}
