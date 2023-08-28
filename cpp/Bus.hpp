#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#include "Packet.hpp"
#include "Shared.hpp"

#include <cstring>

#ifndef HEARTBEAT_FREQ
#define HEARTBEAT_FREQ 1
#endif

#ifndef HEARTBEAT_TIMEOUT_MS
#define HEARTBEAT_TIMEOUT_MS 5000
#endif

#define NODE_MAX 16
#define ENTRIES_MAX 32

#define ID_HEARTBEAT 0x7F
#define ID_SANITY_SUMMARY '?'
#define ID_ERROR_SUMMARY '!'

#define NODE_ID_ADDR 0

#define BUS_FILTER_WIDTH 64

class BloomFilter32 {
public:
  BloomFilter32(uint8_t k);
  void set(unsigned key);
  void setAll();
  void clearAll();

  bool isSet(unsigned key) const;

private:
  uint8_t k_;
  uint32_t field_;
};

// N must be 2^n
template<unsigned N> class BitFilter {
public:
  BitFilter() {
    for (unsigned i = 0; i < N / 8; i++) field_[i] = 0x00;
  };

  inline void set(unsigned key) {
    field_[(key % N) >> 3] |= 0b1 << (key & 0b111);
  }
  void setAll() {
    for (unsigned i = 0; i < N / 8; i++) {
      field_[i] = 0xFF;
    }
  }
  inline void clearAll() {
    for (unsigned i = 0; i < N / 8; i++) field_[i] = 0x0;
  }

  bool isSet(unsigned key) const {
    return field_[key % N >> 3] & (0b1 << (key & 0b111));
  }

  const uint8_t* getValue() const {
    return field_;
  }

//private:
  uint8_t field_[N/8];
};

struct NodeInfo {
  uint8_t name;
  uint8_t error_count;
  uint8_t error_code[3];

  uint8_t first_seq;

  uint16_t sanity_bits;

  unsigned long heartbeat_millis;

  void reset();
  bool alive() const;
};

class Bus {
public:
  Bus(uint8_t node_name);

  virtual bool begin();
  virtual void update();

  // Packet filter
  virtual void listenAll() {
    filter_.setAll();
  }
  virtual void unlistenAll() { filter_.clearAll();}
  virtual void listen(Packet::Kind kind, uint8_t id) {
    filter_.set((kind << 7) | id);
  }
  inline bool filter(uint8_t id) const {return filter_.isSet(id); }


  // Sending Packet
  virtual bool send(Packet &packet) = 0;
  virtual bool availableForSend(const Packet &packet) = 0;

  // Receiving Packet
  virtual const Packet receive() = 0;

  // Get status
  inline unsigned getErrorCount() const { return error_count_; }
  inline unsigned getReceivedCount() const { return received_count_; }
  inline unsigned getLostCount() const { return lost_count_; }

  // Errors
  inline void error(const char error_code[]) {
    error_count_++;
    memcpy(error_code_, error_code, 3);
  };
  Packet& getErrorSummary();

  // Sanity chevk
  bool sanity(unsigned bit, bool isSane);
  bool sanity(unsigned bit) const;
  unsigned insanity() const;
  Packet& getSanitySummary();

  // Shared variables
  template <typename T>
  Shared<T>& subscribe(Shared<T> &variable, uint8_t packet_id,
                       uint8_t entry_type, uint8_t packet_from = 0xFF) {
    shared_.add(variable, packet_id, entry_type, packet_from);
    listen(TELEMETRY, variable.packetId());
    listen(COMMAND, variable.packetId());
    return variable;
  }
  void insert(Shared<uint32_t> &variable);

  // void sendTestPacket();


protected:
  uint8_t self_name_;
  uint8_t self_node_;
  uint8_t self_seq_;
  uint32_t self_unique_;
  NodeInfo nodes[NODE_MAX];

  bool started_;

  unsigned long heartbeat_millis_;

  unsigned received_count_;
  unsigned lost_count_;

  unsigned error_count_;
  uint8_t  error_code_[3];
  uint16_t sanity_bits_;

  BitFilter<BUS_FILTER_WIDTH> filter_;

  SharedVariables shared_;

  void sendHeartbeat();
  void receivedPacket(const Packet &packet);

  bool receivedFrom(uint8_t node_id, uint8_t seq);
};


extern unsigned getMillis();

extern uint8_t readEEPROM(unsigned addr);
extern void writeEEPROM(unsigned addr, uint8_t value);
extern unsigned getUnique();
