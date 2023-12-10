#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

class float16 {
public:
  float16(float value = 0.0f);
  float16(uint16_t raw): raw_(raw) {};
  operator float() const;
  inline uint16_t getRaw() { return raw_; }

protected:
  uint16_t raw_;
};
