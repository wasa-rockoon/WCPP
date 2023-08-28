#include "Bus.hpp"
#include "Packet.hpp"
#include "Shared.hpp"
#include "UARTBus.hpp"

unsigned countBits(uint16_t v);

BloomFilter32::BloomFilter32(uint8_t k) {
  k_ = k;
  field_ = 0;
}

void BloomFilter32::set(unsigned key) {
  unsigned key_odd = key;
  unsigned key2 = key << 1;
  key_odd += key2;
  for (unsigned n = 0; n < k_; n++) {
    field_ |= 0b1 << (key_odd & 0b11111);
    key_odd += key2;
  }
}

inline void BloomFilter32::setAll() { field_ = 0xFFFFFFFF; }

inline void BloomFilter32::clearAll() { field_ = 0x0; }

bool BloomFilter32::isSet(unsigned key) const {
  unsigned key_odd = key;
  unsigned key2 = key << 1;
  key_odd += key2;
  for (unsigned n = 0; n < k_; n++) {
    if (!(field_ & (0b1 << (key_odd & 0b11111))))
      return false;
    key_odd += key2;
  }
  return true;
}


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
  return getMillis() - heartbeat_millis < HEARTBEAT_TIMEOUT_MS;
};

Bus::Bus(uint8_t node_name) : self_name_(node_name) {}

bool Bus::begin() {
  self_node_ = readEEPROM(NODE_ID_ADDR) % NODE_MAX;
  self_unique_ = getUnique();

  listen(TELEMETRY, ID_HEARTBEAT);

  started_ = true;

  return true;
}

void Bus::update() {
  if (getMillis() - heartbeat_millis_ > 1000 / HEARTBEAT_FREQ) {
    sendHeartbeat();
    heartbeat_millis_ = getMillis();
  }
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

void Bus::sendHeartbeat() {
  uint8_t buf[BUF_SIZE(3)];
  Packet heartbeat(buf, sizeof(buf));
  heartbeat.set(TELEMETRY, ID_HEARTBEAT, FROM_LOCAL, TO_LOCAL);
  heartbeat.begin()
    .append('u', self_unique_)
    .append('n', self_name_)
    .append('s', sanity_bits_);
  send(heartbeat);
}

void Bus::receivedPacket(const Packet &packet) {
  if (packet.id() == ID_HEARTBEAT) {
    uint32_t unique = packet.find('u').as<uint32_t>();
    uint8_t name = packet.find('n').as<uint8_t>();
    uint32_t sanity_bits = packet.find('s').as<uint32_t>();

    nodes[packet.node()].name = name;
    nodes[packet.node()].sanity_bits = sanity_bits;
    nodes[packet.node()].heartbeat_millis = getMillis();

    if (packet.node() == self_node_ && unique != self_unique_) {
      error("BCF");

      if (unique >= self_unique_) {
        self_node_ = (self_node_ + 1) % NODE_MAX;

        writeEEPROM(NODE_ID_ADDR, self_node_);

        nodes[self_node_].reset();
      }
    }
  } else {
    shared_.update(packet);
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

// void Bus::sendTestPacket() {
//   static unsigned n = 0;

//   PacketN<32> test(TELEMETRY, 'z', unit_, TO_LOCAL, n);

//   if (!availableForSend(test))
//     return;

//   for (unsigned i = 0; i < n; i++) {
//     test.entries[i].set('I', self_unique_);
//   }

//   send(test);

//   n = (n + 1) % 32;
// }

unsigned countBits(uint16_t v) {
  unsigned short count = (v & 0x5555) + ((v >> 1) & 0x5555);
  count = (count & 0x3333) + ((count >> 2) & 0x3333);
  count = (count & 0x0f0f) + ((count >> 4) & 0x0f0f);
  return (count & 0x00ff) + ((count >> 8) & 0x00ff);
}
