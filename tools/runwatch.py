"""Record a boot timeline from the runner's debug server.

Drives the ndsrecomp debug protocol (``third_party/ndsrecomp/TCP.md``) over
``--serve`` and samples the game as it advances: vblank counts, ARM9/ARM7 PC+LR,
DISPCNT for both engines, and a hash of each framebuffer. The point is a progress
*curve* from one run instead of an endpoint per run.

When the watched counter stops advancing the harness takes a stall report rather
than waiting out the protocol's round cap: interrupt state for both CPUs, raw
DISPCNT, and a controlled ``run_cycles`` probe that separates "time is passing
but no more VBlanks arrive" from "the emulation itself is wedged".

Screenshots are written as PNG next to a side-by-side (top over bottom) montage
per change. They are ROM-derived output: keep them in the git-ignored ``banks/``
tree, never in a commit.

    python tools/runwatch.py --runner <nds_runner> --rom "roms/<your-rom>.nds" \\
        --screenshots banks/screenshots --out banks/timeline.jsonl
"""

import argparse
import datetime
import hashlib
import json
import os
import socket
import struct
import subprocess
import sys
import time
import zlib


class DebugClient:
    def __init__(self, host, port, timeout):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b""

    def cmd(self, name, **args):
        args["cmd"] = name
        self.sock.sendall((json.dumps(args) + "\n").encode())
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("debug server closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        resp = json.loads(line)
        if isinstance(resp, dict) and resp.get("error"):
            raise RuntimeError(f"{name}: {resp['error']}")
        return resp

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def framebuffer(self, engine):
        reply = self.cmd("framebuffer", engine=engine)
        return reply["w"], reply["h"], bytes.fromhex(reply["rgb"])


def wait_for_server(host, port, deadline):
    last = None
    while time.time() < deadline:
        try:
            return DebugClient(host, port, timeout=3600.0)
        except OSError as exc:
            last = exc
            time.sleep(1.0)
    raise TimeoutError(f"debug server never came up on {host}:{port} ({last})")


def decode_dispcnt(value):
    return {
        "raw": value,
        "mode": value & 0x7,
        "forced_blank": bool(value & 0x80),
        "bg": [bool(value & (1 << (8 + i))) for i in range(4)],
        "obj": bool(value & (1 << 12)),
        "layers": (value & 0x1F00) != 0,
    }


def frame_stats(rgb):
    lit = 0
    for i in range(0, len(rgb), 3):
        if rgb[i] or rgb[i + 1] or rgb[i + 2]:
            lit += 1
    return lit


def png_chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def encode_png(rows):
    height = len(rows)
    width = len(rows[0]) // 3
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
            + png_chunk(b"IDAT", zlib.compress(raw, 6)) + png_chunk(b"IEND", b""))


def write_png(path, width, height, rgb):
    rows = [rgb[y * width * 3:(y + 1) * width * 3] for y in range(height)]
    with open(path, "wb") as handle:
        handle.write(encode_png(rows))


def write_montage(path, screens):
    """Stack screens vertically, console-style: screens[0] is the top screen."""
    width, height, rgb = screens[0]
    rows = [list(rgb[y * width * 3:(y + 1) * width * 3]) for y in range(height)]
    for _, other_height, other_rgb in screens[1:]:
        rows += [list(other_rgb[y * width * 3:(y + 1) * width * 3]) for y in range(other_height)]
    with open(path, "wb") as handle:
        handle.write(encode_png(rows))


def sample(client, engines, want_frames):
    counts = client.cmd("event_counts")
    regs9 = client.cmd("regs", cpu=9)
    regs7 = client.cmd("regs", cpu=7)
    state = {
        "counts": counts,
        "arm9": {"pc": regs9["r"][15], "lr": regs9["r"][14], "mode": regs9["mode"]},
        "arm7": {"pc": regs7["r"][15], "lr": regs7["r"][14], "mode": regs7["mode"]},
    }
    for engine, addr in (("A", 0x04000000), ("B", 0x04001000)):
        dispcnt = client.cmd("read_io", cpu=9, addr=addr, width=32)["value"]
        state[f"dispcnt_{engine}"] = decode_dispcnt(dispcnt)
    frames = {}
    if want_frames:
        for engine in engines:
            width, height, rgb = client.framebuffer(engine)
            frames[engine] = (width, height, rgb)
            state[f"frame_{engine}"] = {
                "w": width,
                "h": height,
                "lit_px": frame_stats(rgb),
                "sha256": hashlib.sha256(rgb).hexdigest(),
            }
    return state, frames


def any_layer(state):
    for engine in ("A", "B"):
        dispcnt = state.get(f"dispcnt_{engine}")
        if dispcnt and (dispcnt["layers"] or dispcnt["obj"]) and not dispcnt["forced_blank"]:
            return True
    return False


def stall_report(client, cycles, reply):
    """Distinguish 'no VBlanks but time passes' from 'emulation wedged'."""
    before = client.cmd("event_counts")
    probe_cycles = before["cyc9"] + cycles
    probe = client.cmd("run_cycles", arm9=probe_cycles)
    after = client.cmd("event_counts")
    return {
        "run_to_event": {key: reply.get(key) for key in
                         ("reached", "stalled", "terminal", "exhausted", "rounds",
                          "reason9", "reason7")},
        "io_state": client.cmd("io_state"),
        "probe": {
            "cycles_requested": cycles,
            "cyc9_before": before["cyc9"],
            "cyc9_after": after["cyc9"],
            "insn9_before": before["insn9"],
            "insn9_after": after["insn9"],
            "vblank9_before": before["vblank9"],
            "vblank9_after": after["vblank9"],
            "run_cycles_reached": probe.get("reached"),
        },
    }


def describe_stall(report):
    probe = report["probe"]
    io = report["io_state"]
    event = report["run_to_event"]
    lines = [
        f"stall: run_to_event stalled={event['stalled']} terminal={event['terminal']} "
        f"exhausted={event['exhausted']} rounds={event['rounds']} "
        f"reason9={event['reason9']!r} reason7={event['reason7']!r}",
        f"stall: cyc9 {probe['cyc9_before']} -> {probe['cyc9_after']} "
        f"(insn9 {probe['insn9_before']} -> {probe['insn9_after']}), "
        f"vblank9 {probe['vblank9_before']} -> {probe['vblank9_after']}, "
        f"run_cycles reached={probe['run_cycles_reached']}",
        f"stall: cpu9 ime={io['cpu9']['ime']} ie=0x{io['cpu9']['ie']:X} if=0x{io['cpu9']['if']:X} "
        f"postflg={io['cpu9']['postflg']} ipcsync=0x{io['cpu9']['ipcsync']:X}",
        f"stall: cpu7 ime={io['cpu7']['ime']} ie=0x{io['cpu7']['ie']:X} if=0x{io['cpu7']['if']:X} "
        f"postflg={io['cpu7']['postflg']} ipcsync=0x{io['cpu7']['ipcsync']:X}",
        f"stall: num_frames={io['num_frames']}",
    ]
    if probe["vblank9_after"] == probe["vblank9_before"] and probe["cyc9_after"] > probe["cyc9_before"]:
        if probe["vblank9_before"] == 0:
            lines.append("stall: verdict = display has not started, so no VBlanks exist yet")
        else:
            lines.append("stall: verdict = CPUs execute, VBlank source has stopped (not a wedged emulator)")
    elif probe["cyc9_after"] == probe["cyc9_before"]:
        lines.append("stall: verdict = no cycle progress at all (emulation wedged)")
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, help="path to nds_runner executable")
    parser.add_argument("--rom", required=True, help="path to the ROM the banks were built from")
    parser.add_argument("--bios", default="bios", help="scratch/identity directory (positional arg 1)")
    parser.add_argument("--cycles", type=int, default=4_000_000_000, help="ARM9 cycle budget")
    parser.add_argument("--step", type=int, default=60, help="vblanks between samples")
    parser.add_argument("--max-vblanks", type=int, default=0, help="stop after this many vblanks (0 = no limit)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19842)
    parser.add_argument("--engines", default="AB", help="engines to capture frames for")
    parser.add_argument("--no-frames", action="store_true", help="skip framebuffer reads entirely")
    parser.add_argument("--startup-timeout", type=float, default=600.0)
    parser.add_argument("--out", help="JSONL timeline path")
    parser.add_argument("--screenshots", help="write PNG screenshots here when the screen changes")
    parser.add_argument("--no-montage", action="store_true", help="skip the top-over-bottom montage")
    parser.add_argument("--runner-log", help="capture runner stdout/stderr here")
    parser.add_argument("--keep-running", action="store_true", help="do not stop at the first enabled layer")
    parser.add_argument("--stall", type=int, default=200000, help="run_to_event no-progress round limit")
    parser.add_argument("--max-rounds", type=int, default=0,
                        help="run_to_event round cap (0 = adapt to the observed cost per step)")
    parser.add_argument("--stall-probe-cycles", type=int, default=20000000, help="cycles for the stall probe")
    args = parser.parse_args()

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_path = args.out or f"banks/timeline-{stamp}.jsonl"
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)

    command = [
        args.runner, args.bios, str(args.cycles),
        "--boot", "direct", "--generated-firmware", "--freebios", "--no-save",
        "--rom", args.rom, "--serve", "--port", str(args.port),
    ]
    log_handle = open(args.runner_log, "wb") if args.runner_log else subprocess.DEVNULL
    print("runner:", " ".join(command), file=sys.stderr)
    process = subprocess.Popen(command, stdout=log_handle, stderr=subprocess.STDOUT)

    timeline = open(out_path, "w", encoding="utf-8")
    client = None
    started = time.time()
    samples = 0
    written = {}

    def emit(record):
        timeline.write(json.dumps(record) + "\n")
        timeline.flush()

    try:
        client = wait_for_server(args.host, args.port, time.time() + args.startup_timeout)
        client.cmd("ping")
        print(f"server up after {time.time() - started:.1f}s", file=sys.stderr)

        probe, _ = sample(client, engines="", want_frames=False)
        vblank = probe["counts"]["vblank9"]
        emit({"sample": 0, "wall_s": round(time.time() - started, 2), "vblank9": vblank, **probe})
        print(f"[0] vblank9={vblank} A9 pc={probe['arm9']['pc']:08X} A7 pc={probe['arm7']['pc']:08X}",
              file=sys.stderr)

        step = max(1, args.step)
        max_rounds = args.max_rounds or 12_000_000
        while True:
            vblank += step
            if args.max_vblanks and vblank > args.max_vblanks:
                break
            reply = client.cmd("run_to_event", event="vblank9", count=vblank,
                               stall=args.stall, max_rounds=max_rounds)
            if reply.get("reached") and not args.max_rounds:
                max_rounds = min(50_000_000, max(4_000_000, int(reply["rounds"]) * 8))
            samples += 1
            want_frames = not args.no_frames
            state, frames = sample(client, engines=args.engines, want_frames=want_frames)
            record = {"sample": samples, "wall_s": round(time.time() - started, 2),
                      "vblank9": state["counts"]["vblank9"], **state}
            emit(record)

            disp_a = state["dispcnt_A"]
            print(f"[{samples}] t={record['wall_s']:.0f}s vblank9={record['vblank9']} "
                  f"A9={state['arm9']['pc']:08X} A7={state['arm7']['pc']:08X} "
                  f"bgA={''.join('1' if b else '0' for b in disp_a['bg'])} objA={int(disp_a['obj'])} "
                  f"blankA={int(disp_a['forced_blank'])}",
                  file=sys.stderr)

            if args.screenshots and frames:
                os.makedirs(args.screenshots, exist_ok=True)
                digest = hashlib.sha256(b"".join(frames[e][2] for e in sorted(frames))).hexdigest()
                if written.get("all") != digest:
                    written["all"] = digest
                    for engine, (width, height, rgb) in frames.items():
                        write_png(os.path.join(args.screenshots, f"vblank{vblank:06d}-{engine}.png"),
                                  width, height, rgb)
                    if not args.no_montage and len(frames) == 2:
                        write_montage(os.path.join(args.screenshots, f"vblank{vblank:06d}-ds.png"),
                                      [frames["A"], frames["B"]])

            if not reply.get("reached"):
                report = stall_report(client, args.stall_probe_cycles, reply)
                emit({"sample": samples + 1, "wall_s": round(time.time() - started, 2),
                      "stalled": True, "requested_vblank9": vblank, **report})
                for line in describe_stall(report):
                    print(line, file=sys.stderr)
                break

            if not args.keep_running and any_layer(state):
                print("layer enabled - first visible frame reached", file=sys.stderr)
                break
    finally:
        if client:
            client.close()
        timeline.close()
        process.terminate()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
        if log_handle is not subprocess.DEVNULL:
            log_handle.close()
        print(f"timeline: {out_path} ({samples} samples, {time.time() - started:.1f}s wall)",
              file=sys.stderr)


if __name__ == "__main__":
    main()
