#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#include <iterator>
#include "checksum.h"

#define COMMAND 0
#define TELEMETRY 1

#define FROM_LOCAL 0

#define TO_LOCAL 0
#define BROADCAST 0b111

#define ENTRIES_MAX 32

#define ENTRY_LEN_MAX 5
#define PACKET_HEADER_LEN 4
#define PACKET_CRC_LEN 1
#define PACKET_MIN_LEN (PACKET_HEADER_LEN + PACKET_CRC_LEN)

#define BUF_SIZE(size) (PACKET_MIN_LEN + ENTRY_LEN_MAX * size)


class float16 {
public:
  float16(float value = 0.0f);
  float toFloat32() const;
  inline uint16_t getRaw() { return raw_; }
protected:
  uint16_t raw_;
};

class Packet;

class Entry: public std::iterator<std::forward_iterator_tag, uint8_t*> {
public:
  Entry(const Entry& entry);

  uint8_t type() const;
  uint8_t size() const;

  template <typename T> T as() const {
    static uint8_t view[4];
    return *reinterpret_cast<const T *>(decode(view));
  }
  inline const uint8_t* asBytes() const {
    static uint8_t view[4];
    return decode(view);
  }

  Entry& append(uint8_t type, const uint8_t *bytes, unsigned size);
  template <typename T>
  inline Entry &append(uint8_t type, T value) {
    return append(type, reinterpret_cast<const uint8_t *>(&value), sizeof(T));
  }
  inline Entry &append(uint8_t type) { return append(type, 0, 0); }

  Entry next() const;
  uint8_t *operator*();
  Entry &operator++();
  Entry operator++(int);
  inline bool operator==(const Entry &another) {
    return ptr_ == another.ptr_;
  }
  inline bool operator!=(const Entry &another) {
    return ptr_ != another.ptr_;
  }
  Entry &operator=(const Entry &another);

  void print() const;

private:
  Entry(Packet* packet, unsigned ptr, uint8_t count);

  const uint8_t *decode(uint8_t * view) const;

  Packet* packet_;
  unsigned ptr_;
  uint8_t count_;

  friend Packet;
};

struct Packet {
public:
  typedef uint8_t Kind;

  Packet();
  Packet(const Packet& packet);
  Packet(uint8_t *buf, unsigned buf_size);
  Packet(uint8_t *buf, unsigned buf_size, unsigned len);

  inline bool isValid() { return buf != nullptr && buf_size != 0; }

  inline Kind kind_id() const { return buf[0]; }
  inline Kind kind() const { return buf[0] >> 7; }
  inline uint8_t id() const { return buf[0] & 0b1111111; }
  inline uint8_t from() const { return buf[1] >> 5; }
  inline uint8_t node() const { return buf[1] & 0b11111; }
  inline uint8_t dest() const { return buf[2] >> 5; }
  inline uint8_t size() const { return buf[2] & 0b11111; }
  inline uint8_t seq() const { return buf[3]; }

  void set(Kind kind, uint8_t id,
           uint8_t dest = TO_LOCAL, uint8_t from = FROM_LOCAL);
  void setSeq(uint8_t seq);
  void setNode(uint8_t node);
  void setFrom(uint8_t from);

  inline void from(uint8_t unit) {
    buf[1] = ((unit & 0b111) << 5) | (buf[1] & 0b11111);
  }
  void clear();

  inline Entry begin() { return Entry(this, PACKET_HEADER_LEN, 0); };
  inline const Entry begin() const {
    return Entry(const_cast<Packet*>(this), PACKET_HEADER_LEN, 0);
  };
  inline Entry end() { return end_; };
  inline const Entry end() const { return end_; };

  Entry find(uint8_t type, uint8_t index = 0);
  const Entry find(uint8_t type, uint8_t index = 0) const;

  Packet &operator=(const Packet &another);
  bool copyTo(Packet& another) const;

  inline uint8_t getCRC() const { return buf[len - 1]; }
  inline uint8_t calcCRC() const { return crc_8(buf, len - 1); }
  inline bool checkCRC() const { return getCRC() == calcCRC(); }
  inline uint8_t writeCRC() {
    buf[len - 1] = calcCRC();
    return getCRC();
  }

  void print();

  uint8_t* buf;
  unsigned buf_size;
  unsigned len;

private:
  Entry end_;

  void setEnd(Entry& entry);
  void setSize(uint8_t n);

  friend Entry;
};
