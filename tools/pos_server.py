"""
Trace POS TCP server.

Accepts a persistent connection from the ESP32 device and sends 2-byte
binary events on demand via an interactive command prompt.

Wire format (must stay in sync with pos_message in pos_client.h):
  Byte 0: event type  (0 = CUSTOMERS_SEATED, 1 = ORDER_READY, 2 = BILL_REQUESTED)
  Byte 1: table index (0-based, 0..MAX_TABLES-1)

Commands:
  seated <table>       Customers seated at table
  order_ready <table>  Kitchen order ready for table
  bill <table>         Table requested the bill
  status               Show whether a device is connected
  help                 Show this message
  quit                 Exit the server
"""

import socket
import struct
import threading
from typing import Optional


HOST       = "0.0.0.0"
PORT       = 5050
MAX_TABLES = 24    # MAX_TABLES from trace_system.h

CUSTOMERS_SEATED = 0
ORDER_READY      = 1
BILL_REQUESTED   = 2

_conn: socket.socket | None = None
_conn_lock = threading.Lock()


def _send_event(event_type: int, table_index: int) -> bool:
    """Send a 2-byte event to the connected device. Returns False if none connected."""
    with _conn_lock:
        if _conn is None:
            return False
        try:
            _conn.sendall(struct.pack("BB", event_type, table_index))
            return True
        except OSError:
            return False


def _tcp_server():
    global _conn

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(1)
        print(f"[server] Listening on {HOST}:{PORT}")

        while True:
            conn, addr = server.accept()
            print(f"[server] Device connected: {addr}")

            with _conn_lock:
                _conn = conn

            try:
                # recv() blocks until the connection drops
                while conn.recv(1):
                    pass
            except OSError:
                pass
            finally:
                with _conn_lock:
                    _conn = None
                conn.close()

            print(f"[server] Device disconnected: {addr}")


def _parse_table(arg: str) -> int | None:
    try:
        t = int(arg)
        if 0 <= t < MAX_TABLES:
            return t
    except ValueError:
        pass
    return None


def _cli():
    print("POS server ready. Type 'help' for commands.")

    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n[server] Shutting down.")
            break

        if not line:
            continue

        parts = line.split()
        cmd   = parts[0].lower()

        if cmd == "quit":
            print("[server] Shutting down.")
            break

        elif cmd == "status":
            connected = _conn is not None
            print(f"[server] Device connected: {connected}")

        elif cmd == "help":
            print(__doc__)

        elif cmd in ("seated", "order_ready", "bill"):
            if len(parts) != 2:
                print(f"Usage: {cmd} <table>")
                continue

            table = _parse_table(parts[1])
            if table <= 0 or table is None:
                print(f"Invalid table index. Must be 1..{MAX_TABLES}.")
                continue

            event_map = {
                "seated":      CUSTOMERS_SEATED,
                "order_ready": ORDER_READY,
                "bill":        BILL_REQUESTED,
            }
            if _send_event(event_map[cmd], table - 1):
                print(f"[server] Sent {cmd} for table {table}")
            else:
                print("[server] No device connected.")

        else:
            print(f"Unknown command: '{cmd}'. Type 'help' for usage.")


if __name__ == "__main__":
    tcp_thread = threading.Thread(target=_tcp_server, daemon=True)
    tcp_thread.start()
    _cli()
