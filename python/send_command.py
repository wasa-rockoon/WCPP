#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
send_command.py

WOBC Ground Station コマンド送信ツール
PCから Ground Station (util.py) 経由でマイコンへコマンドパケットを送信するためのスクリプトです。
.command ファイルへパケットを追記書き込みし、util.py がそれを自動的にシリアル送信します。
"""

import argparse
from pathlib import Path
import sys
from typing import List, Optional

# モジュールインポートの柔軟な対応
import_errs = []
package_root = Path(__file__).resolve().parents[2]
for path_entry in [str(package_root), str(Path(__file__).resolve().parent)]:
    if path_entry not in sys.path:
        sys.path.insert(0, path_entry)

try:
    from wcpp import Entry, Packet, enqueue_packet
except ImportError as e:
    import_errs.append(f"wcpp: {e}")
    try:
        from packet import Entry, Packet
        from transport import enqueue_packet
    except ImportError as e2:
        import_errs.append(f"direct import: {e2}")
        print("エラー: ローカルの WCPP モジュールを読み込めませんでした。")
        print("詳細:")
        for err in import_errs:
            print(f"  - {err}")
        print("\n※ .venv で `pip install -e src/library/wcpp` を実行してパッケージ化してから再実行してください。")
        sys.exit(1)




def parse_id(val: str) -> int:
    """
    ID文字列（16進数 "0x10"、10進数 "16"、または単一文字 'c'）を数値(int)に変換します。
    """
    val = val.strip()
    if val.startswith("0x") or val.startswith("0X"):
        return int(val, 16)
    if len(val) == 1 and not val.isdigit():
        return ord(val)
    return int(val)


def create_entry(name: str, value_str: str, data_type: str = "auto") -> Entry:
    """
    指定された名前と値から Entry を生成します。
    Entry 名は WCPP 仕様に基づき必ず2文字である必要があります。
    """
    if len(name) != 2:
        raise ValueError(f"Entry名は必ず2文字である必要があります: '{name}'")

    entry = Entry(name)
    data_type = data_type.lower()

    if data_type == "int":
        entry.set_int(int(value_str, 0))
    elif data_type == "float":
        entry.set_float32(float(value_str))
    elif data_type == "bool":
        b_val = value_str.lower() in ("true", "1", "yes", "y", "t")
        entry.set_bool(b_val)
    elif data_type == "str" or data_type == "string":
        entry.set_string(value_str)
    else:  # auto
        # 自動型判別
        low_val = value_str.lower()
        if low_val in ("true", "false"):
            entry.set_bool(low_val == "true")
        else:
            try:
                if value_str.startswith("0x") or value_str.startswith("0X"):
                    entry.set_int(int(value_str, 16))
                else:
                    entry.set_int(int(value_str, 10))
            except ValueError:
                try:
                    entry.set_float32(float(value_str))
                except ValueError:
                    entry.set_string(value_str)

    return entry


def print_packet_info(packet: Packet, command_path: Path):
    """
    送信（キュー追加）したパケットの詳細情報を画面に表示します。
    """
    print("\n" + "=" * 50)
    print(" [SUCCESS] パケットをキューに追加しました (.command)")
    print("=" * 50)
    print(f" 出力先ファイル: {command_path.resolve()}")
    print(f" パケットタイプ: COMMAND ({packet.type_})")

    char_repr = (
        f"'{chr(packet.packet_id)}'"
        if 32 <= packet.packet_id <= 126
        else "N/A"
    )
    print(
        f" Packet ID     : 0x{packet.packet_id:02X} ({packet.packet_id}, 文字表現: {char_repr})"
    )
    print(
        f" Component ID  : 0x{packet.component_id:02X} ({packet.component_id})"
    )
    print(f" Origin Unit ID : {packet.origin_unit_id}")
    print(f" Dest Unit ID   : {packet.dest_unit_id}")

    print(f" エントリ数     : {len(packet.entries)}")
    for i, entry in enumerate(packet.entries, start=1):
        if entry.is_int():
            val_info = f"int: {entry.int()}"
        elif entry.is_float():
            val_info = f"float: {entry.float()}"
        elif entry.is_bytes():
            val_info = f"string/bytes: '{entry.string()}'"
        else:
            val_info = f"raw payload: {entry.payload.hex()}"
        print(f"   [{i}] 名: '{entry.name}' | 値: {val_info}")
    print("=" * 50 + "\n")


def send_ping(command_path: Path):
    """Ping コマンドを送信"""
    packet = Packet.command(packet_id=ord("p"), component_id=0x00)
    packet.entries.append(Entry("Pg").set_string("PING"))
    enqueue_packet(packet, command_path=command_path)
    print_packet_info(packet, command_path)


def send_test_command(command_path: Path):
    """テストコマンドを送信"""
    packet = Packet.command(packet_id=ord("t"), component_id=0x10)
    packet.entries.append(Entry("Ts").set_string("TEST_COMMAND"))
    packet.entries.append(Entry("Vl").set_int(1234))
    enqueue_packet(packet, command_path=command_path)
    print_packet_info(packet, command_path)


def interactive_create_packet(command_path: Path):
    """対話形式で任意コマンドパケットを作成して送信"""
    print("\n--- 任意コマンド作成 ---")
    try:
        pkt_id_str = input(
            "Packet ID を入力してください (例: c, p, 10, 0x10) [既定: c]: "
        ).strip()
        pkt_id = parse_id(pkt_id_str) if pkt_id_str else ord("c")

        comp_id_str = input(
            "Component ID を入力してください (例: 0x10, 16) [既定: 0x01]: "
        ).strip()
        comp_id = parse_id(comp_id_str) if comp_id_str else 0x01

        packet = Packet.command(packet_id=pkt_id, component_id=comp_id)

        print("\nEntry (パラメータ) の追加:")
        while True:
            entry_name = input(
                "  Entry名 (2文字, 空白で終了): "
            ).strip()
            if not entry_name:
                break
            if len(entry_name) != 2:
                print("  [ERROR] Entry名は必ず2文字です。やり直してください。")
                continue

            entry_val = input("  Entryの値: ")
            entry_type = input(
                "  型 (auto/int/float/bool/str) [既定: auto]: "
            ).strip()
            if not entry_type:
                entry_type = "auto"

            try:
                entry = create_entry(entry_name, entry_val, entry_type)
                packet.entries.append(entry)
                print(f"  [OK] Entry '{entry_name}' を追加しました。")
            except Exception as e:
                print(f"  [ERROR] Entry作成に失敗しました: {e}")

            cont = (
                input("  さらにEntryを追加しますか？ (y/N): ")
                .strip()
                .lower()
            )
            if cont != "y":
                break

        enqueue_packet(packet, command_path=command_path)
        print_packet_info(packet, command_path)

    except KeyboardInterrupt:
        print("\nキャンセルされました。")
    except Exception as e:
        print(f"\n[ERROR] パケット生成エラー: {e}")


def interactive_mode(command_path: Path):
    """対話型（メニュー）モード"""
    while True:
        print("\n" + "=" * 45)
        print("    WOBC Command Sender (Ground Station)")
        print("=" * 45)
        print(" 1. Ping送信 (PacketID: 'p', CompID: 0x00)")
        print(" 2. テストコマンド送信 (PacketID: 't', CompID: 0x10)")
        print(" 3. 任意コマンド作成")
        print(" 4. 終了")
        print("=" * 45)

        choice = input("メニュー番号を選択してください (1-4): ").strip()
        if choice == "1":
            send_ping(command_path)
        elif choice == "2":
            send_test_command(command_path)
        elif choice == "3":
            interactive_create_packet(command_path)
        elif choice == "4" or choice.lower() == "q" or choice.lower() == "exit":
            print("終了します。")
            break
        else:
            print("無効な選択です。1〜4の数字を入力してください。")


def main():
    parser = argparse.ArgumentParser(
        description="WOBC PC側コマンド送信ツール (Ground Station)"
    )
    parser.add_argument(
        "--packet-id",
        "-p",
        type=str,
        help="パケットID (例: 'c', 'p', '0x10', '16')",
    )
    parser.add_argument(
        "--component-id",
        "-c",
        type=str,
        default="0x01",
        help="コンポーネントID (例: '0x10', '16') [既定: 0x01]",
    )
    parser.add_argument(
        "--data",
        "-d",
        type=str,
        help="送信データ (Entryとしてアペンドされます)",
    )
    parser.add_argument(
        "--entry-name",
        "-e",
        type=str,
        default="Dt",
        help="データEntry名 (2文字) [既定: Dt]",
    )
    parser.add_argument(
        "--data-type",
        "-t",
        type=str,
        default="auto",
        choices=["auto", "int", "float", "bool", "str", "string"],
        help="データ型 [既定: auto]",
    )
    parser.add_argument(
        "--command-path",
        type=str,
        default=".command",
        help="出力する .command ファイルのパス [既定: .command]",
    )
    parser.add_argument(
        "--interactive",
        "-i",
        action="store_true",
        help="対話型メニューモードを強制的に起動",
    )

    args = parser.parse_args()
    command_path = Path(args.command_path)

    # 引数が指定されていない、または --interactive の場合は対話型モード
    if args.interactive or (args.packet_id is None and args.data is None):
        interactive_mode(command_path)
        return

    # CLI 引数モード
    if args.packet_id is None:
        print(
            "エラー: --packet-id (-p) が指定されていません。対話型モードで起動するには引数なしで実行してください。"
        )
        sys.exit(1)

    try:
        pkt_id = parse_id(args.packet_id)
        comp_id = parse_id(args.component_id)

        packet = Packet.command(packet_id=pkt_id, component_id=comp_id)

        if args.data is not None:
            entry = create_entry(args.entry_name, args.data, args.data_type)
            packet.entries.append(entry)

        enqueue_packet(packet, command_path=command_path)
        print_packet_info(packet, command_path)

    except Exception as e:
        print(f"エラー: パケット送信処理に失敗しました: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
