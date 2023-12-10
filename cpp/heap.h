// Best fit allocator

#pragma once

#ifdef ARDUINO
#include "Arduino.h"
#else
#include "stdint.h"
#endif

namespace wccp {

typedef uint8_t  data_size_t;
typedef uint16_t pointer_t;

const unsigned alloc_align = 8;
const unsigned alloc_size_min = 8;
const unsigned data_size_max = 256;
const unsigned bin_number = data_size_max / alloc_align + 1;
const uint8_t  ref_count_max = 63;
const pointer_t bin_ptr = 0xFFFF;
const pointer_t size_mask = 0xFFFD;
const pointer_t metadata_mask = 0x0003;
const pointer_t in_use_mask = 0x0001;
const pointer_t prev_in_use_mask = 0x0002;
const data_size_t allocated_chunk_header_size = 4;
const pointer_t prev_is_free = 0;
const pointer_t prev_is_allocated = 1;

class Heap {
public:
  Heap(uint8_t* arena, pointer_t arena_size);

  void* alloc(unsigned size);
  void free(void *ptr);
  uint8_t addRef(void* ptr);
  uint8_t releaseRef(void* ptr);
  unsigned getSize(void* ptr);

  inline unsigned failCount()  const { return fail_count_; }
  inline unsigned allocCount() const { return alloc_count_; }

  void dump() const;

private:

  union Chunk {
    struct {
      pointer_t prev_size;
      pointer_t size;
      pointer_t prev_free;
      pointer_t next_free;
    } free;
    struct {
      pointer_t   prev_size;
      uint8_t     ref_count;
      data_size_t data_size;
      uint8_t     data[];
    } allocated;

    inline void setIsInUse(bool in_use) {
      free.size = (free.size & ~in_use_mask) | (in_use ? in_use_mask : 0);
    }
    inline void setIsPrevInUse(bool in_use) {
      free.size = (free.size & ~prev_in_use_mask) | (in_use ? prev_in_use_mask : 0);
    }

    inline void setFreeSize(pointer_t size) {
      free.size = (free.size & metadata_mask) | size;
    }
    inline void setRefCount(uint8_t count) {
      allocated.ref_count = (allocated.ref_count & metadata_mask) | (count << 2);
    }
    inline bool isInUse() const {
      return (free.size & in_use_mask) != 0;
    }
    inline bool isPrevInUse() const {
      return (free.size & prev_in_use_mask) != 0;
    }
    inline pointer_t freeSize() { return free.size & size_mask; }
    inline uint8_t   refCount() { return allocated.ref_count >> 2; }
  };

  struct Bin {
    pointer_t prev_free;
    pointer_t next_free;
    uint8_t   skip_to;
  };

  pointer_t allocSize(pointer_t size) const;
  inline Chunk* getChunk(pointer_t ptr) const {
    if (ptr >= arena_size_) return nullptr;
    return reinterpret_cast<Chunk *>(arena_ + ptr);
  }
  inline Chunk* getChunk(void* ptr) const {
    return reinterpret_cast<Chunk *>((uint8_t*)ptr - allocated_chunk_header_size);
  }
  inline pointer_t getPtr(const Chunk* chunk) const {
    return reinterpret_cast<const uint8_t*>(chunk) - arena_;
  }

  int bestFitBin(pointer_t alloc_size);
  void addFreeChunk(Chunk* chunk);
  void removeFreeChunk(Chunk* chunk);

  uint8_t* arena_;
  pointer_t arena_size_;

  Bin bins_[bin_number + 1];  // bins_[0] for large bin

  pointer_t in_use_bytes_;
  unsigned  alloc_count_;
  unsigned  fail_count_;
};

} // namespace wccp
