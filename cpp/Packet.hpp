#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif


#define COMMAND 0
#define TELEMETRY 1

#define FROM_LOCAL 0

#define TO_LOCAL 0
#define BROADCAST 0b111

#define ENTRIES_MAX 32

union Payload {
	uint8_t bytes[4];
	uint32_t uint;
	int32_t int_;
	float float_;
};

const union Payload default_payload = { .uint = 0 };

truct Entry {
  uint8_t type;
  union Payload payload;

  void set(uint8_t type);
  void set(uint8_t type, const uint8_t* bytes);
  void set(uint8_t type,
           uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3);
  void set(uint8_t type, int32_t value);
  void set(uint8_t type, uint32_t value);
  void set(uint8_t type, float value);
  
  uint8_t encode(uint8_t* buf) const;
  uint8_t decode(const uint8_t* buf);
  
  void print() const;

  Entry& operator=(const Entry& entry);
};



struct Packet {
  typedef uint8_t Kind;

  Kind kind;
  uint8_t id;
  uint8_t from;
  uint8_t node;
  uint8_t dest;
  uint8_t size;
  uint8_t seq;

  Entry *entries;

  Packet();
  Packet(Kind kind, uint8_t id, uint8_t from, uint8_t dest, uint8_t size);

  float get(uint8_t type, uint8_t index, float default_) const;
	bool get(uint8_t type, uint8_t index = 0) const;
	bool get(uint8_t type, uint8_t index, union Payload& p) const;

	void addTimestamp(uint32_t time);

  inline unsigned encode(uint8_t *buf) const {
    return encode(buf, node, seq);
  };
  unsigned encode(uint8_t *buf, uint8_t node, uint8_t seq) const;
  bool decode(const uint8_t *buf, uint8_t len, uint8_t size_max);

  void print() const;

	Packet& operator=(const Packet& message);

};

template <uint8_t N> struct PacketN : public Packet {
  Entry entries_buf[N];

  PacketN() : Packet() {
    entries = entries_buf;
  }
  PacketN(Kind kind, uint8_t id, uint8_t from, uint8_t dest, uint8_t size = N):
    Packet(kind, id, from, dest, size > N ? N : size) {
    entries = entries_buf;
  }
};
