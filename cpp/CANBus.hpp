#pragma once

#include "Bus.hpp"
#include "Buffer.hpp"

#ifndef CANBUS_BUFFER_SIZE
#define CANBUS_BUFFER_SIZE 512
#endif

class CANBus: public Bus {
public:

  CANBus(uint8_t node_name, unsigned packet_margin = 0);

  bool begin() override;

  void update() override;

  bool send(Packet &packet) override;

  const Packet receive() override;
  const Packet receive_();

  bool availableForSend(const Packet &packet) override { return true; };

// protected:
  struct ReceivedData {
    uint8_t kind_id;
    uint8_t from;
    uint8_t frame_count;
    uint8_t frame_num;
    uint8_t data[];
  };


  void filterChanged() override;

  SectionBuf<ReceivedData> buf_;
  uint8_t buf_buf_[CANBUS_BUFFER_SIZE];

private:
  unsigned packet_margin_;

  Packet last_received_;

  uint8_t id_field(uint8_t id) const;

  void received(uint32_t ext_id, uint8_t* data, uint8_t dlc);
  void received_(uint32_t ext_id, uint8_t *data, uint8_t dlc);

  friend void CANReceived(uint32_t ext_id, uint8_t* data, uint8_t len);
};

bool CANInit();
bool CANSend(uint32_t ext_id, uint8_t *buf, unsigned len);
bool CANSetFilter(uint32_t id, uint32_t mask);
void CANReceived(uint32_t ext_id, uint8_t *data, uint8_t len);

extern void enableInterrupts();
extern void disableInterrupts();

