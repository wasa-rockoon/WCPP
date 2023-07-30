#include "Packet.hpp"

void Entry::set(uint8_t type) {
	this->type = type, payload.uint = 0;
}

void Entry::set(uint8_t type, const uint8_t* bytes) {
	this->type = type;
	for (int i = 0; i < 4; i++) payload.bytes[i] = bytes[i];
}
void Entry::set(uint8_t type, uint8_t byte0, uint8_t byte1, uint8_t byte2, uint8_t byte3) {
	this->type = type;
	payload.bytes[0] = byte0;
	payload.bytes[1] = byte1;
	payload.bytes[2] = byte2;
	payload.bytes[3] = byte3;
}

void Entry::set(uint8_t type, int32_t value) {
	this->type = type;
	payload.int_ = value;
}

void Entry::set(uint8_t type, uint32_t value) {
	this->type = type;
	payload.uint = value;
}

void Entry::set(uint8_t type, float value) {
	this->type = type;
	payload.float_ = value;
}

uint8_t Entry::encode(uint8_t* buf) const {
  uint8_t len = 0;
  uint8_t type_ = (type - 64) & 0b00111111;

  if (payload.uint & 0xFFFF0000) {
    len = 4;
    type_ |= 0b11000000;
  }
  else if (payload.uint & 0x0000FF00) {
    len = 2;
    type_ |= 0b10000000;
  }
  else if (payload.uint & 0x000000FF) {
    len = 1;
    type_ |= 0b01000000;
  }

  buf[0] = type_;

  for (int i = 0; i < len; i++) buf[1 + i] = payload.bytes[i];

  return 1 + len;
}

uint8_t Entry::decode(const uint8_t* buf) {
	type = (buf[0] & 0b00111111) + 64;

  uint8_t len;
  switch (buf[0] & 0b11000000) {
  case 0b01000000:
    len = 1;
    break;
  case 0b10000000:
    len = 2;
    break;
  case 0b11000000:
    len = 4;
    break;
  default:
    len = 0;
  }
  payload.uint = 0;

  for (int i = 0; i < len; i++) payload.bytes[i] = buf[1 + i];
  return 1 + len;
}

void Entry::print() const {
#ifdef ARDUINO
  Serial.printf("%c: %ld %f", type, payload.int_, payload.float_);
#endif
}

Entry& Entry::operator=(const Entry& entry) {
	type = entry.type;
	payload.uint = entry.payload.uint;
	return *this;
}


Packet::Packet(): id(0), from(0), size(0) {};
Packet::Packet(Kind kind, uint8_t id, uint8_t from, uint8_t dest, uint8_t size)
  : kind(kind), id(id), from(from), dest(dest), size(size){};

float Packet::get(uint8_t type, uint8_t index, float default_) const {
	Payload p = { .float_ = default_ };
	get(type, index, p);
	return p.float_;
}

bool Packet::get(uint8_t type, uint8_t index) const {
	Payload p;
	return get(type, index, p);
}

bool Packet::get(uint8_t type, uint8_t index, union Payload& p) const {
	for (int n = 0; n < size; n++) {
		if (entries[n].type == type) {
			if (index == 0) {
				p = entries[n].payload;
				return true;
			}
			else index--;
		}
	}
	return false;
}

void Packet::addTimestamp(uint32_t time) {
	entries[size].set('t', time);
	size++;
}

unsigned Packet::encode(uint8_t * buf, uint8_t node, uint8_t seq) const {
  buf[0] = ((kind & 0b1) << 7) | (id & 0b1111111);
  buf[1] = ((from & 0b111) << 5) | (node & 0b11111);
  buf[2] = ((dest & 0b111) << 5) | (size & 0b11111);
  buf[3] = seq;

  unsigned i = 4;
  for (uint8_t n = 0; n < size; n++) {
    i += entries[n].encode(buf + i);
  }

  return i;
}

bool Packet::decode(const uint8_t *buf, uint8_t len, uint8_t size_max) {
    kind = buf[0] >> 7;
    id = buf[0] & 0b1111111;
    from = buf[1] >> 5;
    node = buf[1] & 0b11111;
    dest = buf[2] >> 5;
    size = buf[2] & 0b11111;
    seq = buf[3];

    unsigned i = 4;

    for (uint8_t n = 0; n < size; n++) {
      if (i > len || n > size_max) {
        return false;
      }
      Entry entry;
      i += entry.decode(buf + i);
      entries[n] = entry;
    }
    return true;
  }

  void Packet::print() const {
#ifdef ARDUINO
    Serial.printf("%c (%c) [%d]\n", id, from, size);
    for (int n = 0; n < size; n++) {
      Serial.print("  ");
      entries[n].print();
      Serial.println();
    }
  #endif
}

Packet& Packet::operator=(const Packet& packet) {
  kind = packet.kind;
	id   = packet.id;
	from = packet.from;
  node = packet.node;
  dest = packet.dest;
	size = packet.size;
  seq  = packet.seq;

	for (int n = 0; n < size; n++) {
		entries[n] = packet.entries[n];
	}
	return *this;
}
