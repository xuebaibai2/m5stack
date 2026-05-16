#include <Arduino.h>
#include <unity.h>

#include "code_buddy_protocol.h"

void test_heartbeat_snapshot_updates_state() {
  CodeBuddyState state;

  const char* payload =
      "{\"total\":3,\"running\":1,\"waiting\":1,"
      "\"msg\":\"approve: Bash\",\"tokens_today\":31200,"
      "\"prompt\":{\"id\":\"req_abc123\",\"tool\":\"Bash\","
      "\"hint\":\"pio run\"}}";

  TEST_ASSERT_TRUE(codeBuddyApplyJson(payload, state, 1234));
  TEST_ASSERT_EQUAL_UINT8(3, state.sessionsTotal);
  TEST_ASSERT_EQUAL_UINT8(1, state.sessionsRunning);
  TEST_ASSERT_EQUAL_UINT8(1, state.sessionsWaiting);
  TEST_ASSERT_EQUAL_UINT32(31200, state.tokensToday);
  TEST_ASSERT_EQUAL_UINT32(1234, state.lastUpdatedMs);
  TEST_ASSERT_EQUAL_STRING("approve: Bash", state.message);
  TEST_ASSERT_EQUAL_STRING("req_abc123", state.promptId);
  TEST_ASSERT_EQUAL_STRING("Bash", state.promptTool);
  TEST_ASSERT_EQUAL_STRING("pio run", state.promptHint);
}

void test_missing_prompt_clears_existing_prompt() {
  CodeBuddyState state;
  strlcpy(state.promptId, "old", sizeof(state.promptId));
  strlcpy(state.promptTool, "OldTool", sizeof(state.promptTool));
  strlcpy(state.promptHint, "old hint", sizeof(state.promptHint));

  TEST_ASSERT_TRUE(codeBuddyApplyJson("{\"total\":0,\"running\":0}", state, 55));
  TEST_ASSERT_EQUAL_STRING("", state.promptId);
  TEST_ASSERT_EQUAL_STRING("", state.promptTool);
  TEST_ASSERT_EQUAL_STRING("", state.promptHint);
}

void test_entries_update_transcript_state() {
  CodeBuddyState state;

  TEST_ASSERT_TRUE(codeBuddyApplyJson(
      "{\"entries\":[\"first\",\"second\"],\"msg\":\"second\"}", state, 10));
  TEST_ASSERT_EQUAL_UINT8(2, state.entryCount);
  TEST_ASSERT_EQUAL_UINT16(1, state.entryGeneration);
  TEST_ASSERT_EQUAL_STRING("first", state.entries[0]);
  TEST_ASSERT_EQUAL_STRING("second", state.entries[1]);

  TEST_ASSERT_TRUE(codeBuddyApplyJson(
      "{\"entries\":[\"first\",\"second\"],\"msg\":\"second\"}", state, 20));
  TEST_ASSERT_EQUAL_UINT16(1, state.entryGeneration);

  TEST_ASSERT_TRUE(codeBuddyApplyJson(
      "{\"entries\":[\"second\",\"third\"],\"msg\":\"third\"}", state, 30));
  TEST_ASSERT_EQUAL_UINT8(2, state.entryCount);
  TEST_ASSERT_EQUAL_UINT16(2, state.entryGeneration);
  TEST_ASSERT_EQUAL_STRING("second", state.entries[0]);
  TEST_ASSERT_EQUAL_STRING("third", state.entries[1]);
}

void test_permission_command_uses_expected_wire_format() {
  const String payload = codeBuddyEncodePermission("req_abc123", "once");

  TEST_ASSERT_EQUAL_STRING(
      "{\"cmd\":\"permission\",\"id\":\"req_abc123\",\"decision\":\"once\"}",
      payload.c_str());
}

void test_ack_uses_expected_wire_format() {
  const String payload = codeBuddyEncodeAck("chunk", true, 128);

  TEST_ASSERT_EQUAL_STRING("{\"ack\":\"chunk\",\"ok\":true,\"n\":128}",
                           payload.c_str());
}

void test_transfer_path_validation_rejects_escape_paths() {
  TEST_ASSERT_TRUE(codeBuddyIsSafeTransferPath("manifest.json"));
  TEST_ASSERT_TRUE(codeBuddyIsSafeTransferPath("idle_0.gif"));
  TEST_ASSERT_FALSE(codeBuddyIsSafeTransferPath(nullptr));
  TEST_ASSERT_FALSE(codeBuddyIsSafeTransferPath(""));
  TEST_ASSERT_FALSE(codeBuddyIsSafeTransferPath("/manifest.json"));
  TEST_ASSERT_FALSE(codeBuddyIsSafeTransferPath("../manifest.json"));
  TEST_ASSERT_FALSE(codeBuddyIsSafeTransferPath("nested/../../bad"));
  TEST_ASSERT_FALSE(codeBuddyIsSafeTransferPath("bad\\name"));
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_heartbeat_snapshot_updates_state);
  RUN_TEST(test_missing_prompt_clears_existing_prompt);
  RUN_TEST(test_entries_update_transcript_state);
  RUN_TEST(test_permission_command_uses_expected_wire_format);
  RUN_TEST(test_ack_uses_expected_wire_format);
  RUN_TEST(test_transfer_path_validation_rejects_escape_paths);
  UNITY_END();
}

void loop() {
}
