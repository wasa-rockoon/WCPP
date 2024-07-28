import pytest
from .packet import Packet, Entry

class TestPacket:
    def test_cpp_output(self):
        f = open('cpp/build/sample.bin', 'rb')
        data = f.read()

        i = 0
        while i < len(data):
          p = Packet.decode(data[i:])
          print(p)
          i += p.size + 2

        f.close()

    def test_basic_packet(self):

        sample_buf = bytes()

        for i in range(0, 4):
            if i == 0:
                p = Packet.command(ord('A'), 0x11)
            elif i == 1:
                p = Packet.command(ord('B'), 0x11, 0x22, 0x33, 12345)
            elif i == 2:
                p = Packet.telemetry(ord('C'), 0x11)
            else:
                p = Packet.telemetry(ord('D'), 0x11, 0x22, 0x33, 12345)

            sp = Packet.telemetry(ord('P'), 0x55)
            sp.entries = [
                Entry('Px').set_int(0xFF00FF00),
                Entry('Py').set_float32(1.4142),
            ]

            p.entries = [
                Entry('Nu').set_null(),
                Entry('Ix').set_int(1),
                Entry('Iy').set_int(1234567890),
                Entry('Iz').set_int(-1234567890),
                Entry('Fx').set_float16(1.25),
                Entry('Fy').set_float32(4.56),
                Entry('Fz').set_float64(7.89),
                Entry('Bx').set_bytes(b'ABC'),
                Entry('By').set_string('abcdefghijk'),
                Entry('St').set_struct([
                    Entry('Sx').set_int(54321),
                    Entry('Sy').set_float32(3.1415),
                ]),
                Entry('Sp').set_packet(sp),
            ]

            buf = p.encode()
            sample_buf += buf
            sample_buf += bytes([p.checksum(), 0])
            p = Packet.decode(buf)

            print(p)

            if i == 0:
                assert p.packet_id == ord('A')
                assert p.is_command()
                assert p.is_local()
            elif i == 1:
                assert p.packet_id == ord('B')
                assert p.is_command()
                assert p.is_remote()
                assert p.origin_unit_id == 0x22
                assert p.dest_unit_id == 0x33
                assert p.sequence == 12345
            elif i == 2:
                assert p.packet_id == ord('C')
                assert p.is_telemetry()
                assert p.is_local()
            else:
                assert p.packet_id == ord('D')
                assert p.is_telemetry()
                assert p.is_remote()
                assert p.origin_unit_id == 0x22
                assert p.dest_unit_id == 0x33
                assert p.sequence == 12345

            assert p.component_id == 0x11

            assert p.entries[0].name == 'Nu'
            assert p.entries[1].name == 'Ix'
            assert p.entries[1].int() == 1
            assert p.entries[2].name == 'Iy'
            assert p.entries[2].int() == 1234567890
            assert p.entries[3].name == 'Iz'
            assert p.entries[3].int() == -1234567890
            assert p.entries[4].name == 'Fx'
            assert pytest.approx(p.entries[4].float(), 1E-1) == 1.25
            assert p.entries[5].name == 'Fy'
            assert pytest.approx(p.entries[5].float(), 1E-6) == 4.56
            assert p.entries[6].name == 'Fz'
            assert pytest.approx(p.entries[6].float(), 1E-16) == 7.89
            assert p.entries[7].name == 'Bx'
            assert p.entries[7].bytes() == b'ABC'
            assert p.entries[8].name == 'By'
            assert p.entries[8].string() == 'abcdefghijk'


        f = open('cpp/build/sample.bin', 'rb')
        data = f.read()
        assert sample_buf == data
