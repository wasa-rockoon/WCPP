#pragma once

#include "Packet.hpp"

extern unsigned getMillis();

class SharedVariables;

#define NEVER 0xFFFFFFFF

template <typename T> class Shared {
public:
  Shared(): last_updated_millis_(NEVER), next_id_(nullptr), next_(nullptr) {}
  Shared(T initial_value)
    : value_(initial_value), last_updated_millis_(0), next_id_(nullptr),
      next_(nullptr) {}


  inline T value() { return value_; }
  unsigned age() {
    if (last_updated_millis_ == NEVER) return NEVER;
    else return getMillis() - last_updated_millis_;
  }
  bool isValid() {
    if (last_updated_millis_ == NEVER) return false;
    else if (timeout_millis_ == NEVER) return true;
    else return age() < timeout_millis_;
  }

  inline uint8_t packetId() { return packet_id_; }
  inline uint8_t entryType() { return entry_type_; }

  inline void setTimeout(unsigned timeout_millis) {
    timeout_millis_ = timeout_millis;
  }

  inline void appendIfValid(Packet& packet, uint8_t type) {
    if (isValid()) packet.end().append(type, value());
  }

private:
  uint8_t packet_id_;
  uint8_t packet_from_;
  uint8_t entry_type_;
  T value_;
  unsigned timeout_millis_;
  unsigned long last_updated_millis_;
  Shared<uint32_t> *next_id_; // pointer to next node which has different id
  Shared<uint32_t> *next_; // pointer to next node which has same id

  inline Shared<uint32_t>& cast() {
    return *reinterpret_cast<Shared<uint32_t>*>(this);
  }

  friend SharedVariables;
};

class SharedVariables {
public:
  SharedVariables(): root_(nullptr) {}
  void update(const Packet& packet);

  template <typename T> inline void
  add(Shared<T>& variable, uint8_t packet_id, uint8_t entry_type,
      uint8_t packet_from = 0xFF, unsigned timeout_millis = NEVER) {
    variable.timeout_millis_ = timeout_millis;
    variable.packet_id_ = packet_id;
    variable.entry_type_ = entry_type;
    variable.packet_from_ = packet_from;
    insert(variable.cast());
  }

private:
  Shared<uint32_t>* root_;

  void insert(Shared<uint32_t>& variable);
};

