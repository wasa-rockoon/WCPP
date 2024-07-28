#include "float16.h"
#include "packet.h"

#ifndef ARDUINO

#include <cassert>
#include <cstring>
#include <gtest/gtest.h>
#include <iostream>
#include <fstream>

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

    for (int i = 0; i < p.size(); i++)
      printf("%02X ", buf[i]);
    printf("\n");

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

