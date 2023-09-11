#!/usr/bin/env python3

import argparse
import datetime
import struct
import time

import matplotlib.pyplot as plt
import numpy as np
import serial
import websocket
from cobs import cobs
from wccp.packet import Packet


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('port', help='Serial port')
    parser.add_argument('-b', '--baud', help='Baudrate', type=int,
                        default=115200)
    packet_id = 'i'
    entry_type = 'M'

    args = parser.parse_args();

    plt.ion()
    fig = plt.figure(figsize = (8, 8))
    ax = fig.add_subplot(111, projection='3d')
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_zlabel("z")


    A = np.zeros((4, 4))
    b = np.zeros(4)

    # offset = np.array([0.2, 0.3, -0.1])

    xs = []
    ys = []
    zs = []
    # i = 0
    # while i < 10:
    #     i += 1
    #     n = 10
    #     xs = 2 * np.random.rand(n) - 1
    #     ys = 2 * np.random.rand(n) - 1
    #     zs = 2 * np.random.rand(n) - 1
    #     norm = np.linalg.norm([xs, ys, zs], axis=0)
    #     xs = xs / norm + offset[0]
    #     ys = ys / norm + offset[1]
    #     zs = zs / norm + offset[2]


    with serial.Serial(args.port, args.baud) as ser:
        print('connected port:', args.port, ', baud =', args.baud)

        buf = b''

        while True:
            try:
                buf += ser.read_all() or b''
            except:
                print('disconnected')
                break;

            if not buf:
                continue

            splitted = buf.split(b'\x00')
            if len(splitted) <= 1:
                continue

            buf = splitted[-1]

            xs = []
            ys = []
            zs = []

            for data in splitted[0:1]:
                if not data:
                    continue
                try:
                    decoded = cobs.decode(data)
                except:
                    continue

                if not decoded or decoded[0] == 0:
                    continue

                packet = Packet.decode(decoded)

                if not packet:
                    print('decode error')
                    continue

                if packet.id == packet_id:
                    # packet.print()

                    x = packet.find(entry_type, 0).payload.float32
                    y = packet.find(entry_type, 1).payload.float32
                    z = packet.find(entry_type, 2).payload.float32
                    print(x, y, z)

                    xs.append(x)
                    ys.append(y)
                    zs.append(z)
 
            xs = np.array(xs)
            ys = np.array(ys)
            zs = np.array(zs)
            n = len(xs)

            if n == 0:
                continue

            A += np.matrix([
                [np.sum(xs*xs), np.sum(xs*ys), np.sum(xs*zs), np.sum(-xs)],
                [np.sum(xs*ys), np.sum(ys*ys), np.sum(ys*zs), np.sum(-ys)],
                [np.sum(xs*zs), np.sum(ys*zs), np.sum(zs*zs), np.sum(-zs)],
                [np.sum(-xs),   np.sum(-ys),   np.sum(-zs),   n],
            ])
            b += np.array([
                np.sum(- xs*xs*xs - xs*ys*ys - xs*zs*zs),
                np.sum(- xs*xs*ys - ys*ys*ys - ys*zs*zs),
                np.sum(- xs*xs*zs - ys*ys*zs - zs*zs*zs),
                np.sum(xs*xs + ys*ys + zs*zs),
            ])

            abcr = np.linalg.solve(A, b)
            x0 = - abcr[0] / 2
            y0 = - abcr[1] / 2
            z0 = - abcr[2] / 2
            r  = np.sqrt(abcr[3] + x0**2 + y0**2 + z0**2)
            print('x0 =', x0, ', y0 =', y0, ', z0 =', z0, ', r =', r)

            ax.scatter(xs, ys, zs, c='blue')

            # plt.scatter(xs, ys, c='red', label='xy')
            # plt.scatter(ys, zs, c='blue', label='yz')
            # plt.scatter(xs, zs, c='green', label='xz')
            plt.draw()
            plt.pause(0.01)


if __name__ == "__main__":
    main()
