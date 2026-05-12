#include <Arduino.h>
#include <unity.h>

#include "remote_mic_audio_config.h"

void test_remote_mic_codec_adc_volume_uses_zero_db() {
  TEST_ASSERT_EQUAL_HEX8(0x17, remoteMicCodecAdcVolumeRegister());
  TEST_ASSERT_EQUAL_HEX8(0xBF, remoteMicCodecAdcVolumeValue());
}

void test_remote_mic_speex_preprocess_uses_conservative_gain() {
  TEST_ASSERT_EQUAL_INT(-12, remoteMicSpeexNoiseSuppressionDb());
  TEST_ASSERT_EQUAL_INT(45, static_cast<int>(remoteMicSpeexAgcTargetLevel() * 100));
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_remote_mic_codec_adc_volume_uses_zero_db);
  RUN_TEST(test_remote_mic_speex_preprocess_uses_conservative_gain);
  UNITY_END();
}

void loop() {
}
