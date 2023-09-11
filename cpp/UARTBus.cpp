#include "UARTBus.hpp"

#ifdef ARDUINO

#include "Bus.hpp"
#include "Packet.hpp"
#include "COBS.hpp"

UARTBus::UARTBus(uint8_t node_name, Stream &upper_serial, Stream &lower_serial)
    : Bus(node_name), upper_serial_(upper_serial),
      lower_serial_(lower_serial) {}

bool UARTBus::begin() {
  bool ok = Bus::begin();

#if defined(ARDUINO_ARCH_RP2040)
  queue_init(&send_queue_, 8, SEND_QUEUE_SIZE / 8);
  queue_init(&receive_queue_, 8, RECEIVE_QUEUE_SIZE / 8);
#elif defined(ESP32)
  send_queue_ = xQueueCreate(SEND_QUEUE_SIZE / 8, 8);
  receive_queue_ = xQueueCreate(RECEIVE_QUEUE_SIZE / 8, 8);
#endif

  // Serial.printf("Bus initialized %d %d\n", self_id_, self_unique_);

  return ok;
}


void UARTBus::update() {
  checkSend();

  checkSerial(upper_serial_, lower_serial_, upper_buf_, upper_count_);
  checkSerial(lower_serial_, upper_serial_, lower_buf_, lower_count_);

  Bus::update();
}

inline bool UARTBus::availableForSend(const Packet &packet) {
  return 1 + 4 + (unsigned)packet.size() * 5
    < SEND_QUEUE_SIZE - queueSize(send_queue_);
}


bool UARTBus::send(Packet &packet) {
  packet.setNode(self_node_);
  packet.setSeq(self_seq_);
  packet.writeCRC();
  self_seq_++;

  if (!queuePush(send_queue_, packet.buf, packet.len)) {
    error("BSD");
    return false;
  }
  return true;
}

const Packet UARTBus::receive() {
  unsigned len = queuePop(receive_queue_, receive_buf_);

  return Packet(receive_buf_, len, len);
}

void UARTBus::checkSend() {
  uint8_t buf[PACKET_LEN_MAX];
  unsigned len = queuePop(send_queue_, buf);
  if (len == 0) return;

  uint8_t encoded[PACKET_LEN_MAX + 3];
  unsigned encoded_len = COBS::encode(buf, len, encoded);
  encoded[encoded_len] = 0;

  upper_serial_.write(encoded, encoded_len + 1);
  lower_serial_.write(encoded, encoded_len + 1);

  sendViaDebugSerial(encoded, encoded_len + 1);
}

void UARTBus::checkSerial(Stream &serial, Stream &another_serial, uint8_t* buf,
                  unsigned &count) {

  while (serial.available() > 0) {
    uint8_t data = serial.read();

    buf[count] = data;
    count++;

    if (count + 1 >= PACKET_LEN_MAX) {
      error("BLN");
      count = 0;
      continue;
    }

    if (data == 0) {

      uint8_t decoded[PACKET_LEN_MAX + 2];
      unsigned len = COBS::decode(buf, count - 1, decoded);

      if (count < 2 || len == 0) {
        error("BND");
        count = 0;
        continue;
      }

      Packet packet(decoded, len, len);

      if (!packet.checkCRC()) {
        error("BCR");
        count = 0;
        continue;
      }

      uint8_t kind_id = decoded[0];
      bool received_first = receivedFrom(packet.node(), packet.seq());

      if (received_first) {
        receivedPacket(packet);

        another_serial.write(buf, count);

        if (isListening(kind_id)) {
          if (!queuePush(receive_queue_, decoded, len)) {
            error("BRD");
          }
        }

        sendViaDebugSerial(buf, count);
      }

      count = 0;
    }
  }
}

bool UARTBus::queuePush(queue_t &queue, const uint8_t *data, unsigned len) {
  unsigned size = queueSize(queue);

  if (size + 1 + len >= SEND_QUEUE_SIZE) {
    // uint8_t buf[8];

// #if defined(ARDUINO_ARCH_RP2040)
//     queue_peek_blocking(&queue, buf);
// #endif

//     unsigned priority_new = data[0];
//     unsigned priority_top = buf[1];
//     if (priority_new > priority_top) {
      
//     }
    return false;
  }

  uint8_t buf[8];
  unsigned n = 0;

  while (n < len + 1) {
    for (unsigned i = 0; i < 8; i++) {
      if (n == 0 && i == 0) i++;
      if (n + i >= len + 1) break;
      buf[i] = data[n + i - 1];
    }

    if (n == 0)
      buf[0] = len;

#if defined(ARDUINO_ARCH_RP2040)
    queue_add_blocking(&queue, buf);
#elif defined(ESP32)
    xQueueSend(queue, buf, 1000);
#endif

    n += 8;
  }

  return true;
}

unsigned UARTBus::queuePop(queue_t &queue, uint8_t *data) {
  unsigned size = queueSize(queue);

  if (size == 0) return 0;

  uint8_t buf[8];

#if defined(ARDUINO_ARCH_RP2040)
  queue_peek_blocking(&queue, buf);
#elif defined(ESP32)
  xQueuePeek(queue, buf, 1000);
#endif

  unsigned len = buf[0];

  if (size < len) return 0;

#if defined(ARDUINO_ARCH_RP2040)
  queue_remove_blocking(&queue, buf);
#elif defined(ESP32)
  xQueueReceive(queue, buf, 1000);
#endif

  for (unsigned i = 0; i < 7; i++) {
    if (data != nullptr) data[i] = buf[i+1];
  }

  for (unsigned i = 7; i < len; i += 8) {
#if defined(ARDUINO_ARCH_RP2040)
    if (data != nullptr) {
#if defined(ARDUINO_ARCH_RP2040)
      queue_remove_blocking(&queue, data + i);
#elif defined(ESP32)
      xQueueReceive(queue, data + i, 1000);
#endif
    }
#endif
  }

  return len;
}

inline unsigned UARTBus::queueSize(queue_t &queue) {
#if defined(ARDUINO_ARCH_RP2040)
  return queue_get_level(&queue) * 8;
#elif defined(ESP32)
  return uxQueueMessagesWaiting(queue);
#endif
}

void UARTBus::printNodeInfo() {
  Serial.printf("nodes:\n");
  Serial.printf("  self: %d #%d\n", self_node_, self_seq_);
  for (unsigned i = 0; i < NODE_MAX; i++) {
    NodeInfo& n = nodes[i];
    if (n.first_seq == 0) continue;
    Serial.printf("  %2d %c (#%3d)\n", i, n.name, n.first_seq);
  }
}

#endif
