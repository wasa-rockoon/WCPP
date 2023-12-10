#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#include "checksum.h"
#include "float16.h"
#include <concepts>
#include <cstring>
#include <stdio.h>

namespace wccp {

class Entries;
class SubEntries;
class Packet;
class EntriesIterator;
class Entry;

constexpr uint8_t entry_type_size = 2;
constexpr uint8_t unit_id_local = 0;
constexpr uint8_t component_id_self = 0;

class EntriesIterator {
public:
  Entry operator*() const;
  EntriesIterator &operator++();
  bool operator==(const EntriesIterator &i) const { return ptr_ == i.ptr_; }
  bool operator!=(const EntriesIterator &i) const { return ptr_ != i.ptr_; }

private:
  Entries &entries_;
  uint8_t ptr_;

  EntriesIterator(Entries& entries, uint8_t ptr): entries_(entries), ptr_(ptr) {}

  friend Entries;
};


class Entries {
public:
  using iterator = EntriesIterator;

  inline iterator begin() { return iterator(*this, header_size()); }
  inline iterator end() { return iterator(*this, size()); }

  inline virtual uint8_t size() const = 0;
  inline virtual uint8_t header_size() const = 0;
  Entry append(const char name[2]);

protected:
  uint8_t* buf_;
  uint8_t buf_size_;

  Entries(): buf_(nullptr), buf_size_(0) {};
  Entries(uint8_t *buf, uint8_t buf_size): buf_(buf), buf_size_(buf_size) {};

private:
  virtual bool resize(uint8_t ptr, uint8_t size_from_ptr) = 0;

  friend SubEntries;
  friend Entry;
};


class Packet: public Entries {
public:
  using iterator = EntriesIterator;

  static Packet null() { return Packet(); }
  static Packet decode(uint8_t* buf) { return Packet(buf); }
  template <uint8_t N>
  static Packet empty(uint8_t buf[N]) { return Packet(buf, N); }

  Packet& command(uint8_t packet_id, uint8_t component_id,
                  uint8_t unit_id = unit_id_local);
  Packet& telemetry(uint8_t packet_id, uint8_t component_id = component_id_self,
                    uint8_t unit_id = unit_id_local);

  inline uint8_t size() const override {
    if (isNull())
      return 0;
    return buf_[0];
  }
  inline uint8_t header_size() const override { return 4; }

  inline bool isNull()      const { return buf_ == nullptr; }
  inline bool isCommand()   const { return !(buf_[1] & 0b10000000); }
  inline bool isTelemetry() const { return !!(buf_[1] & 0b10000000); }

  inline uint8_t packet_id()    const { return buf_[1] & 0b01111111; }
  inline uint8_t component_id() const { return buf_[2]; }
  inline uint8_t unit_id()      const { return buf_[3]; }
  inline uint8_t checksum()     const { return buf_[size()]; }

private:

  Packet() {}
  Packet(uint8_t* buf) : Entries(buf, buf[0]) {}
  Packet(uint8_t* buf, uint8_t buf_size): Entries(buf, buf_size) {}

  bool resize(uint8_t ptr, uint8_t size_from_ptr) override;

  friend Entry;
};


class SubEntries : public Entries {
public:
  inline uint8_t size() const override { return buf_[0]; }
  inline uint8_t header_size() const override { return 1; }

private:
  Entries &parent_;
  uint8_t ptr_;

  SubEntries(Entries& parent, uint8_t ptr, uint8_t* buf, uint8_t buf_size)
    : Entries(buf, buf_size), parent_(parent), ptr_(ptr) {}

  bool resize(uint8_t ptr, uint8_t size_from_ptr) override;

  friend Entry;
};


class Entry {
public:
  class Name {
  public:
    Name &operator=(const char name[2]);
    bool operator==(const char name[2]);

  private:
    Name(uint8_t* buf) : buf_(buf){};

    uint8_t *buf_;

    friend Entry;
  };

  inline Name name() { return Name(entries_.buf_ + ptr_); }
  uint8_t size() const;


  inline bool isNull()   const { return matchType(0b000000); }
  inline bool isInt()    const {
    return matchType(0b010000, 0b110000) || matchType(0b100000, 0b100000);
  }
  inline bool isFloat()   const { return matchType(0b000100, 0b111100); }
  inline bool isFloat16() const { return matchType(0b000101); }
  inline bool isFloat32() const { return matchType(0b000110); }
  inline bool isFloat64() const { return matchType(0b000111); }
  inline bool isBytes()  const {
    return matchType(0b000011) || matchType(0b001000, 0b111000);
  }
  inline bool isPacket() const { return matchType(0b000010); }
  inline bool isStruct() const { return matchType(0b000011); }


  inline bool getBool() const { return getSignedInt(); }
  inline uint64_t getUInt() const { return getUnsignedInt(); }
  inline int64_t  getInt()  const { return getSignedInt(); }

  float16 getFloat16() const;
  float   getFloat32() const;
  double  getFloat64() const;
  uint8_t getBytes(uint8_t* bytes) const;
  uint8_t getString(char* str) const;
  Packet  getPacket();
  SubEntries getSubEntries();


  bool setNull();
  template<std::integral T> bool setInt(T value) {
    uint8_t size = 0;
    bool is_negative = value < 0;
    if (is_negative) value = - value;
    T v = value;
    while (v > 0) {
      size++;
      v = v >> 8;
    }
    uint8_t bytes[8];
    std::memcpy(bytes, &value, size);
    return setInt(bytes, size, is_negative);
  }
  inline bool setBool(bool value) { return setInt(value); }
  template <typename T> inline bool setEnum(T value) {
    return setInt(static_cast<int>(value));
  }
  bool setFloat16(float value);
  bool setFloat32(float value);
  bool setFloat64(double value);
  bool setBytes(const uint8_t *bytes, uint8_t length);
  bool setString(const char *str);


private:
  Entries &entries_;
  uint8_t ptr_;

  Entry(Entries& entries, uint8_t ptr) : entries_(entries), ptr_(ptr) {}

  inline bool matchType(uint8_t value, uint8_t mask = 0b111111) const {
    return (getType() & mask) == value;
  }

  void setType(uint8_t type);
  uint8_t getType() const;
  bool setSize(uint8_t size);

  inline void setPayload(const uint8_t *payload, uint8_t size,
                         uint8_t offset = 0) {
    std::memcpy(getPayloadBuf() + offset, payload, size);
  }
  template <typename T> inline void setPayload(T payload, uint8_t size) {
    std::memcpy(getPayloadBuf(), &payload, size);
  }
  template <typename T> inline T getPayload(uint8_t size) const {
    T payload = 0;
    std::memcpy(&payload, getPayloadBuf(), size);
    return payload;
  }
  inline const uint8_t* getPayloadBuf() const {
    return entries_.buf_ + ptr_ + entry_type_size;
  }
  inline uint8_t* getPayloadBuf() {
    return entries_.buf_ + ptr_ + entry_type_size;
  }

  bool setInt(const uint8_t *bytes, uint8_t size, bool is_negative);

  int64_t getSignedInt() const;
  uint64_t getUnsignedInt() const;

  friend EntriesIterator;
  friend Entries;
};

} // namespace wccp
