#!/usr/bin/env python3

import argparse
import datetime
import struct
import time

import serial
import websocket
from cobs import cobs
from wccp.packet import Packet


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('path', help='Log file path')
    parser.add_argument('-f', '--filter', help='Packet ids to show', default='')

    args = parser.parse_args();
    print(args)

    source = 'log'

    with open(args.path, "rb") as f:
        while True:

            header = f.read(5)
            if not header:
                break
            millis = struct.unpack('<I', header[:4])[0]
            len_ = header[4]
            buf = f.read(len_)

            if not millis or not len_ or not buf:
                break;

            packet = Packet.decode(buf)

            if not packet:
                print('decode error')
                continue

            if (not args.filter or args.filter.find(packet.id) != -1):
                print(millis, 'ms:')
                packet.print()


if __name__ == "__main__":
    main()
