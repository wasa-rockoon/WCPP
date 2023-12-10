#include "heap.h"
#include <cassert>
#include <stdio.h>
#include <cstring>

namespace wccp {

Heap::Heap(uint8_t *arena, pointer_t arena_size)
  :arena_(arena) {
  arena_size_ = (arena_size / alloc_align) * alloc_align;
  in_use_bytes_ = 0;
  alloc_count_ = 0;
  fail_count_ = 0;

  Chunk* first_chunk = reinterpret_cast<Chunk*>(arena_);

  first_chunk->free.size = arena_size_;
  first_chunk->free.prev_size = 0;
  first_chunk->free.next_free = bin_ptr;
  first_chunk->free.prev_free = bin_ptr;

  bins_[0].next_free = 0;
  bins_[0].prev_free = 0;

  for (int i = 1; i <= bin_number; i++) {
    bins_[i].next_free = bin_ptr;
    bins_[i].prev_free = bin_ptr;
    bins_[i].skip_to = 0;
  }
}

int Heap::bestFitBin(pointer_t alloc_size) {
  int i = alloc_size / alloc_align;

  if (i > bin_number) {
    if (bins_[0].prev_free == bin_ptr) return -1;
    else return 0;
  }
  else {
    while (bins_[i].prev_free == bin_ptr) {
      if (i == 0) return -1;
      i = bins_[i].skip_to;
    }
    return i;
  }
}

void* Heap::alloc(unsigned size) {
  if (size == 0 || size > data_size_max) return nullptr;

  unsigned alloc_size = allocSize(size);

  // Search best fit
  int i = bestFitBin(alloc_size);
  if (i >= 0) {
    Bin &bin = bins_[i > bin_number ? 0 : i];

    pointer_t alloc_ptr = bin.prev_free;
    Chunk* alloc_chunk = getChunk(alloc_ptr);

    // printf("alloc %d %d %d\n", alloc_size, alloc_ptr, alloc_chunk->free.size);

    assert(!alloc_chunk->isInUse());
    assert(alloc_chunk->freeSize() >= alloc_size);

    removeFreeChunk(alloc_chunk);

    Chunk *next_chunk = getChunk(alloc_ptr + alloc_chunk->freeSize());

    // Split chunk larger than expected
    if (alloc_chunk->freeSize() > alloc_size) {
      Chunk* residue_chunk = getChunk(alloc_ptr + alloc_size);

      residue_chunk->free.prev_size = alloc_size;
      residue_chunk->setFreeSize(alloc_chunk->freeSize() - alloc_size);
      residue_chunk->setIsInUse(false);
      residue_chunk->setIsPrevInUse(true);

      addFreeChunk(residue_chunk);

      if (next_chunk) {
        next_chunk->free.prev_size = alloc_chunk->freeSize() - alloc_size;
        next_chunk->setIsPrevInUse(false);
      }
    }
    else {
      if (next_chunk)
        next_chunk->setIsPrevInUse(true);
    }


    alloc_chunk->allocated.data_size = size - 1;
    alloc_chunk->setIsInUse(true);
    alloc_chunk->setRefCount(1);

    // std::memset(alloc_chunk->allocated.data, 0, size);

    // printf("alloced %d\n", alloc);

    in_use_bytes_ += alloc_size;
    alloc_count_++;

    return reinterpret_cast<void *>(alloc_chunk->allocated.data);
  }
  else {
    fail_count_++;
    return nullptr;
  }
}

void Heap::free(void *ptr) {
  Chunk* free_chunk = getChunk(ptr);
  assert(free_chunk->isInUse());

  pointer_t free_ptr = getPtr(free_chunk);
  pointer_t free_size = allocSize((pointer_t)free_chunk->allocated.data_size + 1);

  // printf("free: %d\n", free_ptr);

  Chunk* next_chunk = getChunk(free_ptr + free_size);
  if (next_chunk) {
    assert(next_chunk->isPrevInUse());
    next_chunk->setIsPrevInUse(false);
  }

  Chunk* prev_chunk = nullptr;
  if (!free_chunk->isPrevInUse())
    prev_chunk = getChunk(free_ptr - free_chunk->allocated.prev_size);

  pointer_t combined_size = free_size;

  if (prev_chunk != nullptr && prev_chunk != free_chunk) {
    assert(!prev_chunk->isInUse());
    combined_size += prev_chunk->freeSize();
    removeFreeChunk(prev_chunk);

    free_chunk = prev_chunk;
  }
  if (next_chunk != nullptr && !next_chunk->isInUse()) {
    combined_size += next_chunk->freeSize();
    removeFreeChunk(next_chunk);

    next_chunk = getChunk(getPtr(next_chunk) + next_chunk->freeSize());
  }

  if (next_chunk != nullptr) {
    next_chunk->allocated.prev_size = combined_size;
    next_chunk->setIsPrevInUse(false);
  }


  free_chunk->setFreeSize(combined_size);
  free_chunk->setIsInUse(false);
  addFreeChunk(free_chunk);

  in_use_bytes_ -= free_size;
}

uint8_t Heap::addRef(void *ptr) {
  Chunk* chunk = getChunk(ptr);
  if (chunk->refCount() < ref_count_max)
    chunk->setRefCount(chunk->refCount() + 1);
  return chunk->refCount();
}

uint8_t Heap::releaseRef(void *ptr) {
  Chunk *chunk = getChunk(ptr);
  if (chunk->refCount() == 1) {
    free(ptr);
    return 0;
  }
  else {
    chunk->setRefCount(chunk->refCount() - 1);
    return chunk->refCount();
  }
}

unsigned Heap::getSize(void *ptr) {
  return (pointer_t)getChunk(ptr)->allocated.data_size + 1;
}

void Heap::addFreeChunk(Chunk* chunk) {
  pointer_t chunk_ptr = getPtr(chunk);

  unsigned i = chunk->freeSize() / alloc_align;
  if (i > bin_number) i = 0;
  Bin& bin = bins_[i];

  chunk->free.next_free = bin.next_free;
  chunk->free.prev_free = bin_ptr;
  if (bin.next_free != bin_ptr)
    getChunk(bin.next_free)->free.prev_free = chunk_ptr;
  bin.next_free = chunk_ptr;
  if (bin.prev_free == bin_ptr) bin.prev_free = chunk_ptr;

  for (int j = i - 1; j > 0; j--) {
    if (bins_[j].prev_free != bin_ptr) break;
    bins_[j].skip_to = i;
  }
}

void Heap::removeFreeChunk(Chunk* chunk) {
  pointer_t chunk_ptr = getPtr(chunk);

  unsigned i = chunk->freeSize() / alloc_align;
  Bin& bin = bins_[i > bin_number ? 0 : i];


  if (chunk->free.next_free == bin_ptr) bin.prev_free = chunk->free.prev_free;
  else getChunk(chunk->free.next_free)->free.prev_free = chunk->free.prev_free;

  if (chunk->free.prev_free == bin_ptr) bin.next_free = chunk->free.next_free;
  else getChunk(chunk->free.prev_free)->free.next_free = chunk->free.next_free;
}

pointer_t Heap::allocSize(pointer_t size) const {
  unsigned alloc_size = size + allocated_chunk_header_size - 2;
  if (alloc_size < alloc_size_min)
    alloc_size = alloc_size_min;
  if (alloc_size % alloc_align != 0)
    alloc_size += alloc_align - alloc_size % alloc_align;
  return alloc_size;
}

void Heap::dump() const {
  printf("chunks:");
  unsigned p = 0;
  while (p < arena_size_) {
    Chunk* chunk = getChunk(p);
    printf("\n  %04X: ", p);
    if (chunk->isInUse()) {
      printf("used %4d %2d", allocSize((pointer_t)chunk->allocated.data_size + 1),
             chunk->refCount());

      // for (int j = 0; j < chunk->allocated.data_size; j++)
      //   printf("%02X ", chunk->allocated.data[j]);

      assert(allocSize((pointer_t)chunk->allocated.data_size + 1) > 0);

      p += allocSize((pointer_t)chunk->allocated.data_size + 1);
    }
    else {
      printf("free %4d", chunk->freeSize());
      assert(chunk->freeSize() > 0);
      p += chunk->freeSize();
    }
  }

  printf("\nbins:");
  for (int i = 0; i < bin_number; i++) {
    printf("\n    %2d: %2d %4X %4X ",
           i, bins_[i].skip_to, bins_[i].prev_free, bins_[i].next_free);
    if (bins_[i].next_free == bin_ptr) continue;
    Chunk *chunk = getChunk(bins_[i].next_free);
    while (true) {
      printf("%4X ", chunk->free.next_free);
      if (chunk->free.next_free == bin_ptr) break;
      chunk = getChunk(chunk->free.next_free);
    };
  }

  printf("\nin use: %d / %d\n", in_use_bytes_, arena_size_);
  printf("failed: %d / %d\n", fail_count_, alloc_count_);

  printf("\narena:\n");
  for (int i = 0; i * 32 < arena_size_; i++) {
    printf("  %03X0: ", i * 2);
    for (int j = 0; j < 32; j++)
      printf("%02X ", arena_[i * 32 + j]);
    printf("\n");
  }

}

} // namespace wccp
