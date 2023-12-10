#include "float16.h"
#include "packet.h"
#include "heap.h"

#include <cassert>
#include <cstring>
#include <gtest/gtest.h>
#include <iostream>

TEST(PacketTest, BasicAssertions) {

  uint8_t buf[255];
  memset(buf, 0, 255);

  wccp::Packet p = wccp::Packet::empty<255>(buf);

  p.command('A', 0x11);
  p.append("Nu").setNull();
  p.append("Ix").setInt(1);
  p.append("Iy").setInt(1234567890);
  p.append("Iz").setInt(-1234567890);
  p.append("Fx").setFloat16(1.23);
  p.append("Fy").setFloat32(4.56);
  p.append("Fz").setFloat64(7.89);
  p.append("Bx").setBytes((const uint8_t*)"ABC", 3);
  p.append("By").setString("abcdefghijk");

  for (int i = 0; i < p.size(); i++)
    printf("%02X ", buf[i]);
  printf("\n");

  EXPECT_EQ(buf[p.size()], 0);

  EXPECT_EQ(p.packet_id(), 'A');
  EXPECT_TRUE(p.isCommand());
  EXPECT_FALSE(p.isTelemetry());
  EXPECT_EQ(p.component_id(), 0x11);
  EXPECT_EQ(p.unit_id(), 0);

  auto e = p.begin();
  EXPECT_TRUE((*e).name() == "Nu");
  ++e;
  EXPECT_TRUE((*e).name() == "Ix");
  EXPECT_EQ((*e).getUInt(), 1);
  ++e;
  EXPECT_TRUE((*e).name() == "Iy");
  EXPECT_EQ((*e).getUInt(), 1234567890);
  ++e;
  EXPECT_TRUE((*e).name() == "Iz");
  EXPECT_EQ((*e).getInt(), -1234567890);
  ++e;
  EXPECT_TRUE((*e).name() == "Fx");
  EXPECT_EQ((*e).getFloat16(), float16(1.23f));
  ++e;
  EXPECT_TRUE((*e).name() == "Fy");
  EXPECT_FLOAT_EQ((*e).getFloat32(), 4.56);
  ++e;
  EXPECT_TRUE((*e).name() == "Fz");
  EXPECT_FLOAT_EQ((*e).getFloat64(), 7.89);
  ++e;
  EXPECT_TRUE((*e).name() == "Bx");
  uint8_t bytes[32];
  memset(bytes, 0, 32);
  EXPECT_EQ((*e).getBytes(bytes), 3);
  EXPECT_STREQ((char*)bytes, "ABC");
  ++e;
  EXPECT_TRUE((*e).name() == "By");
  char str[32];
  memset(str, 0, 32);
  EXPECT_EQ((*e).getString(str), 11);
  EXPECT_STREQ(str, "abcdefghijk");
}

TEST(HeapTest, BasicAssertions) {
  uint8_t arena[4096];
  memset(arena, 0, sizeof(arena));

  wccp::Heap h(arena, sizeof(arena));

  h.dump();

  unsigned max_size = 256;

  uint8_t* ptr[max_size / 8];
  unsigned size[max_size / 8];
  unsigned seed[max_size / 8];
  for (unsigned i = 0; i < max_size / 8; i++) ptr[i] = nullptr;

  const unsigned N = 10000;
  unsigned rand = 11;

  for (unsigned n = 0; n < N; n++) {
    uint8_t i = (rand / 1000) % (max_size / 8);
    printf("N = %d\n", n);
    // printf("RAND %d %d\n", rand, i);
    if (ptr[i] == nullptr) {
      size[i] = rand % (max_size - 1) + 1;

      ptr[i] = (uint8_t *)h.alloc(size[i]);

      if (ptr[i] == nullptr) {
        printf("NOT ENOUGH SPACE %d\n", size[i]);
      }
      else {
        ASSERT_NE(ptr[i], nullptr);
        ASSERT_EQ(size[i], h.getSize(ptr[i]));
        seed[i] = rand;
        for (int k = 0; k < size[i]; k++)
          ptr[i][k] = (seed[i] + k * 97) % 256;
        printf("ALLOC %d %d %X\n", i, size[i], ptr[i]);
      }
    }
    else if (rand % 3 == 0) {
      printf("ADD REF %d %X\n", i, ptr[i]);

      ASSERT_EQ(size[i], h.getSize(ptr[i]));
      for (int k = 0; k < size[i]; k++)
        ASSERT_EQ(ptr[i][k], (seed[i] + k * 97) % 256);

      h.addRef(ptr[i]);
    }
    else {
      printf("FREE %d %X\n", i, ptr[i]);

      ASSERT_EQ(size[i], h.getSize(ptr[i]));
      for (int k = 0; k < size[i]; k++)
        ASSERT_EQ(ptr[i][k], (seed[i] + k * 97) % 256);

      if (h.releaseRef(ptr[i]) == 0)
        ptr[i] = nullptr;
    }

    // h.dump();

    rand = rand * 1664525 + 1013904223;

    // getchar();
  }
}
