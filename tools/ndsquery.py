"""One-shot queries against a running ndsrecomp debug server.

The runner started with ``--serve`` speaks line-delimited JSON
(``third_party/ndsrecomp/TCP.md``). This sends a few commands and prints the
raw replies, so bring-up questions don't need a throwaway script or a shell
quoting puzzle.

    python tools/ndsquery.py io_state
    python tools/ndsquery.py '{"cmd":"read_io","cpu":9,"addr":67125248,"width":16}'
    python tools/ndsquery.py regs --arg cpu=9
"""

import argparse
import json
import socket
import sys


def parse_command(text, args):
    command = json.loads(text) if text.lstrip().startswith("{") else {"cmd": text}
    for pair in args:
        key, _, value = pair.partition("=")
        try:
            command[key] = int(value, 0)
        except ValueError:
            command[key] = value
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("commands", nargs="+")
    parser.add_argument("--arg", action="append", default=[], help="key=value, appended to every command")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19842)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--raw", action="store_true", help="print replies as-is instead of summarising")
    args = parser.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=args.timeout)
    sock.settimeout(args.timeout)
    buf = b""
    failed = False
    for text in args.commands:
        command = parse_command(text, args.arg)
        sock.sendall((json.dumps(command) + "\n").encode())
        try:
            while b"\n" not in buf:
                chunk = sock.recv(65536)
                if not chunk:
                    raise ConnectionError("server closed the connection")
                buf += chunk
        except socket.timeout:
            print(f"{command['cmd']}: <no reply within {args.timeout}s>", flush=True)
            failed = True
            continue
        line, buf = buf.split(b"\n", 1)
        print(f"{command['cmd']}: {line.decode()}", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
