#include "float16.h"
#include "packet.h"

#ifndef ARDUINO

#include <cassert>
#include <cstring>
#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <random>


wcpp::Packet generateRandomPacket(uint8_t* buf, std::mt19937& rand, uint8_t size_max);
bool appendRandomEntry_(wcpp::Entries& p, std::mt19937& rand);
void assertRandomPacket(const wcpp::Packet& p, std::mt19937& rand);
bool assertRandomEntry(wcpp::EntriesConstIterator e, std::mt19937& rand);

bool appendRandomEntry(wcpp::Entries& p, std::mt19937& rand) {
  char name[] = {(char)(rand()%32 + 64), (char)(rand()%32 + 96)};

  wcpp::Entry e = p.append(name);

  unsigned type = rand() % 13;
  // printf("NAME %c%c %d %d %d\n", name[0], name[1], p.size_remain(), p.size(), type);
  // if (p.size_remain() == 0) return false;

  switch (type) {
    case 0:
      return true;
    case 1:
      return e.setNull();
    case 2:
      return e.setInt(rand()%32);
    case 3:
      return e.setInt((long)rand()%512 - 256);
    case 4:
      return e.setInt((long)rand() - 0x80000000);
    case 5:
      return e.setInt((long)rand() * ((long)rand()-0x80000000));
    case 6:
      return e.setFloat16((float)rand()/(float)((long)rand()-0x80000000));
    case 7:
      return e.setFloat32((float)rand()/(float)((long)rand()-0x80000000));
    case 8: {
      return e.setFloat64((double)rand()/(double)((long)rand()-0x80000000));
    }
    case 9: {
      uint8_t bytes[64];
      int len = rand() % 64;
      for (int i = 0; i < len; i++) {
        bytes[i] = rand();
      }
      return e.setBytes(bytes, len);
    }
    case 10: {
      char str[65];
      int len = rand() % 64;
      for (int i = 0; i < len; i++) {
        str[i] = rand()%128;
      }
      str[len] = 0; 
      return e.setString(str);
    }
    case 11: {
      auto sub = e.setStruct();
      if (e.size() < 1) return false;
      while (rand() % 8) {
        if (sub.size_remain() < 2) return false;
        if (!appendRandomEntry(sub, rand)) return false;
      }
      return true;
    }
    case 12: {
      uint8_t sub_buf[255];
      memset(sub_buf, 0, 255);
      if (p.size_remain() < 7) return false;
      wcpp::Packet sp = generateRandomPacket(sub_buf, rand, p.size_remain());
      bool b = e.setPacket(sp); 
      return b ;
    }
  }
  return false;
}

bool assertRandomEntry(wcpp::EntriesConstIterator e, std::mt19937& rand) {
  char name[] = {(char)(rand()%32 + 64), (char)(rand()%32 + 96)};

  unsigned type = rand() % 13;
  // printf("ASSERT %c%c %c%c %d %d %d\n", name[0], name[1], (*e).name()[0], (*e).name()[1], (*e).size(), (*e).ptr_, type);
  EXPECT_TRUE((*e).name() == name);

  switch (type) {
    case 0:
    case 1:
      EXPECT_TRUE((*e).isNull());
      break;
    case 2: {
      int value = rand()%32;
      if ((*e).isNull()) return false;
      EXPECT_TRUE((*e).isInt());
      EXPECT_EQ((*e).getInt(), value);
      break;
    }
    case 3: {
      int value = rand()%512 - 256;
      if ((*e).isNull()) return false;
      EXPECT_TRUE((*e).isInt());
      EXPECT_EQ((*e).getInt(), value);
      break;
    }
    case 4: {
      long value = (long)rand()- 0x80000000;
      if ((*e).isNull()) return false;
      EXPECT_TRUE((*e).isInt());
      EXPECT_EQ((*e).getInt(), value);
      break;
    }
    case 5: {
      long value = (long)rand() * ((long)rand()-0x80000000);
      if ((*e).isNull()) return false;
      EXPECT_TRUE((*e).isInt());
      EXPECT_EQ((*e).getInt(), value);
      break;
    }
    case 6: {
      float16 value = float16((float)rand()/(float)((long)rand()-0x80000000));
      if ((*e).isNull()) return false;
      EXPECT_TRUE((*e).isFloat());
      EXPECT_TRUE((*e).isFloat16());
      EXPECT_EQ((*e).getFloat16(), value);
      break;
    }
    case 7: {
      float value = (float)rand()/(float)((long)rand()-0x80000000);
      if ((*e).isNull()) return false;
      EXPECT_TRUE((*e).isFloat());
      EXPECT_TRUE((*e).isFloat32());
      EXPECT_EQ((*e).getFloat32(), value);
      break;
    }
    case 8: {
      double value = (double)rand()/(double)((long)rand()-0x80000000);
      if ((*e).isNull()) return false;
      EXPECT_TRUE((*e).isFloat());
      EXPECT_TRUE((*e).isFloat64());
      EXPECT_EQ((*e).getFloat64(), value);
      break;
    }
    case 9: {
      uint8_t bytes[64];
      int len = rand() % 64;
      for (int i = 0; i < len; i++) {
        bytes[i] = rand();
      }
      if ((*e).isNull()) return false;
      EXPECT_TRUE((*e).isBytes());
      uint8_t b[64];
      EXPECT_EQ((*e).getBytes(b), len);
      EXPECT_TRUE(std::memcmp((char*)b, (char*)bytes, len) == 0);
      break;
    }
    case 10: {
      char str[65];
      int len = rand() % 64;
      for (int i = 0; i < len; i++) {
        str[i] = rand()%128;
      }
      if ((*e).isNull()) return false;
      EXPECT_TRUE((*e).isBytes());
      str[len] = 0; 
      char s[65];
      (*e).getString(s);
      // EXPECT_EQ((*e).getString(s), len);
      EXPECT_STREQ(s, str);
      break;
    }
    case 11: {
      if ((*e).size() == 0) return false;
      EXPECT_TRUE((*e).isStruct());
      const auto st = (*e).getStruct();
      if ((*e).size() < 1) return false;
      wcpp::EntriesConstIterator sub = st.begin();
      while (rand() % 8) {
        if (sub == st.end() && st.size_remain() < 2) return false;
        if (!assertRandomEntry(sub, rand)) return false;
        if (sub == st.end()) return false;
        ++sub;
      }
      break;
    }
    case 12: {
      if ((*e).size() == 0) return false;
      EXPECT_TRUE((*e).isPacket());
      wcpp::Packet sp = (*e).getPacket();
      assertRandomPacket(sp, rand);
      break;
    }
  }
  return true;
}

wcpp::Packet generateRandomPacket(uint8_t* buf, std::mt19937& rand, uint8_t size_max=255) {
  wcpp::Packet p = wcpp::Packet::empty(buf, size_max);
  switch (rand()%4) {
  case 0:
    p.command(rand()%128, rand()%256);
    break;
  case 1:
    p.command(rand()%128, rand()%256, rand()%255+1, rand()%255+1, rand()%65536);
    break;
  case 2:
    p.telemetry(rand()%128, rand()%256);
    break;
  case 3:
    p.telemetry(rand()%128, rand()%256, rand()%255+1, rand()%255+1, rand()%65536);
    break;
  }
  while (rand() % 8) {
    if (p.size_remain() < 2) break;
    if (!appendRandomEntry(p, rand)) break;
  }

  return p;
}

void assertRandomPacket(const wcpp::Packet& p, std::mt19937& rand) {
  switch (rand()%4) {
  case 0:
    EXPECT_EQ(p.packet_id(), rand()%128);
    EXPECT_TRUE(p.isCommand());
    EXPECT_FALSE(p.isTelemetry());
    EXPECT_EQ(p.component_id(), rand()%256);
    EXPECT_TRUE(p.isLocal());
    EXPECT_FALSE(p.isRemote());
    break;
  case 1:
    EXPECT_EQ(p.packet_id(), rand()%128);
    EXPECT_TRUE(p.isCommand());
    EXPECT_FALSE(p.isTelemetry());
    EXPECT_FALSE(p.isLocal());
    EXPECT_TRUE(p.isRemote());
    EXPECT_EQ(p.component_id(), rand()%256);
    EXPECT_EQ(p.origin_unit_id(), rand()%255+1);
    EXPECT_EQ(p.dest_unit_id(), rand()%255+1);
    EXPECT_EQ(p.sequence(), rand()%65536);
    break;
  case 2:
    EXPECT_EQ(p.packet_id(), rand()%128);
    EXPECT_FALSE(p.isCommand());
    EXPECT_TRUE(p.isTelemetry());
    EXPECT_EQ(p.component_id(), rand()%256);
    EXPECT_TRUE(p.isLocal());
    EXPECT_FALSE(p.isRemote());
    break;
  case 3:
    EXPECT_EQ(p.packet_id(), rand()%128);
    EXPECT_FALSE(p.isCommand());
    EXPECT_TRUE(p.isTelemetry());
    EXPECT_FALSE(p.isLocal());
    EXPECT_TRUE(p.isRemote());
    EXPECT_EQ(p.component_id(), rand()%256);
    EXPECT_EQ(p.origin_unit_id(), rand()%255+1);
    EXPECT_EQ(p.dest_unit_id(), rand()%255+1);
    EXPECT_EQ(p.sequence(), rand()%65536);
    break;
  }

  auto e = p.begin();
  while (rand() % 8) {
    if (e == p.end() && p.size_remain() < 2) break;
    if (!assertRandomEntry(e, rand)) break;
    if (e == p.end()) break;
    ++e;
  }
}

TEST(EncodeDecodeTest, BasicAssertions) {
  std::random_device seed_gen;
  unsigned seed = seed_gen();
  // seed = -1524307131;
  printf("SEED: %d\n", seed);
  std::mt19937 rand_encode(seed);
  std::mt19937 rand_decode(seed);
  uint8_t buf[256];
  memset(buf, 0, 255);

  buf[255] = 0;
  wcpp::Packet p = generateRandomPacket(buf, rand_encode);
  EXPECT_EQ(buf[255], 0);

  printf("packet: %d\n", p.size());

  std::ofstream fout;
  fout.open("data.bin", std::ios::out|std::ios::binary|std::ios::trunc);
  fout.write(reinterpret_cast<const char *>(p.encode()), p.size());
  fout << p.checksum();
  fout << '\0';
  fout.close();

  for (int i = 0; i < p.size(); i++)
    printf("%02X ", buf[i]);
  printf("\n");

  assertRandomPacket(p, rand_decode);
}

TEST(PacketTest, BasicAssertions) {

  uint8_t buf[255];
  memset(buf, 0, 255);

  wcpp::Packet p = wcpp::Packet::empty(buf, 255);

  std::ofstream fout;
  fout.open("sample.bin", std::ios::out|std::ios::binary|std::ios::trunc);
  EXPECT_TRUE(fout);

  for (int i = 0; i < 4; i++) {
    switch (i) {
    case 0:
      p.command('A', 0x11);
      break;
    case 1:
      p.command('B', 0x11, 0x22, 0x33, 12345);
      break;
    case 2:
      p.telemetry('C', 0x11);
      break;
    case 3:
      p.telemetry('D', 0x11, 0x22, 0x33, 12345);
      break;
    }

    p.append("Nu").setNull();
    p.append("Ix").setInt(1);
    p.append("Iy").setInt(1234567890);
    p.append("Iz").setInt(-1234567890);
    p.append("Fx").setFloat16(1.25);
    p.append("Fy").setFloat32(4.56);
    p.append("Fz").setFloat64(7.89);
    p.append("Bx").setBytes((const uint8_t*)"ABC", 3);
    p.append("By").setString("abcdefghijk");
    auto sub = p.append("St").setStruct();
    sub.append("Sx").setInt(54321);
    sub.append("Sy").setFloat32(3.1415);

    uint8_t sub_buf[255];
    memset(sub_buf, 0, 255);
    wcpp::Packet sp = wcpp::Packet::empty(sub_buf, 255);
    sp.telemetry('P', 0x55);
    sp.append("Px").setInt(0xFF00FF00);
    sp.append("Py").setFloat32(1.4142);

    p.append("Sp").setPacket(sp);

    // for (int i = 0; i < p.size(); i++)
    //   printf("%02X ", buf[i]);
    // printf("\n");

    fout.write(reinterpret_cast<const char *>(p.encode()), p.size());
    fout << p.checksum();
    fout << '\0';

    switch (i) {
    case 0:
      EXPECT_EQ(p.packet_id(), 'A');
      EXPECT_TRUE(p.isCommand());
      EXPECT_FALSE(p.isTelemetry());
      EXPECT_TRUE(p.isLocal());
      EXPECT_FALSE(p.isRemote());
      break;
    case 1:
      EXPECT_EQ(p.packet_id(), 'B');
      EXPECT_TRUE(p.isCommand());
      EXPECT_FALSE(p.isTelemetry());
      EXPECT_FALSE(p.isLocal());
      EXPECT_TRUE(p.isRemote());
      EXPECT_EQ(p.origin_unit_id(), 0x22);
      EXPECT_EQ(p.dest_unit_id(), 0x33);
      EXPECT_EQ(p.sequence(), 12345);
      break;
    case 2:
      EXPECT_EQ(p.packet_id(), 'C');
      EXPECT_FALSE(p.isCommand());
      EXPECT_TRUE(p.isTelemetry());
      EXPECT_TRUE(p.isLocal());
      EXPECT_FALSE(p.isRemote());
      break;
    case 3:
      EXPECT_EQ(p.packet_id(), 'D');
      EXPECT_FALSE(p.isCommand());
      EXPECT_TRUE(p.isTelemetry());
      EXPECT_FALSE(p.isLocal());
      EXPECT_TRUE(p.isRemote());
      EXPECT_EQ(p.origin_unit_id(), 0x22);
      EXPECT_EQ(p.dest_unit_id(), 0x33);
      EXPECT_EQ(p.sequence(), 12345);
      break;
    }

    EXPECT_EQ(p.component_id(), 0x11);

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
    EXPECT_EQ((*e).getFloat16(), float16(1.25f));
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
    ++e;
    EXPECT_TRUE((*e).name() == "St");
    auto st = (*e).getStruct();
    auto s = st.begin();
    EXPECT_TRUE((*s).name() == "Sx");
    EXPECT_EQ((*s).getUInt(), 54321);
    ++s;
    EXPECT_TRUE((*s).name() == "Sy");
    EXPECT_FLOAT_EQ((*s).getFloat32(), 3.1415);
    // printf("NAME %c%c\n", (*s).name()[0], (*s).name()[1]);
    ++s;
    EXPECT_TRUE((*s).name() == "Sp");
    const wcpp::Packet sp_ = (*s).getPacket();
    EXPECT_EQ(sp_.packet_id(), 'P');
    EXPECT_TRUE(sp_.isTelemetry());
    EXPECT_TRUE(sp_.isLocal());
    EXPECT_EQ(sp_.component_id(), 0x55);
    auto a = sp_.begin();
    EXPECT_TRUE((*a).name() == "Px");
    EXPECT_EQ((*a).getUInt(), 0xFF00FF00);
    ++a;
    EXPECT_TRUE((*a).name() == "Py");
    EXPECT_FLOAT_EQ((*a).getFloat32(), 1.4142);
  }

  fout.close();
}

#endif

