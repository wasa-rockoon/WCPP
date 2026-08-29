#!/usr/bin/env python3

import sys
import code
try:
    import readline
except ImportError:
    readline = None
import os
import atexit
from collections import defaultdict
import argparse
from datetime import datetime
import time
import serial
import serial.tools.list_ports
import code
from rich.live import Live
from rich.tree import Tree
from rich.console import Console
from rich.columns import Columns
from rich.text import Text
from rich.layout import Layout
from rich.panel import Panel
from wcpp import Packet, Entry
import wcpp
try:
    from .session_logger import SessionManager
except ImportError:
    from session_logger import SessionManager

if sys.platform == 'win32':
    import msvcrt

    def getkey():
        if not msvcrt.kbhit():
            return ''
        key = msvcrt.getwch()
        # Consume the scan code paired with Windows special keys. The current
        # TUI only uses printable one-character commands.
        if key in ('\x00', '\xe0'):
            if msvcrt.kbhit():
                msvcrt.getwch()
            return ''
        return key
else:
    import getchlib

    def getkey():
        return getchlib.getkey(False, echo=False)


refresh_per_second = 10


def main():
    args = parse_args()

    source = 'unknown'
    status = 'not opened'
    ser = None
    session = None
    command_file = None
    exit_reason = 'ERROR'
    all_packets = defaultdict(lambda: [defaultdict(lambda: [defaultdict(lambda: [[], None, -1]), None]), None])

    try:
        if args.repl:
            print('Starting REPL. Call send(packet: Packet) to send packet via wcpp-util.')

            with open('.command', mode='ab') as f:

                def send_command(packet: Packet):
                    f.write(packet.encode())
                    f.write(bytes([packet.checksum(), 0]))
                    f.flush()

                console = Console(
                    local={name: getattr(wcpp, name) for name in dir(wcpp)} | {'send': send_command}
                )
                console.interact()
            exit_reason = 'COMPLETED'
            return

        if args.file:
            source = args.file
            with open(args.file, mode='rb') as f:
                file_data = f.read()
                for packet in parse_packet(file_data)[1]:
                    add_packet(all_packets, packet)
                status = 'opened'
        else:
            ser = open_serial(args.port, args.baud)
            source = ser.name
            status = 'connected'
            session = SessionManager(
                log_dir=args.log_dir,
                source=source,
                baudrate=args.baud,
                output_path=args.out,
                flush_interval=args.flush_interval,
                fsync_interval=args.fsync_interval,
            )
            session.start()

        try:
            with open('.command', mode='xb') as f:
                pass
        except FileExistsError:
            pass

        command_file = open('.command', mode='rb')
        command_file.seek(0, 2)

        layout = init_layout(source, status)

        with Live(layout, refresh_per_second=refresh_per_second, transient=True, screen=True):
            last_refreshed = time.time()
            selection = [0, 0, 0]
            parse_buffer = b''
            command_data = b''

            while True:
                now = time.time()
                if now > last_refreshed + 1.0/refresh_per_second:
                    last_refreshed = now
                    layout['source'].update(Text(source_status(status, source, session)))

                    if selection == [0, 0, 0]:
                        selection = select_first(all_packets) or selection

                    layout['main']['list'].update(packet_tree(all_packets, selection))
                    panel = packet_view(all_packets, selection)
                    if panel:
                        layout['main']['packet'].update(panel)

                c: str = getkey()
                message = on_input(c, all_packets, selection, ser)
                if c == 's':
                    if session and session.force_flush():
                        message = f'raw log flushed ({session.bytes_written} bytes)'
                    elif session:
                        message = f'raw log flush failed: {session.last_error}'
                    else:
                        message = 'raw autosave is only active for serial input'
                if message:
                    layout['message'].update(Text(message))
                if message == 'quit':
                    exit_reason = 'COMPLETED'
                    time.sleep(0.5)
                    break

                layout['input'].update(Text(':' + c if c else ':'))

                command_data += command_file.read() or b''
                command_data, packets = parse_packet(command_data)
                for packet in packets:
                    add_packet(all_packets, packet, datetime.now())

                    if ser and ser.is_open:
                        ser.write(packet.encode())
                        ser.write(bytes([packet.checksum(), 0]))
                        ser.flush()
                        layout['message'].update(Text('sent packet'))

                if ser and ser.is_open:
                    try:
                        new_data = ser.read_all() or b''
                        if new_data:
                            if session:
                                session.write(new_data)
                            parse_buffer += new_data
                    except (serial.SerialException, OSError):
                        ser.close()

                    if not ser.is_open:
                        status = 'disconnected'
                    else:
                        parse_buffer, packets = parse_packet(parse_buffer)
                        for packet in packets:
                            add_packet(all_packets, packet, datetime.now())

                if session:
                    session.tick()

    except KeyboardInterrupt:
        if session:
            session.log_event('KEYBOARD_INTERRUPT')
        exit_reason = 'COMPLETED'
    finally:
        if command_file:
            command_file.close()
        if ser and ser.is_open:
            ser.close()
        if session:
            session.finalize(exit_reason)

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--port', help='Serial port path')
    parser.add_argument('-f', '--file', help='File path')
    parser.add_argument('-b', '--baud', help='Serial baudrate', type=int, default=115200)
    parser.add_argument('-o', '--out', help='Explicit raw autosave path (must not already exist)')
    parser.add_argument('--log-dir', default='logs', help='Autosave session directory root (default: ./logs)')
    parser.add_argument('--flush-interval', type=float, default=1.0,
                        help='Seconds between buffered file flushes (default: 1)')
    parser.add_argument('--fsync-interval', type=float, default=5.0,
                        help='Seconds between durable disk syncs (default: 5)')
    parser.add_argument('-q', '--quit', help='automatically close when data finished')
    parser.add_argument('-r', '--repl', action='store_true')

    return parser.parse_args()

def help_text() -> str:
    return '''
h: select previous id
l: select next id
k: previous packet
j: next packet
K: first packet
J: latest packet
s: flush raw autosave to disk
e: export selected as CSV
E: export all as CSV
C: clear all packets
q: quit
    '''

def init_layout(source: str, status: str) -> Layout:
    layout = Layout()
    layout.split_column(
        Layout(Text(status + ' ' + source), name='source', size=2),
        Layout(name='main'),
        Layout(Text(' '), name='message', size=1),
        Layout(Text(':'), name='input', size=1),
    )
    layout['main'].split_row(
        Layout(Panel(Text(''), title='Packet'), name='list', size=30),
        Layout(Panel(Text('')), name='packet'),
        Layout(Panel(Text(help_text(), justify="left"), title='Help'), size=30),
    )
    return layout

def source_status(status: str, source: str, session) -> str:
    if not session:
        return f'{status} {source}'
    text = f'{status} {source} | {session.status_label} | {session.bytes_written} bytes'
    if session.last_error:
        text += f' | {session.last_error}'
    else:
        text += f' | {session.temporary_raw_path}'
    return text

def packet_tree(all_packets, selection) -> Panel:
    tree = [Tree('unit')]
    tree[0].add('component').add('packet')

    now = time.time()
    times = []

    for unit_id, (unit_packets, unit_time) in all_packets.items():
        style = 'default'
        if selection[0] == unit_id:
            style = 'cyan'
        unit_tree = Tree(f'{hex(unit_id)}', style=style)
        tree.append(unit_tree)
        times.append(unit_time.time().strftime('%X') if unit_time else '')

        for component_id, (component_packets, component_time) in unit_packets.items():
            style = 'default'
            if selection[0] == unit_id and selection[1] == component_id:
                style = 'cyan'
            component_tree = unit_tree.add(f'{hex(component_id)}', style=style)
            times.append(component_time.time().strftime('%X') if component_time else '')

            for packet_id, (packets, packet_time, i) in component_packets.items():
                style = 'default'
                if selection[0] == unit_id and selection[1] == component_id and selection[2] == packet_id:
                    style = 'cyan'
                if packet_time and now < packet_time.timestamp() + 1.0/refresh_per_second:
                    style += ' reverse'
                component_tree.add(f'{hex(packet_id)} ({chr(packet_id)})', highlight=True, style=style)
                            
                times.append(packet_time.time().strftime('%X') if packet_time else '')

    tree_layout = Layout()
    tree_layout.split_row(Layout(Columns(tree)), Layout(Text('\n\n\n' + '\n'.join(times), style='blue'), size=8))
    return Panel(tree_layout, title='Packets')


def packet_view(all_packets, selection) -> Panel:
    if selection[0] in all_packets and \
        selection[1] in all_packets[selection[0]][0] and \
        selection[2] in all_packets[selection[0]][0][selection[1]][0]: 
        packets, t, i = all_packets[selection[0]][0][selection[1]][0][selection[2]]

        if packets:
            packet = packets[i]

            txt = f'{i + 1 if i >= 0 else len(packets)} / {len(packets)} total packets\n'
            txt += f''
            txt += f'type:         {"command" if packet.is_command() else "telemetry"}\n'
            txt += f'size:         {packet.size} bytes\n'
            txt += f'packet id:    {hex(packet.packet_id)} ({chr(packet.packet_id)})\n'
            txt += f'component id: {hex(packet.component_id)}\n'
            if packet.is_local():
                txt += f'local packet\n'
            else:
                txt += f'remote packet\n'
                txt += f'  origin id:  {hex(packet.origin_unit_id)}\n'
                txt += f'  dest id:    {hex(packet.dest_unit_id)}\n'
                txt += f'  sequence:   {packet.sequence}\n'
                        
            txt += f'entries:      {len(packet.entries)}\n'

            for entry in packet.entries:
                txt += entry.__str__('  ')

            if packet.is_local():
                title = f'component {hex(packet.component_id)}, packet {hex(packet.packet_id)} ({chr(packet.packet_id)})'
            else:
                title = f'unit {hex(packet.origin_unit_id)}, component {hex(packet.component_id)}, packet {hex(packet.packet_id)} ({chr(packet.packet_id)})'

            return Panel(Text(txt), title=title)

    return None

def on_input(c: str, all_packets, selection, ser) -> str:
    if c == 'q':
        return 'quit'

    if c == 'h' or c == 'l':
        tree_keys = []
        selection_i = -1
        for unit_id, (unit_packets, unit_time) in all_packets.items():
            for component_id, (component_packets, component_time) in unit_packets.items():
                for packet_id, (packets, packet_time, i) in component_packets.items():
                    if selection == [unit_id, component_id, packet_id]:
                        selection_i = len(tree_keys)
                    tree_keys.append([unit_id, component_id, packet_id])
        
        if not selection or selection_i < 0:
            if not tree_keys:
                selection[:] = [0, 0, 0]
            else:
                selection[:] = tree_keys[0]
        else:
            if c == 'l':
                new_selection_i = selection_i + 1
            if c == 'h':
                new_selection_i = selection_i - 1
            selection[:] = tree_keys[new_selection_i % len(tree_keys)]

        return f'selecting unit {hex(selection[0])}, component {hex(selection[1])}, packet {hex(selection[2])}'

    if c == 'j' or c == 'k' or c == 'J' or c == 'K':
        if selection[0] in all_packets and \
            selection[1] in all_packets[selection[0]][0] and \
            selection[2] in all_packets[selection[0]][0][selection[1]][0]: 
            packets, t, i = all_packets[selection[0]][0][selection[1]][0][selection[2]]
            new_i = i
            if c == 'j':
                if i >= 0 and i < len(packets) - 1:
                    new_i = i + 1
            if c == 'k':
                if i == -1:
                    new_i = len(packets) - 2
                elif i > 0:
                    new_i = i - 1
            if c == 'K':
                new_i = 0
            if c == 'J':
                new_i = -1
            all_packets[selection[0]][0][selection[1]][0][selection[2]][2] = new_i

            if new_i == -1:
                return f'showing latest packet'
            else:
                return f'showing {new_i}th packet'
        else:
            return f'select packet first'
        
    if c == 'C':
        all_packets.clear() 
        return f'cleared all packets'

    if c == 's':
        return None

    if c:
        return f'unknown command: {c}'

    return None


def parse_packet(buf: bytes) -> tuple[bytes, list[Packet]]:

    packets = []

    while len(buf) > 0 and len(buf) > buf[0] + 1:
        size = buf[0]

        if buf[size + 1] != 0:
            zero = buf.find(0)
            if zero < 0:
                buf = b''
            else:
                buf = buf[zero + 1:]
            continue


        packet = Packet.decode(buf[:size])
        checksum = buf[size]
        if packet and packet.checksum() == checksum:
            packets.append(packet)

        buf = buf[size + 1:]

    return (buf, packets)

def add_packet(all_packets, packet, time=None):
    all_packets[packet.origin_unit_id][1] = time
    all_packets[packet.origin_unit_id][0][packet.component_id][1] = time
    all_packets[packet.origin_unit_id][0][packet.component_id][0][packet.packet_id][0].append(packet)
    all_packets[packet.origin_unit_id][0][packet.component_id][0][packet.packet_id][1] = time

def select_first(all_packets):
    if all_packets:
        unit_packets = list(all_packets.values())[0][0]
        if unit_packets:
            component_packets = list(unit_packets.values())[0][0]
            if component_packets:
                packets = list(unit_packets.values())[0][0]
                if packets:
                    return [list(all_packets.keys())[0], 
                            list(unit_packets.keys())[0], 
                            list(component_packets.keys())[0]]


                
def open_serial(port: str = None, baud: int = 115200) -> serial.Serial:
    if port:
        ser = serial.Serial(port, baudrate=baud)
        if not ser.isOpen():
            raise ConnectionError('Failed to open serial port: ' + source)
        return ser
    else:
        ports = serial.tools.list_ports.comports(include_links=False)
        if not ports:
            raise FileNotFoundError('No serial port detected. Specify a port or file name manually.')
        
        source = ports[-1].device
        ser = serial.Serial(source, baudrate=baud)
        if not ser.isOpen():
            raise ConnectionError('Failed to open serial port: ' + source)
        return ser


class Console(code.InteractiveConsole):
    def __init__(self, local=None, filename="<console>",
                 histfile=os.path.expanduser("~/.console-history")):
        code.InteractiveConsole.__init__(self, local, filename)
        self.init_history(histfile)

    def init_history(self, histfile):
        if readline is not None:
            readline.parse_and_bind("tab: complete")
        if readline is not None and hasattr(readline, "read_history_file"):
            try:
                readline.read_history_file(histfile)
            except IOError:
                pass
            atexit.register(self.save_history, histfile)

    def save_history(self, histfile):
        if readline is not None:
            readline.write_history_file(histfile)
    

if __name__ == "__main__":
    main()
