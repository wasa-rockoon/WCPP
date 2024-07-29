#include "float16.h"
#include "packet.h"

#include <cassert>
#include <cstring>

namespace wcpp {

Entry::Name &Entry::Name::operator=(const char name[2]) {
  buf_[0] = (buf_[0] & 0b11100000) | (name[0] & 0b00011111);
  buf_[1] = (buf_[1] & 0b11100000) | (name[1] & 0b00011111);
  return *this;
}

bool Entry::Name::operator==(const char name[2]) {
  return (buf_[0] & 0b00011111) == (name[0] & 0b00011111) &&
         (buf_[1] & 0b00011111) == (name[1] & 0b00011111);
}

bool Entry::Name::operator==(Name name) {
  return *this == name.buf_;
}

char Entry::Name::operator[](int i) const {
  if (i == 0) return (buf_[0] & 0b00011111) + 64; 
  if (i == 1) return (buf_[1] & 0b00011111) + 96; 
  return '\0';
} 


Entry::Name Entry::name() const {
  return Name(entries_.buf_ + ptr_); 
}

uint8_t Entry::size() const {
  uint8_t type = getType();
  if (type == 0b000000 || type == 0b000100 || type & 0b100000)
    return 0; // null, 0.0f, short int
  if ((type & 0b010000) == 0b010000)
    return (type & 0b000111) + 1; // int
  if (type >= 0b000101 && type <= 0b000111)
    return 1 << (type & 0b000011); // float
  if (type == 0b000011)
    return 1 + entries_.buf_[ptr_ + entry_type_size]; // bytes
  if (type == 0b000001 || type == 0b000010)
    return entries_.buf_[ptr_ + entry_type_size]; // struct, packet
  if ((type & 0b111000) == 0b001000)
    return type & 0b000111; // short bytes

  return 0;
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

const Packet Entry::getPacket() const {
  if (isPacket()) return Packet::decode(getPayloadBuf());
  else            return Packet::null();
}

const SubEntries Entry::getStruct() const {
  return SubEntries(entries_, ptr_ + entry_type_size, entries_.buf_, 
                  (int)entries_.buf_size_ + entries_.offset() - ptr_ - entry_type_size);
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

SubEntries Entry::setStruct() {
  setType(0b000001);
  if (setSize(1)) {
    setPayload(1, 1);
  } 
  return SubEntries(entries_, ptr_ + entry_type_size, 
                    entries_.buf_, (int)entries_.buf_size_ + entries_.offset() - ptr_ - entry_type_size);
}

bool Entry::setPacket(const Packet& packet) {
  if (!setSize(packet.size())) return false;
  setType(0b000010);
  setPayload(packet.encode(), packet.size());
  return true; 
}

bool Entry::setSize(uint8_t size) {
  return entries_.resize(ptr_, entry_type_size + size);
}

const uint8_t* Entry::getPayloadBuf() const {
  return entries_.buf_ + ptr_ + entry_type_size;
}
uint8_t* Entry::getPayloadBuf() {
  return entries_.buf_ + ptr_ + entry_type_size;
}


EntriesIterator &EntriesIterator::operator++() {
  if (ptr_ + (**this).size() + entry_type_size < entries_.offset() + entries_.size())
    ptr_ += (**this).size() + entry_type_size;
  else ptr_ = entries_.offset() + entries_.size();
  return *this;
}

EntriesConstIterator &EntriesConstIterator::operator++() {
  if (ptr_ + (**this).size() + entry_type_size < entries_.offset() + entries_.size())
    ptr_ += (**this).size() + entry_type_size;
  else ptr_ = entries_.offset() + entries_.size();
  return *this;
}


EntriesIterator& EntriesIterator::find(const char name[2]) {
  while (*this != entries_.end() && (**this).name() == name) ++(*this);
  return *this;
}

EntriesConstIterator& EntriesConstIterator::find(const char name[2]) {
  while (*this != entries_.end() && (**this).name() == name) ++(*this);
  return *this;
}

Entries::iterator Entries::find(const char name[2]) {
  iterator itr = begin();
  return itr.find(name);
}

Entries::const_iterator Entries::find(const char name[2]) const {
  const_iterator itr = begin();
  return itr.find(name);
}

Entry Entries::append(const char name[2]) {
  Entry last(*this, offset() + size());
  if (resize(offset() + size(), entry_type_size)) {
    last.name() = name;
    last.setNull();
  }
  return last;
}

Packet &Packet::command(uint8_t packet_id, uint8_t component_id) {
  buf_[1] = packet_id & ~(packet_type_mask);
  buf_[2] = component_id;
  buf_[3] = unit_id_local;
  buf_[0] = header_size();
  return *this;
};

Packet &Packet::command(uint8_t packet_id, uint8_t component_id,
                        uint8_t origin_unit_id, uint8_t dest_unit_id, uint16_t sequence) {
  buf_[1] = packet_id & ~(packet_type_mask);
  buf_[2] = component_id;
  buf_[3] = origin_unit_id;
  buf_[4] = dest_unit_id;
  buf_[5] = sequence & 0xFF;
  buf_[6] = sequence >> 8;
  buf_[0] = header_size();
  return *this;
};

Packet &Packet::telemetry(uint8_t packet_id, uint8_t component_id) {
  buf_[1] = packet_id | packet_type_mask;
  buf_[2] = component_id;
  buf_[3] = unit_id_local;
  buf_[0] = header_size();
  return *this;
};

Packet &Packet::telemetry(uint8_t packet_id, uint8_t component_id,
                          uint8_t origin_unit_id, uint8_t dest_unit_id, uint16_t sequence) {
  buf_[1] = packet_id | packet_type_mask;
  buf_[2] = component_id;
  buf_[3] = origin_unit_id;
  buf_[4] = dest_unit_id;
  buf_[5] = sequence & 0xFF;
  buf_[6] = sequence >> 8;
  buf_[0] = header_size();
  return *this;
};

Packet& Packet::operator=(const Packet& packet) {
  buf_ = packet.buf_;
  buf_size_ = packet.buf_size_;
  ref_change_ = packet.ref_change_;
  if (ref_change_ != nullptr) ref_change_(*this, +1);
  return *this;
}

Packet& Packet::operator=(Packet&& packet) {
  buf_ = packet.buf_;
  buf_size_ = packet.buf_size_;
  ref_change_ = packet.ref_change_;
  return *this;
}

bool Packet::copyPayload(const Packet& from) {
  if (buf_size_ - header_size() < from.size() - from.header_size()) return false;

  buf_[0] = header_size() + from.size() - from.header_size();
  std::memcpy(buf_ + header_size(), from.buf_ + header_size(),
              from.size() - from.header_size());
  return true;
}

bool Packet::copy(const Packet& from) {
  if (buf_size_ < from.size()) return false;

  std::memcpy(buf_, from.buf_, from.size() + 1);
  return true;
}

bool Packet::resize(uint8_t ptr, uint8_t size_from_ptr) {
  // printf("RESIZE P %d %d %d\n", ptr, size_from_ptr, buf_size_);
  if ((int)ptr + size_from_ptr > buf_size_) return false;
  buf_[0] = ptr + size_from_ptr;
  return true;
}

bool SubEntries::resize(uint8_t ptr, uint8_t size_from_ptr) {
  // printf("RESIZE S %d %d %d %d\n", ptr, ptr_, size_from_ptr, buf_size_);
  if ((int)ptr + size_from_ptr - offset() > buf_size_)
    return false;
  if (!parent_.resize(ptr, size_from_ptr)) 
    return false;
  buf_[offset()] = (int)ptr + size_from_ptr - offset();
  return true;
}


} // namespace wccp
