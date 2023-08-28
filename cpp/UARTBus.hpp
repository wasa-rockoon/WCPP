#pragma once


#ifdef ARDUINO

#include <Arduino.h>

#include "Bus.hpp"
#include "Packet.hpp"


#define BUS_SERIAL_BAUD 115200

#define BUS_DEBUG_SERIAL

#ifndef SEND_QUEUE_SIZE
#define SEND_QUEUE_SIZE 128
#endif

#ifndef RECEIVE_QUEUE_SIZE
#define RECEIVE_QUEUE_SIZE 1024
#endif

#ifndef PACKET_LEN_MAX
#define PACKET_LEN_MAX 256
#endif


#ifndef NODE_ID_ADDR
#define NODE_ID_ADDR 0
#endif

#define HEARTBEAT_ID 0xFF

#if defined (ARDUINO_ARCH_RP2040)

#elif defined(ESP32)
typedef QueueHandle_t queue_t;
#else
typedef int queue_t;
#endif


class UARTBus: public Bus {
public:
  UARTBus(uint8_t node_name, Stream &upper_serial, Stream &lower_serial);

  bool begin();

  void update();

  bool send(Packet &packet) override;
  const Packet receive() override;

  bool availableForSend(const Packet &packet);

  void printNodeInfo();

private:
  Stream &upper_serial_;
  Stream &lower_serial_;

  uint8_t upper_buf_[PACKET_LEN_MAX];
  uint8_t lower_buf_[PACKET_LEN_MAX];

  unsigned upper_count_;
  unsigned lower_count_;

  queue_t send_queue_;
  queue_t receive_queue_;

  uint8_t receive_buf_[PACKET_LEN_MAX];


  void checkSend();
  void checkSerial(Stream &serial, Stream &another_serial, uint8_t *buf,
                    unsigned &count);

  bool queuePush(queue_t &queue, const uint8_t *data, unsigned len);
  unsigned queuePop(queue_t &queue, uint8_t *data);
  unsigned queueSize(queue_t &queue);

  inline void sendViaDebugSerial(const uint8_t* data, unsigned len) {
#ifdef BUS_DEBUG_SERIAL
    Serial.write(data, len);
#endif
  }
};

#endif

