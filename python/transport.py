from __future__ import annotations

from pathlib import Path

from .packet import Packet


def frame_packet(packet: Packet) -> bytes:
    return packet.encode() + bytes([packet.checksum(), 0])


def enqueue_packet(packet: Packet, command_path: str | Path = ".command") -> None:
    path = Path(command_path)
    with path.open(mode="ab") as f:
        f.write(frame_packet(packet))
        f.flush()