#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#include <cstring>
#include <stdio.h>
#include "cppcrc.h"
#include "float16.h"

namespace wcpp {

class Entries;
class SubEntries;
class Packet;
class EntriesIterator;
class EntriesConstIterator;
class Entry;

constexpr unsigned size_max = 255;

constexpr uint8_t entry_type_size   = 2;
constexpr uint8_t unit_id_local     = 0x00;
constexpr uint8_t component_id_self = 0x00;
constexpr uint8_t packet_type_mask  = 0b10000000;
constexpr uint8_t packet_id_mask    = 0b01111111;


class Entry {
public:
  class Name {
  public:
    Name &operator=(const char name[2]);
    bool operator==(const char name[2]);
    bool operator==(Name name);

    char operator[](int i) const; 

  private:
    Name(uint8_t* buf) : buf_(buf){};

    uint8_t *buf_;

    friend Entry;
  };

  Name name() const;
  uint8_t size() const;

  operator bool() const;

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
  inline bool isStruct() const { return matchType(0b000001); }

  inline bool getBool() const { return getSignedInt(); }
  inline uint64_t getUInt() const { return getUnsignedInt(); }
  inline int64_t  getInt()  const { return getSignedInt(); }

  float16 getFloat16() const;
  float   getFloat32() const;
  double  getFloat64() const;
  uint8_t getBytes(uint8_t* bytes) const;
  uint8_t getString(char* str) const;
  const SubEntries getStruct() const;
  const Packet getPacket() const;

  void remove();

  bool setNull();
  template<typename T> bool setInt(T value) {
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
  SubEntries setStruct();
  bool setPacket(const Packet& packet);

private: 
  Entries &entries_;
  uint8_t ptr_;

  Entry(Entries& entries, uint8_t ptr) : entries_(entries), ptr_(ptr) {}

  inline bool matchType(uint8_t value, uint8_t mask = 0b111111) const {
    return (getType() & mask) == value;
  }

  void setType(uint8_t type);
  uint8_t getType() const;
  bool setSize(uint8_t size_new);

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
  const uint8_t* getPayloadBuf() const;
  uint8_t* getPayloadBuf();

  bool setInt(const uint8_t *bytes, uint8_t size, bool is_negative);

  int64_t getSignedInt() const;
  uint64_t getUnsignedInt() const;

  friend EntriesIterator;
  friend EntriesConstIterator;
  friend Entries;
};


class EntriesIterator {
public:
  inline Entry operator*() const { return Entry(entries_, ptr_); }
  EntriesIterator &operator++();
  inline bool operator==(const EntriesIterator &i) const { return ptr_ == i.ptr_; }
  inline bool operator!=(const EntriesIterator &i) const { return ptr_ != i.ptr_; }

  EntriesIterator &find(const char name[2]);
  Entry insert(const char name[2]);
  EntriesIterator remove() { (**this).remove(); return *this; }

private:
  Entries &entries_;
  uint8_t ptr_;

  EntriesIterator(Entries& entries, uint8_t ptr): entries_(entries), ptr_(ptr) {}

  friend Entries;
};

class EntriesConstIterator {
public:
  inline const Entry operator*() const { return Entry(const_cast<Entries&>(entries_), ptr_); }
  EntriesConstIterator &operator++();
  bool operator==(const EntriesConstIterator &i) const { return ptr_ == i.ptr_; }
  bool operator!=(const EntriesConstIterator &i) const { return ptr_ != i.ptr_; }

  EntriesConstIterator &find(const char name[2]);

private:
  const Entries &entries_;
  uint8_t ptr_;

  EntriesConstIterator(const Entries& entries, uint8_t ptr): entries_(entries), ptr_(ptr) {}

  friend Entries;
};


class Entries {
public:
  using iterator = EntriesIterator;
  using const_iterator = EntriesConstIterator;

  inline iterator begin() { return iterator(*this, offset() + header_size()); }
  inline const_iterator begin() const { return const_iterator(*this, offset() + header_size()); }
  inline iterator end() { return iterator(*this, offset() + size()); }
  inline const_iterator end() const { return const_iterator(*this, offset() + size()); }
  iterator at(unsigned n);
  const_iterator at(unsigned n) const;
  
  virtual uint8_t size() const = 0;
  virtual uint8_t header_size() const = 0;

  inline int size_remain() const { return (int)buf_size_ - (int)size(); }

  iterator find(const char name[2]);
  const_iterator find(const char name[2]) const;

  inline const uint8_t* getBuf() const { return buf_; }
  inline uint8_t* getBuf() { return buf_; }

  Entry append(const char name[2]);

  void clear();

protected:
  uint8_t* buf_;
  uint8_t buf_size_;

  Entries(): buf_(nullptr), buf_size_(0) {};
  Entries(uint8_t *buf, uint8_t buf_size): buf_(buf), buf_size_(buf_size) {};

private:
  virtual uint8_t offset() const = 0;
  virtual bool resize(uint8_t ptr, uint8_t size_from_ptr, uint8_t size_from_ptr_old) = 0;

  friend SubEntries;
  friend Entry;
  friend iterator;
  friend const_iterator;
};


class Packet: public Entries {
public:
  using ref_change_t = void (*)(const Packet&, int);

  // Constructors
  static Packet null() { return Packet(); }
  static Packet empty(uint8_t* buf, uint8_t N, ref_change_t ref_change = nullptr) { 
    buf[0] = 0;
    return Packet(buf, N, ref_change); 
  }
  static const Packet decode(const uint8_t* buf, ref_change_t ref_change = nullptr) { 
    Packet p = Packet(const_cast<uint8_t*>(buf), ref_change); 
    return p;
  }

  inline Packet(const Packet& packet): Packet(packet.buf_, packet.buf_size_, packet.ref_change_) {
    if (!isNull() && ref_change_ != nullptr) (*ref_change_)(*this, +1);
  }
  inline Packet(Packet&& packet): Packet(packet.buf_, packet.buf_size_, packet.ref_change_) {
    packet.buf_ = nullptr;
  }

  // Setting header
  Packet &command(uint8_t packet_id, uint8_t component_id = component_id_self);
  Packet &command(uint8_t packet_id, uint8_t component_id,
                  uint8_t origin_unit_id, uint8_t dest_unit_id, uint16_t squence = 0);
  Packet &telemetry(uint8_t packet_id, uint8_t component_id = component_id_self);
  Packet &telemetry(uint8_t packet_id, uint8_t component_id,
                    uint8_t origin_unit_id, uint8_t dest_unit_id, uint16_t squence = 0);

  // Get info

  inline uint8_t offset() const override { return 0; }
  inline uint8_t size() const override { return isNull() ? 0 : buf_[0]; }
  inline uint8_t header_size() const override { return isLocal() ? 4 : 7; }

  inline bool isNull()      const { return buf_ == nullptr; }
  inline bool isCommand()   const { return  !(buf_[1] & packet_type_mask); }
  inline bool isTelemetry() const { return !!(buf_[1] & packet_type_mask); }
  inline bool isLocal()     const { return buf_[3] == unit_id_local; }
  inline bool isRemote()    const { return buf_[3] != unit_id_local; }

  inline operator bool() const { return !isNull(); }
  bool operator!() const { return isNull(); }

  inline uint8_t packet_id()      const { return buf_[1] & packet_id_mask; }
  inline uint8_t type_and_id()    const { return buf_[1]; }
  inline uint8_t component_id()   const { return buf_[2]; }
  inline uint8_t origin_unit_id() const { return isRemote() ? buf_[3] : unit_id_local; }
  inline uint8_t dest_unit_id()   const { return isRemote() ? buf_[4] : unit_id_local; }
  inline uint16_t sequence()      const { return isRemote() ? *(uint16_t*)(buf_ + 5) : 0; }

  inline uint8_t checksum() const { return checksum(buf_, size()); };
  inline static uint8_t checksum(const uint8_t* buf, uint8_t size) {
    return CRC8::CRC8::calc(buf, size);
  }

  Packet& operator=(const Packet& packet);
  Packet& operator=(Packet&& packet);

  inline const uint8_t* encode() const { return buf_; }

  bool copyPayload(const Packet& from);
  bool copy(const Packet& from);

  void clear();
  inline ~Packet() { clear(); }

private:
  ref_change_t ref_change_;

  inline Packet(): ref_change_(nullptr) {}
  inline Packet(uint8_t* buf, ref_change_t ref_change = nullptr): Packet(buf, buf[0], ref_change) {}

  inline Packet(uint8_t* buf, uint8_t buf_size, ref_change_t ref_change = nullptr)
  : Entries(buf, buf_size), ref_change_(ref_change) {}


  bool resize(uint8_t ptr, uint8_t size_from_ptr, uint8_t size_from_ptr_old) override;

  friend Entry;
};


class SubEntries : public Entries {
public:

  inline uint8_t offset() const override { return ptr_; }
  inline uint8_t size() const override { return buf_[offset()]; }
  inline uint8_t header_size() const override { return 1; }

private:
  Entries &parent_;
  uint8_t ptr_;

  SubEntries(Entries& parent, uint8_t ptr, uint8_t* buf, uint8_t buf_size)
    : Entries(buf, buf_size), parent_(parent), ptr_(ptr) {}

  bool resize(uint8_t ptr, uint8_t size_from_ptr, uint8_t size_from_ptr_old) override;

  friend Entry;
};


} // namespace wccp
