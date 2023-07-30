#include "CANBus.hpp"
#include "Bus.hpp"
#include <cstring>

CANBus *instance;

void CANReceived(uint32_t ext_id, uint8_t *data, uint8_t len) {
  instance->received(ext_id, data, len);
}

CANBus::CANBus(uint8_t system, uint8_t node_name)
  : Bus(system, node_name), buf_(buf_buf_, CANBUS_BUFFER_SIZE) {
  instance = this;
}

bool CANBus::begin() {
  bool ok = Bus::begin();

  CANInit();

  return ok;
}

void CANBus::update() {
  Bus::update();
}



bool CANBus::send(const Packet &packet) {
  uint8_t buf[PACKET_LEN_MAX];
  unsigned len = packet.encode(buf, self_node_, self_seq_);
  self_seq_++;

  uint8_t frame_num = (len + 7) / 8;
  uint32_t ext_id = buf[0] << 21 | buf[1] << 13 | id_field(buf[0]) << 5;
  uint8_t frame[8];

  for (unsigned frame_count = 0; frame_count < frame_num; frame_count++) {
    memcpy(frame, buf + frame_count * 8, 8);
    if (frame_count == 0) {
      frame[0] = len;
      frame[1] = frame_num;
    }
    if (!CANSend(ext_id, frame, 8)) return false;
    ext_id++;
  }
  return true;
}

bool CANBus::receiveN(Packet &packet, uint8_t N) {
  SectionBuf<ReceivedData>::iterator itr = buf_.begin();
  while (itr != buf_.end()) {
    ReceivedData &rd = (*itr).value();
    if (rd.frame_count + 1 == rd.frame_num) {
      if (!packet.decode(rd.data, (*itr).size() - sizeof(rd), N)) {
        error();
        sendAnomaly('B', "DECD");
        (*itr).free();
        return false;
      }
      (*itr).free();
      return true;
    }
    ++itr;
  }
  return false;
}

void CANBus::received(uint32_t ext_id, uint8_t *data, uint8_t dlc) {
  uint8_t kind_id = ext_id >> 21;
  uint8_t from = (ext_id >> 13) & 0xFF;
  uint8_t frame_count = ext_id & 0b11111;

  if (frame_count == 0) {
    SectionBuf<ReceivedData>::iterator itr = buf_.begin();
    while (itr != buf_.end()) {
      ReceivedData &rd = (*itr).value();
      if (rd.kind_id == kind_id && rd.from == from) {
        (*itr).free();
      }
      ++itr;
    }
    uint8_t len = data[0];
    ReceivedData &rd = (*buf_.alloc(sizeof(ReceivedData) + len)).value();
    rd.kind_id = kind_id;
    rd.from = from;
    rd.frame_count = frame_count;
    rd.frame_num = data[1];
    rd.data[0] = kind_id;
    rd.data[1] = from;
    memcpy(rd.data + 2, data + 2, 6);
    }
  else {
    SectionBuf<ReceivedData>::iterator itr = buf_.begin();
    while (itr != buf_.end()) {
      ReceivedData &rd = (*itr).value();
      if (rd.kind_id == kind_id && rd.from == from) {
        if (rd.frame_count + 1 != frame_count) {
          buf_.pop();
          error();
          sendAnomaly('B', "RBUF");
          break;
        }

        memcpy(rd.data + frame_count * 8, data, dlc);
        rd.frame_count++;

        break;
      }
      ++itr;
    }
  }
}

void CANBus::filterChanged() {
  const uint8_t* filterValue = filter_.getValue();
  uint8_t field = 0;
  for (unsigned i = 0; i < BUS_FILTER_WIDTH / 8; i++) {
    field |= filterValue[i];
  }
  uint32_t id = (field ^ 0xFF) << 5;
  uint32_t mask = (field ^ 0xFF) << 5;
  CANSetFilter(id, mask);
}

inline uint8_t CANBus::id_field(uint8_t id) const {
  return 0b1 << (id & 0b111);
}
