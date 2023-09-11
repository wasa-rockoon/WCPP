#include "Bus.hpp"
#include "Packet.hpp"
#include "Shared.hpp"
#include "UARTBus.hpp"
#include <cstring>

unsigned countBits(uint16_t v);

void NodeInfo::reset() {
  name = 0;
  error_count = 0;
  error_code[0] = 0;
  error_code[1] = 0;
  error_code[2] = 0;
  first_seq = 0;
  sanity_bits = 0;
  heartbeat_millis = 0;
}

bool NodeInfo::alive() const {
  return heartbeat_millis != 0 &&
    getMillis() - heartbeat_millis < HEARTBEAT_TIMEOUT_MS;
};

Bus::Bus(uint8_t node_name) : self_name_(node_name) {
  memset(listenings_, 0xFF, LISTENING_MAX);
  listening_all_ = false;
  filter_bits_ = 0;
}

bool Bus::begin() {
  self_node_ = readEEPROM(NODE_ID_ADDR) % NODE_MAX;
  self_unique_ = getUnique();
  memcpy(error_code_, "RST", 3);

  listen(TELEMETRY, ID_HEARTBEAT);

  started_ = true;

  return true;
}

void Bus::update() {
  if (getMillis() - heartbeat_millis_ > 1000 / HEARTBEAT_FREQ) {
    sendHeartbeat();
    heartbeat_millis_ = getMillis();

    sanity(0, connectedNodes() > 0);
  }
}

void Bus::listenAll() {
  listening_all_ = true;
  filter_bits_ = 0xFF;
  filterChanged();
}

bool Bus::listen(Packet::Kind kind, uint8_t id) {
  uint8_t kind_id = (kind << 7) | id;
  filter_bits_ |= 0b1 << (kind_id % 7);
  for (int i = 0; i < LISTENING_MAX; i++) {
    if (listenings_[i] == kind_id) return true;
    if (listenings_[i] == 0xFF) {
      listenings_[i] = kind_id;
      filterChanged();
      return true;
    }
  }
  return false;
}

bool Bus::listenShared(Packet::Kind kind, uint8_t id) {
  uint8_t kind_id = (kind << 7) | id;
  for (int i = LISTENING_MAX - 1; i >= 0; i--) {
    if (listenings_[i] == kind_id)
      return true;
    if (listenings_[i] == 0xFF) {
      listenings_[i] = kind_id;
      return true;
    }
  }
  return false;
}

bool Bus::isListening(uint8_t kind_id) const {
  if (listening_all_) return true;
  for (int i = 0; i < LISTENING_MAX; i++) {
    if (listenings_[i] == 0xFF)
      return false;
    if (listenings_[i] == kind_id)
      return true;
  }
  return false;
}

bool Bus::isListeningShared(uint8_t kind_id) const {
  for (int i = LISTENING_MAX - 1; i >= 0; i--) {
    if (listenings_[i] == 0xFF)
      return false;
    if (listenings_[i] == kind_id)
      return true;
  }
  return false;
}

Packet &Bus::getErrorSummary() {
  static uint8_t buf[BUF_SIZE(NODE_MAX)];
  static Packet summary(buf, sizeof(buf));
  summary.clear();
  summary.set(TELEMETRY, ID_ERROR_SUMMARY);
  Entry itr = summary.begin();

  nodes[self_node_].name = self_name_;
  memcpy(nodes[self_node_].error_code, error_code_, 3);
  nodes[self_node_].error_count = static_cast<uint8_t>(error_count_);
  nodes[self_node_].heartbeat_millis = getMillis();

  for (unsigned n = 0; n < NODE_MAX; n++) {
    if (nodes[n].alive() || n == self_node_) {
      uint32_t error = nodes[n].error_count;
      memcpy(reinterpret_cast<uint8_t *>(&error) + 1, nodes[n].error_code, 3);
      memset(nodes[n].error_code, 0, 3);
      itr.append(nodes[n].name, error);
    }
  }
  return summary;
}

void Bus::printErrorSummary() {
  Packet& summary = getErrorSummary();

  for (Entry entry = summary.begin(); entry != summary.end(); ++entry) {
#ifdef ARDUINO
    Serial.printf("%c: %d (%.3s)\n",
                  entry.type(), entry.as<uint8_t>(), entry.asBytes() + 1);
#else
    printf("%c: %d (%.3s)\n",
           entry.type(),  entry.as<uint8_t>(), entry.asBytes() + 1);
#endif
  }
}

bool Bus::sanity(unsigned bit, bool isSane) {
  sanity_bits_ = (sanity_bits_ & ~(0b1 << bit)) | (isSane ? 0b0 : 0b1 << bit);
  return isSane;
}

bool Bus::sanity(unsigned bit) const {
  return !!(sanity_bits_ & (0b1 << bit));
}


unsigned Bus::insanity() const { return countBits(sanity_bits_); }

Packet &Bus::getSanitySummary() {
  static uint8_t buf[BUF_SIZE(NODE_MAX)];
  static Packet summary(buf, sizeof(buf));
  summary.clear();
  summary.set(TELEMETRY, ID_SANITY_SUMMARY);
  Entry itr = summary.begin();

  nodes[self_node_].sanity_bits = sanity_bits_;

  for (unsigned n = 0; n < NODE_MAX; n++) {
    if (nodes[n].alive() || n == self_node_) {
      uint32_t sane = n | nodes[n].sanity_bits << 8;

      itr.append(nodes[n].name, sane);
    }
  }
  return summary;
}

void Bus::printSanitySummary() {
  Packet &summary = getSanitySummary();

  for (Entry entry = summary.begin(); entry != summary.end(); ++entry) {
#ifdef ARDUINO
    Serial.printf("%c: %x\n", entry.type(),
                  (unsigned)(entry.as<uint32_t>() >> 8));
#else
    printf("%c: %x\n", entry.type(), entry.as<uint32_t>() >> 8);
#endif
  }
}

unsigned Bus::connectedNodes() const {
  unsigned count = 0;
  for (unsigned n = 0; n < NODE_MAX; n++) {
    if (nodes[n].alive() && n != self_node_) count++;
  }
  return count;
}

void Bus::sendHeartbeat() {
  uint8_t buf[BUF_SIZE(5)];
  Packet heartbeat(buf, sizeof(buf));
  heartbeat.set(TELEMETRY, ID_HEARTBEAT, FROM_LOCAL, TO_LOCAL);
  heartbeat.begin()
    .append('u', self_unique_)
    .append('n', self_name_)
    .append('s', sanity_bits_)
    .append('e', error_count_)
    .append('c', error_code_, 3);
  send(heartbeat);
}

void Bus::receivedPacket(const Packet &packet) {
  if (packet.id() == ID_HEARTBEAT) {
    uint32_t unique      = packet.find('u').as<uint32_t>();
    uint8_t  name        = packet.find('n').as<uint8_t>();
    uint32_t sanity_bits = packet.find('s').as<uint32_t>();
    uint32_t error_count = packet.find('e').as<uint32_t>();
    const uint8_t* error_code  = packet.find('c').asBytes();

    nodes[packet.node()].name = name;
    nodes[packet.node()].sanity_bits = sanity_bits;
    nodes[packet.node()].heartbeat_millis = getMillis();
    nodes[packet.node()].error_count = error_count;
    memcpy(nodes[packet.node()].error_code, error_code, 3);

    if (packet.node() == self_node_ && unique != self_unique_) {
      error("BCF");

      if (unique >= self_unique_) {
        self_node_ = (self_node_ + 1) % NODE_MAX;

        writeEEPROM(NODE_ID_ADDR, self_node_);

        nodes[self_node_].reset();
      }
    }
  }
  else if (isListeningShared(packet.kind_id())) {
    shared_.update(packet, nodes[packet.node()].name);
  }
}

bool Bus::receivedFrom(uint8_t node_id, uint8_t seq) {
  NodeInfo &node = nodes[node_id];
  if (node.heartbeat_millis == 0 || seq - node.first_seq < 128) {
    // first time received
    if (node.first_seq == 255) {
      if (seq != 0)
        lost_count_ += seq;
    } else if (seq != node.first_seq + 1 && node.alive()) {
      lost_count_ += seq - node.first_seq;
    }

    received_count_++;

    node.first_seq = seq;
    return true;
  } else {
    return false;
  }
}

unsigned countBits(uint16_t v) {
  unsigned short count = (v & 0x5555) + ((v >> 1) & 0x5555);
  count = (count & 0x3333) + ((count >> 2) & 0x3333);
  count = (count & 0x0f0f) + ((count >> 4) & 0x0f0f);
  return (count & 0x00ff) + ((count >> 8) & 0x00ff);
}
