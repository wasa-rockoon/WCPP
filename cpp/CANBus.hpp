#pragma once

#include "Bus.hpp"
#include <iterator>
#include <string.h>


#ifndef CANBUS_BUFFER_SIZE
#define CANBUS_BUFFER_SIZE 128
#endif


template<typename T> class SectionBuf {
public:
  class iterator;

  class Section {
  public:
    inline uint16_t size() const {
      return ((uint16_t)(buf_[1] & 0x7F) << 8) | buf_[0];
    };
    inline bool isFree() const {
//      return !!(*reinterpret_cast<uint16_t *>(buf_) & 0x8000);
      return !!(buf_[1] & 0x80);
    };
    inline T &value() const {
      return *reinterpret_cast<T *>(buf_ + sizeof(uint16_t));
    };
    inline void free() { buf_[1] |= 0x80; }

  // private:
    Section(uint8_t *buf): buf_(buf) {};

    void setSize(uint16_t size) {
      buf_[0] = size & 0xFF;
      buf_[1] = (size & 0x7F00) >> 8;
    }

    uint8_t *buf_;

    friend iterator;
    friend SectionBuf;
  };

  class iterator: public std::iterator<std::forward_iterator_tag, Section> {
  public:
    iterator(const iterator& itr): buf_(itr.buf_), ptr_(itr.ptr_) {

    };

    inline Section operator*() const {
      return Section(buf_->buf_ + ptr_);
    }
    iterator &operator++() {
      while (*this != buf_->end_) {
        step();
        if (!(**this).isFree()) break;
      }

      return *this;
    }
    iterator operator++(int) {
      iterator result = *this;
      ++(*this);
      return result;
    }
    inline bool operator!=(const iterator& itr) {
      return this->ptr_ != itr.ptr_;
    }
    inline bool operator==(const iterator &itr) {
      return this->ptr_ == itr.ptr_;
    }

    iterator& operator=(const iterator& itr) {
      buf_ = itr->buf_;
      ptr_ = itr->ptr_;
    }

  // private:
    iterator(SectionBuf* buf, unsigned ptr): buf_(buf), ptr_(ptr) {};

    void step() {
      ptr_ += (**this).size() + sizeof(uint16_t);
      if (ptr_ >= buf_->size_) {
        ptr_ = 0;
      }
    }

    SectionBuf* buf_;
    unsigned ptr_;

    friend SectionBuf;
  };

  SectionBuf(uint8_t *buf, unsigned size)
    :buf_(buf), size_(size), begin_(this, 0), end_(this, 0) {
    memset(buf, 0, size);
  };

  inline iterator begin() {
    while ((*begin_).isFree() ||
           begin_.ptr_ + (*begin_).size() + sizeof(uint16_t) >= size_) {
      if (begin_.ptr_ == end_.ptr_) break;
      begin_.step();
    }
    return begin_;
  };
  inline iterator end() { return end_; };

  void pop() {
    if (begin() != end_)
      ++begin_;
  }
  iterator alloc(uint16_t size) {
    if (end_.ptr_ + size + sizeof(uint16_t) >= size_) {
      while (begin().ptr_ > end_.ptr_) {
        (*begin_).free();
        begin_.step();
        overflow_count_++;
      }
      (*end_).setSize(size);
      end_.ptr_ = 0;
    }
    while (end_.ptr_ < begin_.ptr_ &&
        begin_.ptr_ <= end_.ptr_ + sizeof(uint16_t)) {
      ++begin_;
      overflow_count_++;
    }
    (*end_).setSize(size);
    iterator at = end_;
    end_.step();

    return at;
  }

  inline unsigned getOverflowCount() const { return overflow_count_; }

// private:

  uint8_t* buf_;
  unsigned size_;

  iterator begin_;
  iterator end_;

  unsigned overflow_count_;
};


class CANBus: public Bus {
public:

  CANBus(uint8_t system, uint8_t node_name);

  bool begin() override;

  void update() override;

  bool send(Packet &packet) override;
  const Packet receive() override;

  bool availableForSend(const Packet &packet) override { return true; };

  inline unsigned getDroppedCount() const override {
    return dropped_count_ + buf_.getOverflowCount();
  }

  void listenAll() override {
    filter_.setAll();
    filterChanged();
  }
  void unlistenAll() override { filter_.clearAll(); filterChanged(); }
  void listen(Packet::Kind kind, uint8_t id) override {
    filter_.set((kind << 7) | id);
    filterChanged();
  }

protected:
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
