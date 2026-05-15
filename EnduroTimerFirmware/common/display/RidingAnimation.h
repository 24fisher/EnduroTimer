#pragma once

#include <Arduino.h>

const uint32_t RIDING_ANIM_INTERVAL_MS = 300UL;
const uint8_t RIDING_ANIM_WIDTH = 8;

inline uint8_t ridingAnimFrame(uint32_t nowMs) {
  return static_cast<uint8_t>((nowMs / RIDING_ANIM_INTERVAL_MS) % RIDING_ANIM_WIDTH);
}

inline String makeMovingArrow(uint8_t frame) {
  String s;
  for (uint8_t i = 0; i < RIDING_ANIM_WIDTH; ++i) {
    s += (i == frame) ? ">" : " ";
  }
  return s;
}
