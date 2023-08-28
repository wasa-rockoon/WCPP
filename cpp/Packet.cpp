#include "Packet.hpp"

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

float float16::toFloat32() const {
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

Entry::Entry(const Entry &entry)
    : packet_(entry.packet_), ptr_(entry.ptr_), count_(entry.count_) {};

Entry::Entry(Packet* packet, unsigned ptr, uint8_t count)
    : packet_(packet), ptr_(ptr), count_(count){};


uint8_t Entry::type() const {
  return (packet_->buf[ptr_] & 0b00111111) + 64;
};
uint8_t Entry::size() const {
  uint8_t sizes[] = {0, 1, 2, 4};
  return sizes[packet_->buf[ptr_] >> 6];
};


Entry& Entry::append(uint8_t type, const uint8_t *bytes, unsigned size) {
  if (ptr_ + 1 + size > packet_->buf_size)
    return *this;

  unsigned actual_size = size;
  switch (size) {
  case 4:
    if (bytes[2] == 0x00 && bytes[2] == 0x00)
      actual_size = 2;
    else {
      packet_->buf[ptr_ + 4] = bytes[3];
      packet_->buf[ptr_ + 3] = bytes[2];
    }
  case 2:
    if (bytes[1] == 0x00 && actual_size == 2)
      actual_size = 1;
    else
      packet_->buf[ptr_ + 2] = bytes[1];
  case 1:
    if (bytes[0] == 0x00 && actual_size == 1)
      actual_size = 0;
    else
      packet_->buf[ptr_ + 1] = bytes[0];
  }
  uint8_t sizes[] = {0b00, 0b01, 0b10, 0b00, 0b11};
  packet_->buf[ptr_] = ((type - 64) & 0b00111111) | (sizes[actual_size] << 6);
  ptr_ += 1 + this->size();
  count_++;
  packet_->len += 1 + actual_size;
  packet_->setEnd(*this);
  packet_->setSize(packet_->size() + 1);
  return *this;
}

Entry Entry::next() const {
  if (ptr_ + 1 + size() < packet_->len && count_ + 1 < packet_->size()) {
    return Entry(packet_, ptr_ + 1 + (unsigned)size(), count_ + 1);
  }
  else return packet_->end_;
}
uint8_t* Entry::operator*() { return packet_->buf + ptr_ + 1; }
Entry& Entry::operator++() {
  if (ptr_ + 1 + size() < packet_->len && count_ + 1 < packet_->size()) {
    ptr_ += 1 + (unsigned)size();
    count_++;
  } else
    *this = packet_->end_;
  return *this;
}
Entry Entry::operator++(int) {
  Entry result = *this;
  ++(*this);
  return result;
}


Entry& Entry::operator=(const Entry &another) {
  packet_ = another.packet_;
  ptr_    = another.ptr_;
  count_  = another.count_;
  return *this;
}

void Entry::print() const {
#ifdef ARDUINO
  Serial.printf("%c: %d %f", type(), as<int32_t>(), as<float>());
#else
  printf("%c: %d", type(), as<int32_t>());
#endif
}

const uint8_t *Entry::decode(uint8_t *bytes) const {
  bytes[0] = bytes[1] = bytes[2] = bytes[3] = 0;
  switch (size()) {
  case 4:
    bytes[3] = packet_->buf[ptr_ + 4];
    bytes[2] = packet_->buf[ptr_ + 3];
  case 2:
    bytes[1] = packet_->buf[ptr_ + 2];
  case 1:
    bytes[0] = packet_->buf[ptr_ + 1];
  }
  return bytes;
}

Packet::Packet() : buf(nullptr), buf_size(0), len(4), end_(this, 4, 0){};

Packet::Packet(const Packet& packet)
  : buf(packet.buf), buf_size(packet.buf_size), len(packet.len),
    end_(packet.end_) {}

Packet::Packet(uint8_t *buf, unsigned buf_size)
    : buf(buf), buf_size(buf_size), len(4), end_(this, 4, 0) {
  if (buf != nullptr) memset(buf, 0, 4);
};
Packet::Packet(uint8_t *buf, unsigned buf_size, unsigned len)
    : buf(buf), buf_size(buf_size), len(len),
      end_(this, len, buf[2] & 0b11111) {};

void Packet::set(Kind kind, uint8_t id, uint8_t from, uint8_t dest) {
  buf[0] = ((kind & 0b1) << 7) | (id & 0b1111111);
  buf[1] = ((from & 0b111) << 5) | (buf[1] & 0b11111);
  buf[2] = ((dest & 0b111) << 5) | (buf[2] & 0b11111);
}

void Packet::setNode(uint8_t node) {
  buf[1] = (buf[1] & 0b11100000) | (node & 0b11111);
}
void Packet::setSeq(uint8_t seq) {
  buf[3] = seq;
}

void Packet::setSize(uint8_t n) {
  buf[2] = (buf[2] & 0b11100000) | (n & 0b11111);
}

void Packet::clear() {
  len = 4;
  setSize(0);
  end_ = begin();
};

Entry Packet::find(uint8_t type, uint8_t index) {
  uint8_t i = 0;
  for (Entry entry = begin(); entry != end(); ++entry) {
    if (entry.type() == type) {
      if (i == index) return entry;
      i++;
    }
  }
  return end();
}

const Entry Packet::find(uint8_t type, uint8_t index) const {
  uint8_t i = 0;
  for (Entry entry = begin(); entry != end(); ++entry) {
    if (entry.type() == type) {
      if (i == index)
        return entry;
      i++;
    }
  }
  return end();
}

Packet& Packet::operator=(const Packet &another) {
  buf = another.buf;
  len = another.len;
  buf_size = another.buf_size;
  end_ = another.end_;
  return *this;
}

bool Packet::copyTo(Packet& another) const {
  if (len > another.buf_size) return false;
  another.len = len;
  memcpy(another.buf, buf, len);
  another.end_ = Entry(&another, end_.ptr_, end_.count_);
  return true;
}

void Packet::print() {
#ifdef ARDUINO
  Serial.printf("%c(%x) (%d %d -> %d) [%d] #%d\n",
                id(), id(), from(), node(), dest(), size(), seq());
  for (Entry entry = begin(); entry != end(); ++entry) {
    Serial.printf("  ");
    entry.print();
    Serial.printf("\n");
  }
#else
  printf("%c(%x) (%d %d -> %d) [%d] #%d\n",
         id(), id(), from(), node(), dest(), size(), seq());
  for (Entry entry = begin(); entry != end(); ++entry) {
    printf("  ");
    entry.print();
    printf("\n");
  }
#endif
}

void Packet::setEnd(Entry &entry) {
  end_ = entry;
  setSize(entry.count_ - 1);
}
