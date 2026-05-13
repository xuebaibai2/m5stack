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
  return 0x9F;
}

constexpr uint32_t remoteMicCodecI2cFrequency() {
  return 100000;
}

#endif
