#pragma once

#include "Packet.hpp"

extern unsigned getMillis();

class SharedVariables;

#define NEVER 0xFFFFFFFF
#define FROM_ALL 0xFF

template <typename T> class Shared {
public:
  Shared()
    : last_updated_millis_(NEVER), next_id_(nullptr), next_(nullptr), dummy_(0) {}
  Shared(T initial_value)
    : value_(*(uint32_t*)&initial_value), last_updated_millis_(0),
      next_id_(nullptr), next_(nullptr), dummy_(0) {}


  inline T value() { return *(T*)&value_; }
  inline T setValue(T v) { value_ = *(uint32_t*)&v; return value(); }
  unsigned age() {
    if (last_updated_millis_ == NEVER) return NEVER;
    else return getMillis() - last_updated_millis_;
  }
  bool isValid() {
    if (last_updated_millis_ == NEVER) return false;
    else if (timeout_millis_ == NEVER) return true;
    else return age() < timeout_millis_;
  }

  inline uint8_t packetId() { return kind_id_; }
  inline uint8_t entryType() { return entry_type_; }

  inline Shared& setTimeout(unsigned timeout_millis) {
    timeout_millis_ = timeout_millis;
    return *this;
  }
  inline Shared& from(uint8_t packet = FROM_ALL, uint8_t node_name = FROM_ALL) {
    packet_from_ = packet;
    node_name_ = node_name;
    return *this;
  }

  inline void appendIfValid(Packet& packet, uint8_t type) {
    if (isValid()) packet.end().append(type, value());
  }

// private:
  uint8_t kind_id_;
  uint8_t entry_type_;
  uint8_t index_;
  uint8_t packet_from_;
  uint8_t node_name_;
  uint32_t value_;
  unsigned timeout_millis_;
  unsigned long last_updated_millis_;
  Shared<uint32_t> *next_id_; // pointer to next node which has different id
  Shared<uint32_t> *next_; // pointer to next node which has same id
  unsigned dummy_;

  inline Shared<uint32_t>& cast() {
    return *reinterpret_cast<Shared<uint32_t>*>(this);
  }

  friend SharedVariables;
};

class SharedVariables {
public:
  SharedVariables(): root_(nullptr) {}
  void update(const Packet& packet, uint8_t node_name);

  template <typename T> inline void
  add(Shared<T>& variable, uint8_t kind_id, uint8_t entry_type, uint8_t index = 0,
      uint8_t packet_from = FROM_ALL, uint8_t node_name = FROM_ALL) {
    variable.kind_id_ = kind_id;
    variable.entry_type_ = entry_type;
    variable.index_ = index;
    variable.packet_from_ = packet_from;
    variable.node_name_ = node_name;
    insert(variable.cast());
  }

private:
  Shared<uint32_t>* root_;

  void insert(Shared<uint32_t>& variable);
};

