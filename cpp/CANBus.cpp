#include "CANBus.hpp"
#include "Bus.hpp"
#include "Packet.hpp"
#include <algorithm>
#include <cstring>

//void printlnBytes(const uint8_t *bytes, unsigned len, unsigned split = 0) {
//  Serial.print("0x");
//  for (unsigned n = 0; n < len; n++) {
//    if (split > 0 && n % split == 0)
//      Serial.print('_');
//    if (bytes[n] < 16)
//      Serial.print("0");
//    Serial.print(bytes[n], HEX);
//  }
//  Serial.println();
//}


CANBus *canbus_instance;

void CANReceived(uint32_t ext_id, uint8_t *data, uint8_t len) {
  canbus_instance->received(ext_id, data, len);
}

CANBus::CANBus(uint8_t node_name)
  : Bus(node_name), buf_(buf_buf_, CANBUS_BUFFER_SIZE),
    last_received_(nullptr, 0) {
  canbus_instance = this;
}

bool CANBus::begin() {

  bool ok = Bus::begin();
  CANInit();
  filterChanged();

  return ok;
}

void CANBus::update() {
  Bus::update();

  static unsigned overflow_count = 0;

  unsigned new_overflow = overflow_count - buf_.getOverflowCount();
  if (new_overflow > 0) {
    error("BOF");
    error_count_ += new_overflow;
    overflow_count += new_overflow;
  }
}



bool CANBus::send(Packet &packet) {
  packet.setNode(self_node_);
  packet.setSeq(self_seq_);
  self_seq_++;

  uint8_t frame_num = (packet.len + 7) / 8;
  uint32_t ext_id = packet.buf[0] << 21
                  | packet.buf[1] << 13
                  | id_field(packet.buf[0]) << 5;
  uint8_t frame[8];

  for (unsigned frame_count = 0; frame_count < frame_num; frame_count++) {
    memcpy(frame, packet.buf + frame_count * 8, 8);
    if (frame_count == 0) {
      frame[0] = packet.len;
      frame[1] = frame_num;
    }
    if (!CANSend(ext_id, frame,
                 std::min((unsigned)8, packet.len - frame_count * 8))) {
      error("BCS");
      return false;
    }
    ext_id++;
  }
  return true;
}

const Packet CANBus::receive() {
  buf_.unlock();
  SectionBuf<ReceivedData>::iterator itr = buf_.begin();
  if (itr == buf_.end()) return Packet();

  while (itr != buf_.end()) {
    ReceivedData &rd = (*itr).value();

    if (rd.data == last_received_.buf) {
      (*itr).free();
      ++itr;
      last_received_ = Packet();

      continue;
    }

    if (rd.frame_count + 1 == rd.frame_num && !(*itr).isFree()) {

      unsigned len = (*itr).size() - sizeof(rd);
      last_received_ = Packet(rd.data, len, len);
      buf_.lock(itr);

      break;
    }
    ++itr;
  }
  return last_received_;
}

void CANBus::received(uint32_t ext_id, uint8_t *data, uint8_t dlc) {
  if (!started_) return;

  uint8_t kind_id = ext_id >> 21;
  uint8_t from = (ext_id >> 13) & 0xFF;
  volatile uint8_t frame_count = ext_id & 0b11111;

  if (!filter(kind_id) && (kind_id & 0b01111111) != ID_HEARTBEAT) return;

  if (frame_count == 0) {
    SectionBuf<ReceivedData>::iterator itr = buf_.begin();
    while (itr != buf_.end()) {
      ReceivedData &rd = (*itr).value();
      if (rd.kind_id == kind_id && rd.from == from) {
        (*itr).free();
        buf_.pop();
        error("BDR");
      }
      ++itr;
    }
    volatile uint8_t len = data[0];
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
          (*itr).free();
          buf_.pop();
          error("BDR");
          break;
        }

        memcpy(rd.data + frame_count * 8, data, dlc);
        rd.frame_count++;

        if (rd.frame_count + 1 == rd.frame_num) {
          unsigned len = (*itr).size() - sizeof(rd);
          Packet packet(rd.data, len, len);
          receivedFrom(packet.node(), packet.seq());
          receivedPacket(packet);

          if (packet.id() == ID_HEARTBEAT) {
            (*itr).free();
          }
        }

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
//    printf("%d %u %u\n", i, filter_.field_[i], filterValue[i]);
  }
  uint32_t id = (field ^ 0xFF) << 5;
  uint32_t mask = (field ^ 0xFF) << 5;
  CANSetFilter(id, mask);
}

uint8_t CANBus::id_field(uint8_t id) const {
  return ~(0b1 << (id & 0b111));
}
