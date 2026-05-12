#include <Arduino.h>
#include <unity.h>

#include "remote_mic_audio_config.h"

void test_remote_mic_codec_adc_volume_uses_zero_db() {
  TEST_ASSERT_EQUAL_HEX8(0x17, remoteMicCodecAdcVolumeRegister());
  TEST_ASSERT_EQUAL_HEX8(0xBF, remoteMicCodecAdcVolumeValue());
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_remote_mic_codec_adc_volume_uses_zero_db);
  UNITY_END();
}

void loop() {
}
