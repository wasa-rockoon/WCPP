#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#include "Packet.hpp"


#ifndef HEARTBEAT_FREQ
#define HEARTBEAT_FREQ 1
#endif

#define NODE_MAX 32
#define ENTRIES_MAX 32

#define ID_HEARTBEAT 0x7F
#define ID_ANOMALY 0x7E

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

  uint8_t first_seq;
  uint8_t second_seq;

#ifndef NO_DETAIL_NODE_INFO
  unsigned received_count;
  unsigned lost_count;
  unsigned long first_millis;
  unsigned long second_millis;
#endif

  bool received(uint8_t seq); // return true when first time received
  void reset();
};

class Bus {
public:
  Bus(uint8_t system, uint8_t node_name);

  virtual bool begin();

  virtual void listenAll() {
    filter_.setAll();
  }
  virtual void unlistenAll() { filter_.clearAll();}
  virtual void listen(Packet::Kind kind, uint8_t id) {
    filter_.set((kind << 7) | id);
  }
  inline bool filter(uint8_t id) const {return filter_.isSet(id); }

  virtual void update();

  virtual bool send(Packet &packet) = 0;

  const Packet receive();

  virtual bool availableForSend(const Packet &packet) = 0;

  inline void error() { error_count_++; };
  inline void dropped(){ dropped_count_++; };
  inline unsigned getErrorCount() const { return error_count_; }
  virtual inline unsigned getDroppedCount() const { return dropped_count_; }

  void sendAnomaly(uint8_t category, const char *info, bool send_bus = true);
  void sendHeartbeat();

  // void sendTestPacket();

  void receivedHeartbeat(Packet &packet);

protected:
  uint8_t system_;
  uint8_t self_name_;
  uint8_t self_node_;
  uint8_t self_seq_;
  uint32_t self_unique_;
  NodeInfo nodes[NODE_MAX];

  unsigned error_count_;
  uint8_t  error_code_[4];
  unsigned dropped_count_;

  uint32_t sanity_;

  BitFilter<BUS_FILTER_WIDTH> filter_;


  virtual unsigned receive(uint8_t*& buf) = 0;


private:

  unsigned long heartbeat_millis_;
};


unsigned getMillis();

uint8_t readEEPROM(unsigned addr);
void writeEEPROM(unsigned addr, uint8_t value);
unsigned getUnique();
