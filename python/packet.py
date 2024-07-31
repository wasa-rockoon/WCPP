import struct
from enum import IntEnum
from typing import List, Optional

from crc import Calculator, Crc8


class Entry:

    def __init__(self, name: str):
        self.name = name
        self.type_ = 0b000000
        self.size = 0
        self.payload = bytes()
        self.sub_entries = []
        self.sub_packet = None

    def is_null(self) -> bool:
        return self.match_type(0b000000)

    def is_int(self) -> bool:
        return self.match_type(0b010000, 0b110000) or self.match_type(
            0b100000, 0b100000
        )

    def is_float(self) -> bool:
        return self.match_type(0b000100, 0b111100)

    def is_float16(self) -> bool:
        return self.match_type(0b000101)

    def is_float32(self) -> bool:
        return self.match_type(0b000110)

    def is_float64(self) -> bool:
        return self.match_type(0b000111)

    def is_bytes(self) -> bool:
        return self.match_type(0b000011) or self.match_type(0b001000, 0b111000)

    def is_struct(self) -> bool:
        return self.match_type(0b000001)

    def is_packet(self) -> bool:
        return self.match_type(0b000010)

    def bool(self) -> bool:
        return bool(self.int())

    def int(self) -> int:
        if self.match_type(0b010000, 0b110000):
            is_negative = self.type_ & 0b001000
            buf = bytearray(8)
            buf[0 : self.size] = self.payload
            value = struct.unpack("<Q", buf)[0]
            if is_negative:
                return -value
            else:
                return value
        if self.match_type(0b100000, 0b100000):
            return self.type_ & 0b011111

        return 0

    def float(self) -> float:
        if self.match_type(0b000100, 0b111111):
            return 0.0
        if self.is_float16():
            return struct.unpack("<e", self.payload)[0]
        if self.is_float32():
            return struct.unpack("<f", self.payload)[0]
        if self.is_float64():
            return struct.unpack("<d", self.payload)[0]
        if self.is_int():
            return float(self.int())

        return 0.0

    def bytes(self) -> bytes:
        if self.match_type(0b000011):
            return self.payload[1:]
        if self.match_type(0b001000, 0b111000):
            return self.payload
        return bytes()

    def string(self) -> str:
        return self.bytes().decode()

    def packet(self) -> Optional["Packet"]:
        return self.sub_packet

    def struct(self) -> List["Entry"]:
        return self.sub_entries

    def set_null(self) -> "Entry":
        self.type_ = 0b000000
        self.size = 0
        self.payload = bytes()
        return self

    def set_bool(self, value: bool) -> "Entry":
        self.set_int(int(value))
        return self

    def set_int(self, value: int) -> "Entry":
        value = int(value)
        if value >= 0 and value < 32:
            self.type_ = 0b100000 | value
            self.size = 0
            self.payload = bytes()
        else:
            self.payload = struct.pack("<Q", abs(value))
            self.size = 8
            while self.size > 0:
                if self.payload[self.size - 1] == 0:
                    self.size -= 1
                else:
                    break
            self.payload = self.payload[: self.size]
            if value > 0:
                self.type_ = 0b010000 | (self.size - 1)
            else:
                self.type_ = 0b011000 | (self.size - 1)

        return self

    def set_float16(self, value: float) -> "Entry":
        if value == 0.0:
            self.type_ = 0b000100
            self.size = 0
            self.payload = bytes()
        else:
            self.type_ = 0b000101
            self.size = 2
            self.payload = struct.pack("<e", value)
        return self

    def set_float32(self, value: float) -> "Entry":
        if value == 0.0:
            self.type_ = 0b000100
            self.size = 0
            self.payload = bytes()
        else:
            self.type_ = 0b000110
            self.size = 4
            self.payload = struct.pack("<f", value)
        return self

    def set_float64(self, value: float) -> "Entry":
        if value == 0.0:
            self.type_ = 0b000100
            self.size = 0
            self.payload = bytes()
        else:
            self.type_ = 0b000111
            self.size = 8
            self.payload = struct.pack("<d", value)
        return self

    def set_bytes(self, data: bytes) -> "Entry":
        if len(data) <= 7:
            self.type_ = 0b001000 | len(data)
            self.size = len(data)
            self.payload = data[:]
        else:
            self.type_ = 0b000011
            self.size = 1 + len(data)
            self.payload = bytes([len(data)]) + data[:]
        return self

    def set_string(self, data: str) -> "Entry":
        return self.set_bytes(data.encode())

    def set_struct(self, sub_entries: List["Entry"]) -> "Entry":
        self.type_ = 0b000001
        self.size = 1
        self.sub_entries = sub_entries
        return self

    def set_packet(self, sub_packet: "Packet") -> "Entry":
        self.type_ = 0b000010
        self.size = sub_packet.size
        self.sub_packet = sub_packet
        return self

    def decode(self, buf: bytes) -> int:
        self.type_ = (buf[0] >> 5) | ((buf[1] & 0b11100000) >> 2)

        self.name = chr((buf[0] & 0b00011111) + 64) + chr((buf[1] & 0b00011111) + 96)

        if self.type_ == 0b000000 or self.type_ == 0b000100 or self.type_ & 0b100000:
            self.size = 0
            # null, 0.0f, short int
        elif (self.type_ & 0b110000) == 0b010000:
            self.size = (self.type_ & 0b000111) + 1
            # int
        elif self.type_ >= 0b000101 and self.type_ <= 0b000111:
            self.size = 1 << (self.type_ & 0b000011)
            # float
        elif self.type_ == 0b000011:
            self.size = 1 + buf[2]
            # bytes
        elif self.type_ == 0b000001 or self.type_ == 0b000010:
            self.size = buf[2]
            # struct, packet
        elif (self.type_ & 0b111000) == 0b001000:
            self.size = self.type_ & 0b000111
            # short bytes
        else:
            raise ValueError("Invalid entry type")
        self.payload = buf[2 : 2 + self.size]

        if self.is_struct():
            self.sub_entries = []
            i = 1
            while i < self.size:
                entry = Entry("@@")
                i += entry.decode(self.payload[i:])
                self.sub_entries.append(entry)

        if self.is_packet():
            self.sub_packet = Packet.decode(self.payload[: self.size])

        return 2 + self.size

    def encode(self) -> bytes:
        if self.is_struct():
            self.payload = bytearray([0])
            self.size = 1
            for entry in self.sub_entries:
                entry_buf = entry.encode()
                self.payload += entry_buf
                self.size += len(entry_buf)
            self.payload[0] = self.size

        if self.is_packet():
            self.payload = self.sub_packet.encode()
            self.size = self.sub_packet.size

        buf = bytearray(2 + len(self.payload))
        buf[0] = ((self.type_ & 0b000111) << 5) | (ord(self.name[0].upper()) - 64)
        buf[1] = ((self.type_ & 0b111000) << 2) | (ord(self.name[1].upper()) - 64)
        buf[2:] = self.payload
        return buf

    def match_type(self, value, mask=0b111111):
        return (self.type_ & mask) == value

    def print(self, indent: str = ""):
        payload_str = ""
        type_str = ""
        if self.is_null():
            payload_str = "null"
            type_str = "null   "
        elif self.is_int():
            payload_str = str(self.int())
            type_str = "int    "
        elif self.is_float16():
            type_str = "float16"
            payload_str = str(self.float())
        elif self.is_float32():
            type_str = "float32"
            payload_str = str(self.float())
        elif self.is_float64():
            type_str = "float64"
            payload_str = str(self.float())
        elif self.is_bytes():
            type_str = "bytes  "
            payload_str = repr(self.string())
        elif self.is_packet():
            type_str = "packet "
        elif self.is_struct():
            type_str = "struct "

        print(indent + self.name + ": " + type_str + " = " + payload_str)

        if self.is_packet():
            self.packet().print(indent + "  ")
        elif self.is_struct():
            for entry in self.struct():
                entry.print(indent + "  ")


class PacketType(IntEnum):
    COMMAND = 0
    TELEMETRY = 1

    def __str__(self) -> str:
        if self == PacketType.COMMAND:
            return "cmd"
        else:
            return "tlm"


class Packet:
    """Packet"""

    size_max = 255
    entry_type_size = 2
    unit_id_local = 0x00
    component_id_self = 0x00
    packet_type_mask = 0b10000000
    packet_id_mask = 0b01111111

    def __init__(self):
        self.size = 0
        self.type_ = PacketType.COMMAND
        self.packet_id = 0
        self.component_id = 0
        self.origin_unit_id = 0
        self.dest_unit_id = 0
        self.sequence = 0
        self.entries: List[Entry] = []

        self.buf = None

    def is_command(self) -> bool:
        return self.type_ == PacketType.COMMAND

    def is_telemetry(self) -> bool:
        return self.type_ == PacketType.TELEMETRY

    def is_local(self) -> bool:
        return self.origin_unit_id == self.unit_id_local

    def is_remote(self) -> bool:
        return self.origin_unit_id != self.unit_id_local

    def checksum(self) -> int:
        if self.buf is None:
            return Calculator(Crc8.CCITT).checksum(self.encode())
        else:
            return Calculator(Crc8.CCITT).checksum(self.buf)

    def resize(self):
        pass

    def find(self, name: str) -> Optional[Entry]:
        for entry in self.entries:
            if entry.name.lower() == name.lower():
                return entry
        return None

    def print(self, indent: str = ""):
        if self.is_local():
            print(
                f"{indent}{self.type_}[{self.size}] {hex(self.packet_id)} by {hex(self.component_id)}, local"
            )
        else:
            print(
                f"{indent}{self.type_}[{self.size}] {hex(self.packet_id)} by {hex(self.component_id)}"
                + f", remote (from {hex(self.origin_unit_id)} to {hex(self.dest_unit_id)}), #{self.sequence}"
            )

        for entry in self.entries:
            entry.print(indent + "  ")

    def encode(self) -> bytes:
        buf = bytearray(255)
        buf[1] = (int(self.type_) * self.packet_type_mask) | (
            self.packet_id & self.packet_id_mask
        )
        buf[2] = self.component_id
        buf[3] = self.origin_unit_id
        if self.is_remote():
            buf[4] = self.dest_unit_id
            buf[5:7] = struct.pack("<H", self.sequence)
            i = 7
        else:
            i = 4
        for entry in self.entries:
            entry_buf = entry.encode()
            buf[i : i + len(entry_buf)] = entry_buf
            i += len(entry_buf)

        buf[0] = i

        return bytes(buf[:i])

    @classmethod
    def command(
        cls,
        packet_id: int = 0,
        component_id: int = 0,
        origin_unit_id: int = 0,
        dest_unit_id: int = 0,
        sequence: int = 0,
    ) -> "Packet":
        packet = cls()
        packet.type_ = PacketType.COMMAND
        packet.packet_id = packet_id
        packet.component_id = component_id
        packet.origin_unit_id = origin_unit_id
        packet.dest_unit_id = dest_unit_id
        packet.sequence = sequence
        return packet

    @classmethod
    def telemetry(
        cls,
        packet_id: int = 0,
        component_id: int = 0,
        origin_unit_id: int = 0,
        dest_unit_id: int = 0,
        sequence: int = 0,
    ) -> "Packet":
        packet = cls()
        packet.type_ = PacketType.TELEMETRY
        packet.packet_id = packet_id
        packet.component_id = component_id
        packet.origin_unit_id = origin_unit_id
        packet.dest_unit_id = dest_unit_id
        packet.sequence = sequence
        return packet

    @classmethod
    def decode(cls, buf: bytes) -> Optional["Packet"]:
        if buf[0] > len(buf):
            return None

        try:
            packet = cls()

            packet.size = buf[0]
            packet.type_ = PacketType((buf[1] & cls.packet_type_mask) >> 7)
            packet.packet_id = buf[1] & cls.packet_id_mask
            packet.component_id = buf[2]
            packet.origin_unit_id = buf[3]
            packet.dest_unit_id = buf[4] if packet.is_remote() else cls.unit_id_local
            packet.sequence = (
                struct.unpack("<H", buf[5:7])[0] if packet.is_remote() else 0
            )

            packet.buf = buf[: packet.size]

            i = 7 if packet.is_remote() else 4

            while i < packet.size:
                entry = Entry("@@")
                i += entry.decode(buf[i:])
                packet.entries.append(entry)

            return packet

        except Exception as e:
            raise e
            return None
