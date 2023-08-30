#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#include <iterator>
#include <string.h>

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
    inline void use() { buf_[1] &= 0x7F; }

  private:
    Section(uint8_t *buf): buf_(buf) {};

    void setSize(uint16_t size) {
      buf_[0] = size & 0xFF;
      buf_[1] = (size >> 8) & 0x7F;
    }

    uint8_t *buf_;

    friend class iterator;
    friend class SectionBuf;
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

  SectionBuf(): begin_(this, 0), end_(this, 0), lock_at_(-1) {}

  SectionBuf(uint8_t *buf, unsigned size)
    : buf_(buf), size_(size), begin_(this, 0), end_(this, 0), lock_at_(-1) {
    memset(buf, 0, size);
  };

  void init(uint8_t *buf, unsigned size) {
    buf_ = buf;
    size_ = size;
    memset(buf, 0, size);
  }

  inline iterator begin() {
    while ((*begin_).isFree() ||
           begin_.ptr_ + (*begin_).size() + sizeof(uint16_t) >= size_) {
      if (begin_.ptr_ == end_.ptr_)
        break;
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
      while (begin_.ptr_ > end_.ptr_) {
        if (begin_.ptr_ != lock_at_) {
          (*begin_).free();
          overflow_count_++;
        }
        begin_.step();
      }
      (*end_).setSize(size);
      end_.ptr_ = 0;
    }
    while (end_.ptr_ < begin_.ptr_ &&
           begin_.ptr_ <= end_.ptr_ + size + sizeof(uint16_t)) {
      if (begin_.ptr_ == lock_at_) {
        begin_.step();
        end_.ptr_ = begin_.ptr_;
      }
      else {
        (*begin_).free();
        begin_.step();
        overflow_count_++;
      }
    }
    (*end_).use();
    (*end_).setSize(size);
    iterator at = end_;
    end_.step();

    return at;
  }


  inline void lock(iterator at) { lock_at_ = at.ptr_; }
  inline void unlock() { lock_at_ = -1; }

  inline unsigned getOverflowCount() const { return overflow_count_; }

private:

  uint8_t *buf_;
  unsigned size_;

  iterator begin_;
  iterator end_;

  int lock_at_;

  unsigned overflow_count_;
};
