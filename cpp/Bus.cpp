#include "Bus.hpp"
#include "Packet.hpp"
#include "UARTBus.hpp"


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
    if (!(field_ & (0b1 << (key_odd & 0b11111)))) return false;
    key_odd += key2;
  }
  return true;
}


bool NodeInfo::received(uint8_t seq) {
  if (first_millis == 0 || seq - first_seq < 128) { // first time received

    if (first_seq == 255) {
      if (seq != 0)
        lost_count += seq;
    } else if (seq != first_seq + 1 && first_millis != 0) {
      lost_count += seq - first_seq;
    }

    received_count++;

    first_seq = seq;
    first_millis = getMillis();
    return true;
  } else {
    second_seq = seq;
    second_millis = getMillis();
    return false;
  }
}

void NodeInfo::reset() {
  first_seq = 0;
  second_seq = 0;
  received_count = 0;
  lost_count = 0;
  first_millis = 0;
  second_millis = 0;
}

Bus::Bus(uint8_t system, uint8_t node_name)
  : system_(system), self_name_(node_name) {
}

bool Bus::begin() {
  self_node_ = readEEPROM(NODE_ID_ADDR) % NODE_MAX;
  self_unique_ = getUnique();

//  filterChanged();

  return true;
}


void Bus::update() {
  if (getMillis() - heartbeat_millis_ > 1000 / HEARTBEAT_FREQ) {
    sendHeartbeat();
    heartbeat_millis_ = getMillis();
  }
}

void Bus::sendAnomaly(uint8_t category, const char *info, bool send_bus) {
  uint8_t buf[BUF_SIZE(1)];
  Packet anomaly(buf, sizeof(buf));
  anomaly.set(TELEMETRY, ID_ANOMALY, FROM_LOCAL, TO_LOCAL);
  anomaly.begin().append(category, reinterpret_cast<const uint8_t*>(info), 4);
  if (send_bus) send(anomaly);
}

void Bus::sendHeartbeat() {
  uint8_t buf[BUF_SIZE(1)];
  Packet heartbeat(buf, sizeof(buf));
  heartbeat.set(TELEMETRY, ID_HEARTBEAT, FROM_LOCAL, TO_LOCAL);
  heartbeat.begin().append('u', self_unique_);
  send(heartbeat);
}

void Bus::receivedPacket(const Packet &packet) {
  if (packet.id() == ID_HEARTBEAT) {
    uint32_t unique = packet.find('u').as<uint32_t>();
    if (packet.node() == self_node_ && unique != self_unique_) {
      error();
      sendAnomaly('B', "CFLT");

      if (unique >= self_unique_) {
        self_node_ = (self_node_ + 1) % NODE_MAX;

        writeEEPROM(NODE_ID_ADDR, self_node_);

        // Serial.printf("CHANGE SELF ID %d\n", self_node_);
        nodes[self_node_].reset();
      }
    }
  }
  else {
    shared_.update(packet);
  }
}

// void Bus::sendTestPacket() {
//   static unsigned n = 0;


//   PacketN<32> test(TELEMETRY, 'z', system_, TO_LOCAL, n);

//   if (!availableForSend(test))
//     return;

//   for (unsigned i = 0; i < n; i++) {
//     test.entries[i].set('I', self_unique_);
//   }

//   send(test);

//   n = (n + 1) % 32;
// }
