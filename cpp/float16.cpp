#include "float16.h"

#include <algorithm>
#include <cstring>

float16::float16(float value) {
  uint32_t value32 = *reinterpret_cast<uint32_t *>(&value);
  uint16_t value16;
  volatile unsigned sign = value32 >> 31;
  volatile unsigned exp = (value32 >> 23) & 0xFF;
  volatile unsigned frac = value32 & 0x007FFFFF;

  if (exp == 0) {
    if (frac == 0)
      value16 = sign << 15; // +- Zero
    else
      value16 = (sign << 15) | (frac >> 13); // Denormalized values
  } else if (exp == 0xFF) {
    if (frac == 0)
      value16 = (sign << 15) | 0x7C0; // +- Infinity
    else
      value16 = (sign << 15) | (0x1F << 20) | (frac >> 13); // (S/Q)NaN
  }
  // Normalized values
  else {
    value16 = (sign << 15) |
              (std::min(std::max((int)exp - 127 + 15, 0), 31) << 10) |
              (frac >> 13);
  }
  raw_ = value16;
}

float16::operator float() const {
  uint16_t value16 = raw_;
  uint32_t value32;
  unsigned sign = value16 >> 15;
  unsigned exp = (value16 >> 10) & 0x1F;
  unsigned frac = value16 & 0x03FF;
  if (exp == 0) {
    if (frac == 0)
      value32 = sign << 31; // +- Zero
    else
      value32 = (sign << 31) | (frac << 13); // Denormalized values
  } else if (exp == 0x1F) {
    if (frac == 0)
      value32 = (sign << 31) | 0x7F800000; // +- Infinity
    else
      value32 = (sign << 31) | (0xFF << 23) | (frac << 13); // (S/Q)NaN
  }
  // Normalized values
  else
    value32 = (sign << 31) | ((exp - 15 + 127) << 23) | (frac << 13);

  return *reinterpret_cast<float *>(&value32);
}

