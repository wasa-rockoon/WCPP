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

#ifndef LISTENING_MAX
#define LISTENING_MAX 8
#endif

#define NODE_MAX 32

#define ID_HEARTBEAT 0x7F
#define ID_SANITY_SUMMARY '?'
#define ID_ERROR_SUMMARY '!'

#define NODE_ID_ADDR 0

#define BUS_FILTER_WIDTH 64


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
  void listenAll();
  bool listen(Packet::Kind kind, uint8_t id);
  // bool unlisten(Packet::Kind kind, uint8_t id) {}

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
  void printErrorSummary();

  // Sanity check
  bool sanity(unsigned bit, bool isSane);
  bool sanity(unsigned bit) const;
  unsigned insanity() const;
  Packet& getSanitySummary();
  void printSanitySummary();

  unsigned connectedNodes() const;

  // Shared variables
  template <typename T>
  Shared<T> &subscribe(Shared<T> &variable, Packet::Kind kind,
                       uint8_t packet_id, uint8_t entry_type, uint8_t index = 0,
                       uint8_t packet_from = FROM_ALL,
                       uint8_t node_name = FROM_ALL) {
    shared_.add(variable, (kind << 7) | packet_id, entry_type, index, packet_from,
                node_name);
    listenShared(kind, variable.packetId());
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

  uint8_t listenings_[LISTENING_MAX];
  bool listening_all_;
  uint8_t filter_bits_;

  SharedVariables shared_;

  virtual void filterChanged() {}

  void sendHeartbeat();
  void receivedPacket(const Packet &packet);

  bool receivedFrom(uint8_t node_id, uint8_t seq);

  bool listenShared(Packet::Kind kind, uint8_t id);
  bool isListening(uint8_t kind_id) const;
  bool isListeningShared(uint8_t kind_id) const;
};


extern unsigned getMillis();

extern uint8_t readEEPROM(unsigned addr);
extern void writeEEPROM(unsigned addr, uint8_t value);
extern unsigned getUnique();
