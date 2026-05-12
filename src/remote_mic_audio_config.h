#ifndef REMOTE_MIC_AUDIO_CONFIG_H
#define REMOTE_MIC_AUDIO_CONFIG_H

#include <stdint.h>

constexpr uint8_t remoteMicCodecI2cAddress() {
  return 0x18;
}

constexpr uint8_t remoteMicCodecAdcVolumeRegister() {
  return 0x17;
}

constexpr uint8_t remoteMicCodecAdcVolumeValue() {
  return 0xBF;
}

constexpr uint32_t remoteMicCodecI2cFrequency() {
  return 100000;
}

constexpr int remoteMicSpeexNoiseSuppressionDb() {
  return -12;
}

constexpr float remoteMicSpeexAgcTargetLevel() {
  return 0.45f;
}

#endif
