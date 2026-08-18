#!/usr/bin/env python3
# パケットをPCから送信できるようにする
__version__ = "1.2.0"  # 2025-06-25 - CSV export improvements, UT timestamp and component grouping format

import sys
import code
try:
    import readline
except ImportError:
    # readline is not available on Windows
    readline = None
import os
import atexit
from collections import defaultdict
import argparse
from datetime import datetime, timedelta
import time
import serial
import serial.tools.list_ports
import getchlib
import code
import csv
import os
from rich.live import Live
from rich.tree import Tree
from rich.console import Console
from rich.columns import Columns
from rich.text import Text
from rich.layout import Layout
from rich.panel import Panel
from wcpp import Packet, Entry, frame_packet
import wcpp

#キーボード入力のためのモジュールを条件分岐でインポート
if sys.platform == 'win32':
    import msvcrt
    def getkey():
        try:
            if msvcrt.kbhit():
                return msvcrt.getch().decode('utf-8')
            else:
                return ''
        except (UnicodeDecodeError, OSError, KeyboardInterrupt):
            return ''
else:
    import getchlib
    def getkey():
        try:
            return getchlib.getkey(False, echo=False)
        except (OSError, KeyboardInterrupt):
            return ''

refresh_per_second = 10


def main():
    args = parse_args()

    source = 'unknown'
    status = 'not opened'
    ser = None

    raw_data = bytearray([])
    all_packets = defaultdict(lambda: [defaultdict(lambda: [defaultdict(lambda: [[], None, -1]), None]), None])

    if args.repl:
        print('Starting REPL. Call send(packet: Packet) to send packet via wcpp-util.')
            
        with open('.command', mode='ab') as f:

            def send_command(packet: Packet):
                f.write(frame_packet(packet))
                f.flush()

            console = Console(
                local={name: getattr(wcpp, name) for name in dir(wcpp)} | {'send': send_command}
            )
            console.interact()
        return

    if args.file:
        source = args.file
        with open(args.file, mode='rb') as f:
            data = f.read()
            packets = parse_packet(data)[1]
            # ファイルから読み込まれたパケットに対して、順序に基づいてタイムスタンプを生成
            base_time = datetime.now()
            for i, packet in enumerate(packets):
                # 各パケットに一意のタイムスタンプを与える（マイクロ秒単位で差を付ける）
                packet_time = base_time + timedelta(microseconds=i)
                add_packet(all_packets, packet, time=packet_time, update_csv=args.csv, flatten_structs=not args.no_flatten, csv_path=args.csv_path)
            status = 'opened'
    else:
        ser = open_serial(args.port, args.baud)
        source = ser.name
        status = 'connected'

    try:
        with open('.command', mode='xb') as f:
            pass
    except:
        pass

    command_file = open('.command', mode='rb')
    command_file.seek(0, 2)

    layout = init_layout(source, status)

    with Live(layout, refresh_per_second=refresh_per_second, transient=True, screen=True) as live:
        last_refreshed = time.time()

        selection = [0, 0, 0]

        data = b''
        command_data = b''

        while True:

            # Refreh UI
            now = time.time()
            if now > last_refreshed + 1.0/refresh_per_second:
                last_refreshed = now

                if selection == [0, 0, 0]:
                    selection = select_first(all_packets) or selection

                layout['main']['list'].update(packet_tree(all_packets, selection))

                panel = packet_view(all_packets, selection)
                if panel:
                    layout['main']['packet'].update(panel)

            # Input
            try:
                c: str = getkey()
                message = on_input(c, all_packets, selection, ser, args)
            except Exception as e:
                # キー入力処理でエラーが発生した場合の安全な処理
                c = ''
                message = f'Input error: {str(e)}'
            if c == 's':
                with open(args.out, mode='wb') as f:
                    f.write(raw_data)
            if message:
                layout['message'].update(Text(message))
            if message == 'quit':
                time.sleep(0.5)
                break                

            if c:
                layout['input'].update(Text(':' + c))
                # live.refresh()
            else:
                layout['input'].update(Text(':'))

            # Read command
            command_data = command_file.read() or b''
            command_data, packets = parse_packet(command_data)
            for packet in packets:
                add_packet(all_packets, packet, datetime.now(), update_csv=args.csv, 
                          flatten_structs=not args.no_flatten, csv_path=args.csv_path)

                if ser and ser.isOpen():
                    ser.write(frame_packet(packet))
                    ser.flush()

                    layout['message'].update(Text('sent packet'))

            # Read serial
            if ser and ser.isOpen():
                try:
                    data += ser.read_all() or b''
                    raw_data.extend(data)
                except:
                    ser.close()

                if not ser.isOpen():
                    status = 'disconnected'
                    layout['source'].update(Text(status + ' ' + source)),
                    continue

                data, packets = parse_packet(data)
                for packet in packets:
                    add_packet(all_packets, packet, datetime.now(), update_csv=args.csv, 
                              flatten_structs=not args.no_flatten, csv_path=args.csv_path)






def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--port', help='Serial port path')
    parser.add_argument('-f', '--file', help='File path')
    parser.add_argument('-b', '--baud', help='Serial baudrate', type=int, default=115200)
    parser.add_argument('-o', '--out', help='output file', default='data.bin')
    parser.add_argument('-q', '--quit', help='automatically close when data finished')
    parser.add_argument('-r', '--repl', action='store_true')
    parser.add_argument('-c', '--csv', action='store_true', help='Auto-update CSV file with packet data')
    parser.add_argument('-n', '--no-flatten', action='store_true', help='Do not flatten structured entries in CSV')
    parser.add_argument('--csv-path', help='Specify custom path for CSV output files')

    return parser.parse_args()

def help_text() -> str:
    return '''
h: select previous id
l: select next id
k: previous packet
j: next packet
K: first packet
J: latest packet
s: save raw data
e: export selected packet as CSV
E: export all packets as CSV
C: clear all packets and CSV data
q: quit

Command Line Options:
--csv-path PATH: Specify custom CSV output path
--no-flatten: Do not flatten structures in CSV
-c, --csv: Auto-update CSV while receiving
    '''

def init_layout(source: str, status: str) -> Layout:
    layout = Layout()
    layout.split_column(
        Layout(Text(status + ' ' + source), name='source', size=1),
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
                txt += format_entry_for_view(entry, '  ')

            if packet.is_local():
                title = f'component {hex(packet.component_id)}, packet {hex(packet.packet_id)} ({chr(packet.packet_id)})'
            else:
                title = f'unit {hex(packet.origin_unit_id)}, component {hex(packet.component_id)}, packet {hex(packet.packet_id)} ({chr(packet.packet_id)})'

            return Panel(Text(txt), title=title)

    return None


def format_entry_for_view(entry: Entry, indent: str = '') -> str:
    payload_str = ''
    type_str = ''
    if entry.is_null():
        payload_str = "null"
        type_str = "null   "
    elif entry.is_int():
        payload_str = str(entry.int())
        type_str = "int    "
    elif entry.is_float16():
        type_str = "float16"
        payload_str = str(entry.float())
    elif entry.is_float32():
        type_str = "float32"
        payload_str = str(entry.float())
    elif entry.is_float64():
        type_str = "float64"
        payload_str = str(entry.float())
    elif entry.is_float():
        type_str = "float  "
        payload_str = str(entry.float())
    elif entry.is_bytes():
        type_str = "bytes  "
        payload_str = repr(entry.string())
    elif entry.is_packet():
        type_str = "packet "
    elif entry.is_struct():
        type_str = "struct "
    else:
        type_str = "unknown"

    s = indent + entry.name + ': ' + type_str + ' = ' + payload_str + '\n'

    if entry.is_packet() and entry.packet():
        for sub_entry in entry.packet().entries:
            s += format_entry_for_view(sub_entry, indent + '  ')
    elif entry.is_struct():
        for sub_entry in entry.struct():
            s += format_entry_for_view(sub_entry, indent + '  ')

    return s

def get_entry_value(entry: Entry, flatten=False, prefix='') -> dict:
    """
    エントリーの値を取得する。flatten=Trueの場合、構造体やパケットを平坦化する。
    
    Args:
        entry: 値を取得するエントリ
        flatten: 構造体やパケットを平坦化するか
        prefix: 平坦化する場合のキー接頭辞
    
    Returns:
        flatten=Falseの場合は文字列、flatten=Trueの場合は辞書 {キー: 値}
    """
    if not flatten:
        # 従来通りの文字列を返す処理
        if entry.is_null():
            return "null"
        elif entry.is_int():
            return str(entry.int())
        elif entry.is_float16() or entry.is_float32() or entry.is_float64() or entry.is_float():
            return str(entry.float())
        elif entry.is_bytes():
            # GPS時刻（UT）フィールドの特別処理
            string_value = entry.string()
            if entry.name == "Ut" and ":" in string_value:
                # GPS時刻の場合、秒数を含む完全なフォーマットを保持
                return string_value
            else:
                return string_value
        elif entry.is_struct():
            return "struct(" + ", ".join([get_entry_value(e) for e in entry.struct()]) + ")"
        elif entry.is_packet():
            return "packet"
        else:
            return "unknown"
    else:
        # 平坦化する処理
        result = {}
        key = prefix + entry.name if prefix else entry.name
        
        if entry.is_null():
            result[key] = "null"
        elif entry.is_int():
            result[key] = str(entry.int())
        elif entry.is_float16() or entry.is_float32() or entry.is_float64() or entry.is_float():
            result[key] = str(entry.float())
        elif entry.is_bytes():
            # GPS時刻（UT）フィールドの特別処理
            string_value = entry.string()
            if entry.name == "Ut" and ":" in string_value:
                # GPS時刻の場合、秒数を含む完全なフォーマットを保持
                result[key] = string_value
            else:
                result[key] = string_value
        elif entry.is_struct():
            # 構造体の各要素を個別の列に展開
            for sub_entry in entry.struct():
                # サブエントリに対して再帰的に処理し、結果をマージ
                sub_prefix = f"{key}."
                sub_result = get_entry_value(sub_entry, flatten=True, prefix=sub_prefix)
                result.update(sub_result)
        elif entry.is_packet():
            # パケットのエントリを個別の列に展開
            if entry.packet():
                for sub_entry in entry.packet().entries:
                    sub_prefix = f"{key}."
                    sub_result = get_entry_value(sub_entry, flatten=True, prefix=sub_prefix)
                    result.update(sub_result)
            else:
                result[key] = "packet(empty)"
        else:
            result[key] = "unknown"
            
        return result


def export_selected_packet(packet, flatten_structs=True, custom_path=None, all_packets=None) -> str:
    """
    選択された種類のパケットデータをCSV形式でエクスポートする。
    ユーザーが選択したパケットと同じunit/component/packet idを持つすべてのパケットをCSVに出力する。
    
    Args:
        packet: エクスポートするパケット（リファレンス用。同じIDのパケットを全て取得する）
        flatten_structs: 構造体やネストしたパケットを平坦化するかどうか
        custom_path: カスタム出力先パス（指定しない場合は自動生成）
        all_packets: 全パケットを含むデータ構造。指定しない場合は現在のパケットのみを出力。
    
    Returns:
        str: 処理結果のメッセージ
    """
    # パケットIDを取得（このIDのパケットをすべて取得する）
    unit_id = packet.origin_unit_id
    component_id = packet.component_id
    packet_id = packet.packet_id
    
    # 出力ファイル名が指定されていない場合は、パケットIDを使用
    if custom_path is None:
        time_str = datetime.now().strftime("%Y%m%d%H%M%S")
        filename = f"packet_{hex(unit_id)}_{hex(component_id)}_{hex(packet_id)}_{time_str}.csv"
        filepath = os.path.join(os.environ['USERPROFILE'], 'Downloads', filename)
    else:
        filepath = custom_path
    
    # 時系列データ構造を作成（パケットの順序を保持するためリストを使用）
    time_series_data = []
    
    # この関数を呼び出した時点の現在のパケットのすべてのエントリを取得
    entry_names = []
    if flatten_structs:
        for e in packet.entries:
            flattened = get_entry_value(e, flatten=True)
            for name in flattened.keys():
                if name not in entry_names:
                    entry_names.append(name)
    else:
        for e in packet.entries:
            if e.name not in entry_names:
                entry_names.append(e.name)
    
    # 現在のパケットと同じIDの全パケットを取得
    if (all_packets and 
        unit_id in all_packets and 
        component_id in all_packets[unit_id][0] and 
        packet_id in all_packets[unit_id][0][component_id][0]):
        
        # 対象のパケットリストを取得
        packets, _, _ = all_packets[unit_id][0][component_id][0][packet_id]
        
        # 各パケットのデータを時系列に整理（順序を保持）
        for pkt in packets:
            # パケットのタイムスタンプを取得
            timestamp = getattr(pkt, 'timestamp', datetime.now())
            
            # エントリをマッピングとして取得
            entry_map = {}
            if flatten_structs:
                for e in pkt.entries:
                    flattened = get_entry_value(e, flatten=True)
                    entry_map.update(flattened)
            else:
                for e in pkt.entries:
                    entry_map[e.name] = get_entry_value(e)
            
            # パケットデータを順序付きリストに追加
            time_series_data.append((timestamp, entry_map))
    else:
        # all_packetsが提供されていない、または対象パケットが見つからない場合は、
        # 現在のパケットだけを出力
        timestamp = getattr(packet, 'timestamp', datetime.now())
        entry_map = {}
        if flatten_structs:
            for e in packet.entries:
                flattened = get_entry_value(e, flatten=True)
                entry_map.update(flattened)
        else:
            for e in packet.entries:
                entry_map[e.name] = get_entry_value(e)
        time_series_data.append((timestamp, entry_map))
    
    # コンポーネント接頭辞を作成
    col_prefix = f"C{component_id:02x}_P{packet_id:02x}_"
    
    # 最大3回リトライ
    max_retries = 3
    retry_count = 0
    success = False
    last_error = None
    
    while not success and retry_count < max_retries:
        try:
            with open(filepath, 'w', newline='', encoding='utf-8') as csvfile:
                writer = csv.writer(csvfile)
                
                # ヘッダー行: Date, Time, そしてすべてのエントリ（接頭辞付き）
                header = ["Date", "Time"]
                for name in sorted(entry_names):
                    header.append(f"{col_prefix}{name}")
                
                writer.writerow(header)
                
                # 順序を保持して各パケットのデータを出力
                for timestamp, entry_map in time_series_data:
                    # 日付と時間を分離
                    date_str = timestamp.strftime("%Y-%m-%d")
                    time_str = timestamp.strftime("%H:%M:%S")
                    
                    # 行の値を準備
                    row = [date_str, time_str]
                    for name in sorted(entry_names):
                        row.append(entry_map.get(name, ""))
                    
                    # 行を書き込み
                    writer.writerow(row)
            
            success = True
            return f"Exported {len(time_series_data)} packets of type {hex(packet_id)} to {filepath}"
            
        except PermissionError as e:
            last_error = e
            retry_count += 1
            
            # ファイル名を変更して再試行
            if custom_path:
                base, ext = os.path.splitext(custom_path)
                filepath = f"{base}_{retry_count}{ext}"
            else:
                # 自動生成ファイル名の場合は、番号を追加
                base, ext = os.path.splitext(filepath)
                filepath = f"{base}_{retry_count}{ext}"
            
            # 最後のリトライでも失敗した場合
            if retry_count >= max_retries:
                return f"Error: Unable to write to CSV file after {max_retries} attempts. File may be open in another program. Last error: {str(last_error)}"
    
    # 通常ここには到達しない（successがTrueになるか、エラーメッセージが返される）
    return "Unknown error occurred when exporting packet data"
def export_all_packets(all_packets, flatten_structs=True, custom_path=None) -> str:
    """
    all_packetからパケットデータを取得し、時間情報とともにCSVに出力する。
    コンポーネントごとにデータがまとまるよう整理し、
    シリアルモニタの表示順に従って列を配置する。
    
    Args:
        all_packets: パケットデータが格納されたデータ構造
        flatten_structs: 構造体やネストしたパケットを平坦化するかどうか
        custom_path: カスタム出力先パス（指定しない場合はデフォルトパスを使用）
    
    Returns:
        str: 処理結果のメッセージ
    """
    # 集約用のディクショナリ - タイムスタンプごとにデータを保持
    # キー: タイムスタンプ (datetime)
    # 値: {(unit_id, component_id, packet_id): {列名: 値}} の辞書
    time_series_data = {}
    
    # コンポーネントごとの列名マッピング
    # キー: (unit_id, component_id)
    # 値: 列名リスト（シリアルモニタ表示順）
    component_columns = {}
    
    # コンポーネントのパケットの順序マップ
    # キー: (unit_id, component_id)
    # 値: [packet_id1, packet_id2, ...] （シリアルモニタ表示順）
    component_packet_order = {}
    
    # 1. まず、各コンポーネントの列名と順序を決定
    for unit_id, (unit_packets, _) in all_packets.items():
        for component_id, (component_packets, _) in unit_packets.items():
            comp_key = (unit_id, component_id)
            if comp_key not in component_columns:
                component_columns[comp_key] = []
                component_packet_order[comp_key] = []
                
            for packet_id, (packets, packet_time, _) in component_packets.items():
                if packet_id not in component_packet_order[comp_key]:
                    component_packet_order[comp_key].append(packet_id)
                
                # 代表的なパケットからカラム順序を取得
                if packets:
                    sample_packet = packets[0]  # 最初のパケットをサンプルとして使用
                    
                    # このパケットのエントリ名を取得
                    entry_names = []
                    for e in sample_packet.entries:
                        if flatten_structs:
                            flattened = get_entry_value(e, flatten=True)
                            for name in flattened.keys():
                                if name not in entry_names:
                                    entry_names.append(name)
                        else:
                            if e.name not in entry_names:
                                entry_names.append(e.name)
                    
                    # コンポーネント列リストに追加（重複を避ける）
                    for name in entry_names:
                        if name not in component_columns[comp_key]:
                            component_columns[comp_key].append(name)
    
    # 2. 各パケットのデータを時系列に整理
    for unit_id, (unit_packets, _) in all_packets.items():
        for component_id, (component_packets, _) in unit_packets.items():
            comp_key = (unit_id, component_id)
            
            for packet_id, (packets, _, _) in component_packets.items():
                packet_key = (unit_id, component_id, packet_id)
                
                for pkt in packets:
                    # パケットの受信時刻を取得（または現在時刻を使用）
                    timestamp = getattr(pkt, 'timestamp', datetime.now())
                    
                    # タイムスタンプエントリが存在しない場合は作成
                    if timestamp not in time_series_data:
                        time_series_data[timestamp] = {}
                    
                    # エントリをマッピングとして取得
                    entry_map = {}
                    if flatten_structs:
                        for e in pkt.entries:
                            flattened = get_entry_value(e, flatten=True)
                            entry_map.update(flattened)
                    else:
                        for e in pkt.entries:
                            entry_map[e.name] = get_entry_value(e)
                    
                    # タイムスタンプのエントリにパケットデータを追加
                    time_series_data[timestamp][packet_key] = entry_map

    # 保存先: カスタムパスが指定されていればそれを使用、そうでなければダウンロードフォルダ
    if custom_path:
        filename = custom_path
    else:
        # 日付時刻を含むデフォルトのファイル名
        time_str = datetime.now().strftime("%Y%m%d%H%M%S")
        filename = os.path.join(os.environ['USERPROFILE'], 'Downloads', f'all_packets_{time_str}.csv')
    
    # 最大3回リトライ
    max_retries = 3
    retry_count = 0
    success = False
    last_error = None
    
    while not success and retry_count < max_retries:
        try:
            # CSV出力
            with open(filename, 'w', newline='', encoding='utf-8') as csvfile:
                writer = csv.writer(csvfile)
                
                # ヘッダーの準備
                header = ["Date", "Time"]
                
                # コンポーネントごとに列を追加
                all_comp_keys = sorted(component_columns.keys())
                for comp_key in all_comp_keys:
                    unit_id, component_id = comp_key
                    
                    # このコンポーネントのパケットIDの順序に従って列を追加
                    for packet_id in component_packet_order[comp_key]:
                        packet_prefix = f"C{component_id:02x}_P{packet_id:02x}_"
                        
                        # このパケットのエントリを追加
                        for col_name in component_columns[comp_key]:
                            header.append(f"{packet_prefix}{col_name}")
                
                # ヘッダー行を書き込み
                writer.writerow(header)
                
                # 時間順にソートしたタイムスタンプでデータを出力
                for timestamp in sorted(time_series_data.keys()):
                    # 日付と時間を分離
                    date_str = timestamp.strftime("%Y-%m-%d")
                    time_str = timestamp.strftime("%H:%M:%S")
                    
                    # 行の開始（日付と時間）
                    row = [date_str, time_str]
                    
                    # コンポーネントごとに値を追加
                    for comp_key in all_comp_keys:
                        unit_id, component_id = comp_key
                        
                        # このコンポーネントのパケットを処理
                        for packet_id in component_packet_order[comp_key]:
                            packet_key = (unit_id, component_id, packet_id)
                            
                            # このタイムスタンプにこのパケットのデータがあるか
                            packet_data = time_series_data[timestamp].get(packet_key, {})
                            
                            # このパケットのエントリ値をヘッダー順に追加
                            for col_name in component_columns[comp_key]:
                                row.append(packet_data.get(col_name, ""))
                    
                    # 行を書き込み
                    writer.writerow(row)
            
            success = True
            return f"Exported time series packet data to {filename}"
            
        except PermissionError as e:
            last_error = e
            retry_count += 1
            
            # ファイル名を変更して再試行
            if custom_path:
                base, ext = os.path.splitext(custom_path)
                filename = f"{base}_{retry_count}{ext}"
            else:
                # タイムスタンプを含む代替ファイル名
                time_str = datetime.now().strftime("%Y%m%d%H%M%S")
                filename = os.path.join(os.environ['USERPROFILE'], 'Downloads', f'all_packets_{time_str}_{retry_count}.csv')
                
            # 最後のリトライでも失敗した場合
            if retry_count >= max_retries:
                return f"Error: Unable to write to CSV file after {max_retries} attempts. File may be open in another program. Last error: {str(last_error)}"
    
    # 通常ここには到達しない（successがTrueになるか、エラーメッセージが返される）
    return "Unknown error occurred when exporting packet data"
def update_csv_with_packet(packet, csv_path=None, flatten_structs=True) -> str:
    """
    パケットデータをCSVファイルに追加または更新する。
    タイムスタンプごとに行を作成し、コンポーネントごとにカラムをまとめる。
    
    Args:
        packet: 更新または追加するパケット
        csv_path: 更新するCSVファイルのパス（指定しない場合はデフォルトパスを使用）
        flatten_structs: 構造体やネストしたパケットを平坦化するかどうか
    
    Returns:
        str: 処理結果のメッセージ
    """
    # パケット情報
    unit_id = packet.origin_unit_id
    component_id = packet.component_id
    packet_id = packet.packet_id
    
    # パケットのタイムスタンプ取得
    timestamp = getattr(packet, 'timestamp', datetime.now())
    date_str = timestamp.strftime("%Y-%m-%d")
    time_str = timestamp.strftime("%H:%M:%S")
    
    # CSVファイルのパスが指定されていない場合、デフォルトパスを使用
    if csv_path is None:
        csv_path = os.path.join(os.environ['USERPROFILE'], 'Downloads', 'real_time_packets.csv')
    
    # エントリをマッピング形式で取得
    entry_map = {}
    if flatten_structs:
        # 各エントリを平坦化して結合
        for e in packet.entries:
            flattened = get_entry_value(e, flatten=True)
            entry_map.update(flattened)
    else:
        # 従来通り、エントリ名をキーとして値を格納
        for e in packet.entries:
            entry_map[e.name] = get_entry_value(e)
    
    # カラム接頭辞（コンポーネントとパケットIDで識別）
    col_prefix = f"C{component_id:02x}_P{packet_id:02x}_"
    
    # パケットデータの列名を生成
    col_names = [col_prefix + name for name in entry_map.keys()]
    
    # CSVファイルが存在しない場合は新しく作成
    if not os.path.exists(csv_path):
        max_retries = 3
        retry_count = 0
        success = False
        last_error = None
        
        while not success and retry_count < max_retries:
            try:
                with open(csv_path, 'w', newline='', encoding='utf-8') as csvfile:
                    writer = csv.writer(csvfile)
                    
                    # ヘッダー行: Date, Time, そして各エントリ列
                    header = ["Date", "Time"] + col_names
                    writer.writerow(header)
                    
                    # 最初のデータ行
                    row = [date_str, time_str]
                    for name in entry_map.keys():
                        row.append(entry_map[name])
                    
                    writer.writerow(row)
                
                success = True
                return f"Created new CSV file with time series data at {csv_path}"
            
            except PermissionError as e:
                last_error = e
                retry_count += 1
                
                # ファイル名を変更して再試行
                base, ext = os.path.splitext(csv_path)
                csv_path = f"{base}_{retry_count}{ext}"
                
                # 最後のリトライでも失敗した場合
                if retry_count >= max_retries:
                    return f"Error: Unable to create CSV file after {max_retries} attempts. File may be open in another program. Last error: {str(last_error)}"
    
    else:  # 既存のCSVファイルを更新
        max_retries = 3
        retry_count = 0
        success = False
        last_error = None
        
        while not success and retry_count < max_retries:
            try:
                # 既存CSVの内容を一時的に保持
                rows = []
                header = []
                
                with open(csv_path, 'r', newline='', encoding='utf-8') as csvfile:
                    reader = csv.reader(csvfile)
                    for i, row in enumerate(reader):
                        if i == 0:
                            header = row
                        else:
                            rows.append(row)
                
                # ヘッダーが空の場合は新規作成と同じ処理
                if not header:
                    header = ["Date", "Time"]
                
                # パケットの列がヘッダーにあるか確認し、なければ追加
                existing_cols = set(header)
                new_cols = []
                
                for col in col_names:
                    if col not in existing_cols:
                        header.append(col)
                        new_cols.append(col)
                
                # 新しい行を追加（同じタイムスタンプの行があれば更新、なければ新規追加）
                found_timestamp = False
                for i, row in enumerate(rows):
                    if len(row) >= 2 and row[0] == date_str and row[1] == time_str:
                        # 既存の行を更新
                        found_timestamp = True
                        
                        # 行の長さをヘッダーに合わせる
                        while len(row) < len(header):
                            row.append("")
                        
                        # パケットデータを更新
                        for j, col in enumerate(header):
                            if col.startswith(col_prefix):
                                # 列名からエントリ名を抽出
                                entry_name = col[len(col_prefix):]
                                if entry_name in entry_map:
                                    row[j] = entry_map[entry_name]
                        
                        rows[i] = row
                        break
                
                # タイムスタンプが見つからない場合は新しい行を追加
                if not found_timestamp:
                    new_row = [date_str, time_str] + [""] * (len(header) - 2)
                    
                    # パケットデータを設定
                    for j, col in enumerate(header):
                        if col.startswith(col_prefix):
                            # 列名からエントリ名を抽出
                            entry_name = col[len(col_prefix):]
                            if entry_name in entry_map:
                                new_row[j] = entry_map[entry_name]
                    
                    rows.append(new_row)
                    
                    # 時間順にソート
                    rows.sort(key=lambda x: (x[0], x[1]))
                
                # 更新した内容を書き込み
                with open(csv_path, 'w', newline='', encoding='utf-8') as csvfile:
                    writer = csv.writer(csvfile)
                    writer.writerow(header)
                    writer.writerows(rows)
                
                success = True
                
                if new_cols:
                    return f"Updated CSV file with new columns: {', '.join(new_cols)}"
                else:
                    return f"Updated CSV file with new packet data"
                
            except PermissionError as e:
                last_error = e
                retry_count += 1
                
                # ファイル名を変更して再試行
                base, ext = os.path.splitext(csv_path)
                csv_path = f"{base}_{retry_count}{ext}"
                
                # 最後のリトライでも失敗した場合
                if retry_count >= max_retries:
                    return f"Error: Unable to update CSV file after {max_retries} attempts. File may be open in another program. Last error: {str(last_error)}"
        
    return "Unknown error occurred when updating CSV file"
def clear_csv_file(custom_path=None):
    """
    CSVファイルをクリアする。
    ファイルが存在する場合は削除し、新しい空ファイルは作成しない。
    
    Args:
        custom_path: カスタムCSVファイルパス
    
    Returns:
        str: 処理結果のメッセージ
    """
    messages = []
    
    # メインのCSVファイルパス（リアルタイム更新用）
    if custom_path:
        main_csv = custom_path
    else:
        main_csv = os.path.join(os.environ['USERPROFILE'], 'Downloads', 'all_packets.csv')
    
    # 補助的なCSVファイル（エクスポート用）
    backup_csv = os.path.join(os.environ['USERPROFILE'], 'Downloads', 'real_time_packets.csv')
    
    # メインCSVファイルの削除
    if os.path.exists(main_csv):
        try:
            os.remove(main_csv)
            messages.append(f"Cleared CSV file: {main_csv}")
        except PermissionError:
            messages.append(f"Warning: Could not delete {main_csv} - file may be in use")
    
    # 補助的なCSVファイルの削除（存在する場合）
    if os.path.exists(backup_csv) and backup_csv != main_csv:
        try:
            os.remove(backup_csv)
            messages.append(f"Cleared CSV file: {backup_csv}")
        except PermissionError:
            messages.append(f"Warning: Could not delete {backup_csv} - file may be in use")
    
    # 結果のメッセージを結合
    if not messages:
        return "No CSV files found to clear"
    else:
        return "\n".join(messages)


def on_input(c: str, all_packets, selection, ser, args=None) -> str:
    try:
        # 空の入力は無視
        if not c or c == '':
            return None
            
        if c == 'q':
            if ser:
                ser.close()
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
            # CSVファイルも一緒にクリアする
            custom_path = args.csv_path if args and hasattr(args, 'csv_path') else None
            csv_message = clear_csv_file(custom_path=custom_path)
            return f'cleared all packets. {csv_message}'

        if c == 's':
            return f'saved raw data as data.bin'

        if c == 'e':
            # 選択されたパケットを取得
            if selection[0] in all_packets and \
                selection[1] in all_packets[selection[0]][0] and \
                selection[2] in all_packets[selection[0]][0][selection[1]][0]: 
                packets, t, i = all_packets[selection[0]][0][selection[1]][0][selection[2]]
                
                # パケットが存在するか確認
                if packets:
                    custom_path = args.csv_path if args and hasattr(args, 'csv_path') else None
                    flatten_structs = not args.no_flatten if args and hasattr(args, 'no_flatten') else True
                    packet = packets[i]
                    return export_selected_packet(packet, flatten_structs=flatten_structs, custom_path=custom_path, all_packets=all_packets)
                else:
                    return 'No packets to export in the selected item.'
            else:
                return 'No packet selected.'
        
        if c == 'E':
            # argsが渡されていればオプションをチェック
            flatten_structs = not args.no_flatten if args and hasattr(args, 'no_flatten') else True
            custom_path = args.csv_path if args and hasattr(args, 'csv_path') else None
            return export_all_packets(all_packets, flatten_structs=flatten_structs, custom_path=custom_path)

        if c:
            return f'unknown command: {c}'

        return None
        
    except Exception as e:
        # 予期しないエラーが発生した場合の安全な処理
        return f'Command processing error: {str(e)}'


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

def add_packet(all_packets, packet, time=None, update_csv=False, flatten_structs=True, csv_path=None):
    """
    新しいパケットを all_packets データ構造に追加し、
    update_csv=True の場合はリアルタイムでCSVファイルも更新する。
    
    Args:
        all_packets: パケットを追加するデータ構造
        packet: 追加するパケット
        time: パケット受信時刻。指定がなければNone
        update_csv: CSVファイルをリアルタイムで更新するかどうかのフラグ
        flatten_structs: CSVで構造体やネストしたパケットを平坦化するかどうか
        csv_path: カスタムCSV出力パス。指定がなければNone
    """
    # タイムスタンプをパケット自身に保存（後でCSVエクスポート時に使用）
    if time:
        packet.timestamp = time
    else:
        packet.timestamp = datetime.now()
        
    all_packets[packet.origin_unit_id][1] = packet.timestamp
    all_packets[packet.origin_unit_id][0][packet.component_id][1] = packet.timestamp
    all_packets[packet.origin_unit_id][0][packet.component_id][0][packet.packet_id][0].append(packet)
    all_packets[packet.origin_unit_id][0][packet.component_id][0][packet.packet_id][1] = packet.timestamp
    
    # CSV自動更新が有効な場合、パケットデータでCSVファイルを更新
    if update_csv:
        update_csv_with_packet(packet, csv_path=csv_path, flatten_structs=flatten_structs)

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
            if hasattr(readline, "read_history_file"):
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

def write_safely_to_csv(filepath, write_function, max_retries=3):
    """
    安全にCSVファイルに書き込むためのヘルパー関数。
    ファイルがロックされている場合は別のファイル名を試みる。
    
    Args:
        filepath: 書き込み先ファイルパス
        write_function: ファイルオブジェクトを受け取り処理を行うコールバック関数
        max_retries: リトライ回数
    
    Returns:
        tuple: (成功したかどうか, 使用したファイルパス, エラーメッセージ)
    """
    retry_count = 0
    current_path = filepath
    last_error = None
    
    while retry_count < max_retries:
        try:
            # コールバック関数を実行
            with open(current_path, 'w', newline='', encoding='utf-8') as csvfile:
                write_function(csvfile)
            
            return True, current_path, None
            
        except PermissionError as e:
            last_error = str(e)
            retry_count += 1
            
            # ファイル名を変更して再試行
            base, ext = os.path.splitext(filepath)
            current_path = f"{base}_{retry_count}{ext}"
    
    # すべてのリトライが失敗
    error_msg = f"Unable to write to CSV file after {max_retries} attempts. File may be open in another program."
    return False, filepath, error_msg