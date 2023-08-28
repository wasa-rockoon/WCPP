#pragma once

#include "Bus.hpp"
#include "Buffer.hpp"

#ifndef CANBUS_BUFFER_SIZE
#define CANBUS_BUFFER_SIZE 512
#endif

class CANBus: public Bus {
public:

  CANBus(uint8_t node_name);

  bool begin() override;

  void update() override;

  bool send(Packet &packet) override;

  const Packet receive() override;

  bool availableForSend(const Packet &packet) override { return true; };

  void listenAll() override {
    filter_.setAll();
    filterChanged();
  }
  void unlistenAll() override { filter_.clearAll(); filterChanged(); }
  void listen(Packet::Kind kind, uint8_t id) override {
    filter_.set((kind << 7) | id);
    filterChanged();
  }

// protected:
  struct ReceivedData {
    uint8_t kind_id;
    uint8_t from;
    uint8_t frame_count;
    uint8_t frame_num;
    uint8_t data[];
  };


  void filterChanged();

  SectionBuf<ReceivedData> buf_;
  uint8_t buf_buf_[CANBUS_BUFFER_SIZE];

private:
  Packet last_received_;

  uint8_t id_field(uint8_t id) const;

  void received(uint32_t ext_id, uint8_t* data, uint8_t dlc);

  friend void CANReceived(uint32_t ext_id, uint8_t* data, uint8_t len);
};

bool CANInit();
bool CANSend(uint32_t ext_id, uint8_t *buf, unsigned len);
bool CANSetFilter(uint32_t id, uint32_t mask);
void CANReceived(uint32_t ext_id, uint8_t *data, uint8_t len);
