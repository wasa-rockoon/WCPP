#include "float16.h"
#include "packet.h"

#include <cassert>
#include <cstring>

namespace wccp {

Entry EntriesIterator::operator*() const { return Entry(entries_, ptr_); }

EntriesIterator &EntriesIterator::operator++() {
  // printf("+ %d %d %d\n", ptr_, (**this).size(), entries_.size());
  if (ptr_ + (**this).size() + entry_type_size < entries_.size())
    ptr_ += (**this).size() + entry_type_size;
  else ptr_ = entries_.size();
  return *this;
}

Entry Entries::append(const char name[2]) {
  Entry last(*this, size());
  last.name() = name;
  resize(size(), 2);
  return last;
}

Packet& Packet::command(uint8_t packet_id, uint8_t component_id,
                        uint8_t unit_id) {
  buf_[0] = header_size();
  buf_[1] = packet_id & 0b01111111;
  buf_[2] = component_id;
  buf_[3] = unit_id;
  return *this;
};

Packet& Packet::telemetry(uint8_t packet_id, uint8_t component_id,
                        uint8_t unit_id) {
  buf_[0] = header_size();
  buf_[1] = packet_id | 0b10000000;
  buf_[2] = component_id;
  buf_[3] = unit_id;
  return *this;
};

bool Packet::resize(uint8_t ptr, uint8_t size_from_ptr) {
  if (ptr + size_from_ptr >= buf_size_) return false;
  buf_[0] = ptr + size_from_ptr;
  return true;
}

bool SubEntries::resize(uint8_t ptr, uint8_t size_from_ptr) {
  if (ptr + size_from_ptr >= buf_size_)
    return false;
  buf_[0] = ptr + size_from_ptr;
  parent_.resize(ptr_, buf_[0]);
  return true;
}

Entry::Name &Entry::Name::operator=(const char name[2]) {
  buf_[0] = (buf_[0] & 0b11100000) | (name[0] & 0b00011111);
  buf_[1] = (buf_[1] & 0b11100000) | (name[1] & 0b00011111);
  return *this;
}
bool Entry::Name::operator==(const char name[2]) {
  return (buf_[0] & 0b00011111) == (name[0] & 0b00011111) &&
         (buf_[1] & 0b00011111) == (name[1] & 0b00011111);
}

uint8_t Entry::size() const {
  uint8_t type = getType();
  if (type == 0b000000 || type == 0b000100 || type & 0b100000)
    return 0; // null, 0.0f, short int
  if ((type & 0b010000) == 0b010000)
    return (type & 0b000111) + 1; // int
  if (type >= 0b000101 && type <= 0b000111)
    return 1 << (type & 0b000011); // float
  if (type >= 0b000001 && type <= 0b000011)
    return entries_.buf_[ptr_ + entry_type_size]; // struct, packet, bytes
  if ((type & 0b111000) == 0b001000)
    return type & 0b000111; // short bytes

  assert(false);
}

void Entry::setType(uint8_t type) {
  entries_.buf_[ptr_ + 0] =
    (entries_.buf_[ptr_ + 0] & 0b00011111) | ((type & 0b000111) << 5);
  entries_.buf_[ptr_ + 1] =
    (entries_.buf_[ptr_ + 1] & 0b00011111) | ((type & 0b111000) << 2);
}

uint8_t Entry::getType() const {
  return (entries_.buf_[ptr_ + 0] >> 5) |
         ((entries_.buf_[ptr_ + 1] & 0b11100000) >> 2);
}

int64_t Entry::getSignedInt() const {
  if (matchType(0b010000, 0b110000)) {
    bool is_negative = getType() & 0b001000;
    if (is_negative)
      return - getPayload<uint64_t>((getType() & 0b000111) + 1);
    else
      return getPayload<uint64_t>((getType() & 0b000111) + 1);
  }
  if (matchType(0b100000, 0b100000))
    return getType() & 0b011111;

  return 0;
}

uint64_t Entry::getUnsignedInt() const {
  if (matchType(0b010000, 0b110000)) {
    bool is_negative = getType() & 0b001000;
    if (is_negative)
      return (uint64_t)-getPayload<uint64_t>((getType() & 0b000111) + 1);
    else
      return getPayload<uint64_t>((getType() & 0b000111) + 1);
  }
  if (matchType(0b100000, 0b100000))
    return getType() & 0b011111;

  return 0;
}

float16 Entry::getFloat16() const {
  if (matchType(0b000100, 0b111111))
    return float16();
  if (isFloat16())
    return float16(getPayload<uint16_t>(2));
  if (isFloat32())
    return float16(getPayload<float>(4));
  if (isFloat64())
    return float16((float)getPayload<double>(8));
  if (isInt())
    return float16((float)getSignedInt());

  return float16();
}
float Entry::getFloat32() const {
  if (matchType(0b000100, 0b111111))
    return 0.0f;
  if (isFloat16())
    return (float)float16(getPayload<uint16_t>(2));
  if (isFloat32())
    return getPayload<float>(4);
  if (isFloat64())
    return (float)getPayload<double>(8);
  if (isInt())
    return (float)getSignedInt();

  return 0.0f;
}
double Entry::getFloat64() const {
  if (matchType(0b000100, 0b111111))
    return 0.0;
  if (isFloat16())
    return (double)float16(getPayload<uint16_t>(2));
  if (isFloat32())
    return (double)getPayload<float>(4);
  if (isFloat64())
    return getPayload<double>(8);
  if (isInt())
    return (double)getSignedInt();

  return 0.0;
}

uint8_t Entry::getBytes(uint8_t *bytes) const {
  if (matchType(0b000011)) {
    uint8_t len = getPayload<uint8_t>(1);
    std::memcpy(bytes, getPayloadBuf() + 1, len);
    return len;
  }
  if (matchType (0b001000, 0b111000)) {
    uint8_t len = getType() & 0b000111;
    std::memcpy(bytes, getPayloadBuf(), len);
    return len;
  }
  return 0;
}

uint8_t Entry::getString(char *str) const {
  uint8_t len = getBytes(reinterpret_cast<uint8_t*>(str));
  str[len] = '\0';
  return len;
}

Packet Entry::getPacket() {
  if (isPacket()) return Packet::decode(getPayloadBuf());
  else            return Packet::null();
}

SubEntries Entry::getSubEntries() {
  return SubEntries(entries_, ptr_, getPayloadBuf(), getPayload<uint8_t>(1));
}

bool Entry::setNull() {
  if (!setSize(0))
    return false;
  setType(0b000000);
  return true;
}

bool Entry::setInt(const uint8_t *bytes, uint8_t size, bool is_negative) {
  if (size == 0) {
    if (!setSize(0)) return false;
    setType(0b100000);
  }
  else if (size == 1 && !is_negative && bytes[0] < 32) {
    if (!setSize(0)) return false;
    setType(0b100000 | bytes[0]);
  }
  else {
    if (!setSize(size)) return false;
    if (is_negative) setType(0b011000 | (size - 1));
    else             setType(0b010000 | (size - 1));
    setPayload(bytes, size);
  }
  return true;
}

bool Entry::setFloat16(float value) {
  if (value == 0.0f) {
    if (!setSize(0))
      return false;
    setType(0b000100);
  } else {
    if (!setSize(2))
      return false;
    setType(0b000101);
    float16 value16(value);
    uint16_t raw = value16.getRaw();

    setPayload(raw, 2);
  }
  return true;
}

bool Entry::setFloat32(float value) {
  if (value == 0.0f) {
    if (!setSize(0))
      return false;
    setType(0b000100);
  }
  else {
    if (!setSize(4))
      return false;
    setType(0b000110);
    setPayload(value, 4);
  }
  return true;
}

bool Entry::setFloat64(double value) {
  if (sizeof(double) != 8)
    return setFloat32(value);

  if (value == 0.0f) {
    if (!setSize(0))
      return false;
    setType(0b000100);
  } else {
    if (!setSize(8))
      return false;
    setType(0b000111);
    setPayload(value, 8);
  }
  return true;
}

bool Entry::setBytes(const uint8_t *bytes, uint8_t length) {
  if (length <= 7) {
    if (!setSize(length))
      return false;
    setType(0b001000 | length);
    setPayload(bytes, length);
  }
  else {
    if (!setSize(length + 1))
      return false;
    setType(0b000011);
    setPayload(length, 1);
    setPayload(bytes, length, 1);
  }
  return true;
}

bool Entry::setString(const char *str) {
  return setBytes(reinterpret_cast<const uint8_t*>(str), std::strlen(str));
}

bool Entry::setSize(uint8_t size) {
  return entries_.resize(ptr_, entry_type_size + size);
}

} // namespace wccp
