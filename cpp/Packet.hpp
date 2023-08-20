#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#include <iterator>

#define COMMAND 0
#define TELEMETRY 1

#define FROM_LOCAL 0

#define TO_LOCAL 0
#define BROADCAST 0b111

#define ENTRIES_MAX 32

#define BUF_SIZE(size) (4 + 5 * size)


class float16 {
public:
  float16(float value = 0.0f);
  float toFloat32() const;
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
    uint8_t view[4];
    return *reinterpret_cast<const T *>(decode(view));
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

  inline Kind kind() const { return buf[0] >> 7; }
  inline uint8_t id() const { return buf[0] & 0b1111111; }
  inline uint8_t from() const { return buf[1] >> 5; }
  inline uint8_t node() const { return buf[1] & 0b11111; }
  inline uint8_t dest() const { return buf[2] >> 5; }
  inline uint8_t size() const { return buf[2] & 0b11111; }
  inline uint8_t seq() const { return buf[3]; }

  void set(Kind kind, uint8_t id, uint8_t from, uint8_t dest);
  void setSeq(uint8_t seq);
  void setNode(uint8_t node);

  inline void from(uint8_t unit) {
    buf[1] = ((unit & 0b111) << 5) | (buf[1] & 0b11111);
  }
  void clear();

  inline Entry begin() { return Entry(this, 4, 0); };
  inline const Entry begin() const {
    return Entry(const_cast<Packet*>(this), 4, 0);
  };
  inline Entry end() { return end_; };
  inline const Entry end() const { return end_; };

  Entry find(uint8_t type, uint8_t index = 0);
  const Entry find(uint8_t type, uint8_t index = 0) const;

  Packet &operator=(const Packet &another);
  bool copyTo(Packet& another) const;

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
