"""Record a boot timeline from the runner's debug server.

Drives the ndsrecomp debug protocol (``third_party/ndsrecomp/TCP.md``) over
``--serve`` and samples the game as it advances: emulated time, ARM9/ARM7 PC+LR,
DISPCNT for both engines, the raster (VCOUNT/DISPSTAT) and a hash of each
framebuffer. The point is a progress *curve* from one run instead of an endpoint
per run.

Sampling is driven by **ARM9 cycles**, not by ``vblank9``: the runner only bumps
``vblank9`` while the guest has DISPSTAT bit 3 (VBlank IRQ enable) set
(``io.cpp``, ``g_vcount == 192``), so it is a guest-controlled IRQ counter, not a
measure of progress. A game that disables VBlank IRQs mid-transition freezes that
counter while the raster keeps running. VCOUNT is the ground truth used here.

When the sampled state stops changing the harness takes a stall report instead of
waiting out the protocol's round cap: interrupt state, per-CPU halt reasons, raw
DISPSTAT, and a controlled ``run_cycles`` probe that separates "raster alive,
guest stopped asking for IRQs" from "display dead" from "emulation wedged".

Screenshots are written as PNG next to a top-over-bottom montage whenever the
screen changes. They are ROM-derived output: keep them in the git-ignored
``banks/`` tree, never in a commit.

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

ARM9_HZ = 67_027_980.0
VBLANKS_PER_SECOND = 59.8261


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


def wait_for_server(host, port, deadline, timeout=3600.0):
    last = None
    while time.time() < deadline:
        try:
            return DebugClient(host, port, timeout=timeout)
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


def decode_dispstat(value):
    return {
        "raw": value,
        "vblank_flag": bool(value & 0x1),
        "hblank_flag": bool(value & 0x2),
        "vcount_match": bool(value & 0x4),
        "vblank_irq": bool(value & 0x8),
        "hblank_irq": bool(value & 0x10),
        "vcount_irq": bool(value & 0x20),
        "vcount_trigger": (value >> 8) & 0xFF,
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
        "vcount": client.cmd("read_io", cpu=9, addr=0x04000006, width=16)["value"],
        "dispstat9": decode_dispstat(client.cmd("read_io", cpu=9, addr=0x04000004, width=16)["value"]),
        "dispstat7": decode_dispstat(client.cmd("read_io", cpu=7, addr=0x04000004, width=16)["value"]),
        "tier3": client.cmd("static_coverage"),
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
    """Separate 'raster alive but guest stopped asking for IRQs' from real faults."""
    before = client.cmd("event_counts")
    probe = client.cmd("run_cycles", arm9=before["cyc9"] + cycles)
    after = client.cmd("event_counts")
    return {
        "run_cmd": {key: reply.get(key) for key in
                    ("reached", "stalled", "terminal", "exhausted", "rounds",
                     "reason9", "reason7", "cycles")},
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
        "vcount_after": client.cmd("read_io", cpu=9, addr=0x04000006, width=16)["value"],
        "dispstat9_after": decode_dispstat(
            client.cmd("read_io", cpu=9, addr=0x04000004, width=16)["value"]),
    }


def describe_stall(report, state):
    probe = report["probe"]
    io = report["io_state"]
    cmd = report["run_cmd"]
    cycles_moved = probe["cyc9_after"] > probe["cyc9_before"]
    vblank_moved = probe["vblank9_after"] > probe["vblank9_before"]
    raster_moved = report["vcount_after"] != state["vcount"]
    lines = [
        f"stall: run_cmd reached={cmd['reached']} stalled={cmd['stalled']} terminal={cmd['terminal']} "
        f"exhausted={cmd['exhausted']} rounds={cmd['rounds']} "
        f"reason9={cmd['reason9']!r} reason7={cmd['reason7']!r}",
        f"stall: cyc9 {probe['cyc9_before']} -> {probe['cyc9_after']} "
        f"(insn9 {probe['insn9_before']} -> {probe['insn9_after']}) over +{probe['cycles_requested']} requested",
        f"stall: vblank9 {probe['vblank9_before']} -> {probe['vblank9_after']}, "
        f"vcount {state['vcount']} -> {report['vcount_after']} (raster {'alive' if raster_moved else 'frozen'})",
        f"stall: dispstat9 vblank_irq_enable={state['dispstat9']['vblank_irq']} -> "
        f"{report['dispstat9_after']['vblank_irq']}, vblank_flag={report['dispstat9_after']['vblank_flag']}",
        f"stall: cpu9 ime={io['cpu9']['ime']} ie=0x{io['cpu9']['ie']:X} if=0x{io['cpu9']['if']:X} "
        f"postflg={io['cpu9']['postflg']} ipcsync=0x{io['cpu9']['ipcsync']:X}",
        f"stall: cpu7 ime={io['cpu7']['ime']} ie=0x{io['cpu7']['ie']:X} if=0x{io['cpu7']['if']:X} "
        f"postflg={io['cpu7']['postflg']} ipcsync=0x{io['cpu7']['ipcsync']:X}",
    ]
    if not cycles_moved:
        lines.append("stall: verdict = no cycle progress at all (emulation wedged)")
    elif not raster_moved:
        lines.append("stall: verdict = raster frozen - the display stopped scanning out")
    elif not vblank_moved and not state["dispstat9"]["vblank_irq"]:
        lines.append("stall: verdict = raster alive, guest disabled VBlank IRQs - vblank9 is "
                     "guest-controlled, drive on cycles")
    elif not vblank_moved:
        lines.append("stall: verdict = raster alive and VBlank IRQ enabled but never raised")
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, help="path to nds_runner executable")
    parser.add_argument("--rom", required=True, help="path to the ROM the banks were built from")
    parser.add_argument("--bios", default="bios", help="scratch/identity directory (positional arg 1)")
    parser.add_argument("--cycles", type=int, default=8_000_000_000, help="ARM9 cycle budget")
    parser.add_argument("--drive", choices=("rounds", "cycles", "vblank9"), default="rounds",
                        help="what to advance on: scheduler rounds (default, immune to guest halts), "
                             "emulated cycles, or the guest's VBlank IRQ counter")
    parser.add_argument("--rounds-per-sample", type=int, default=37000,
                        help="rounds-mode step (~1/30 s of emulated time by default)")
    parser.add_argument("--cycles-per-sample", type=int, default=ARM9_HZ // 30,
                        help="cycle-mode step (~1/30 s by default)")
    parser.add_argument("--step", type=int, default=60, help="vblank9-mode step")
    parser.add_argument("--stall-samples", type=int, default=3,
                        help="consecutive samples with an unchanged VCOUNT before reporting a stall")
    parser.add_argument("--max-samples", type=int, default=0, help="stop after this many samples (0 = no limit)")
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
    parser.add_argument("--tier3", help="write the Tier-3 hot-address list here whenever the totals change")
    parser.add_argument("--tier3-every", type=int, default=100,
                        help="also refresh --tier3 every this many samples")
    parser.add_argument("--refine-at", type=float, default=0.0,
                        help="emulated seconds after which rounds-mode uses --refine-rounds as the step")
    parser.add_argument("--refine-rounds", type=int, default=4000,
                        help="rounds-mode step once --refine-at is reached")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="per-command socket timeout; a driver call that overruns it is reported as a stall")
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
    seen = set()

    def emit(record):
        timeline.write(json.dumps(record) + "\n")
        timeline.flush()

    try:
        client = wait_for_server(args.host, args.port, time.time() + args.startup_timeout, args.timeout)
        client.cmd("ping")
        print(f"server up after {time.time() - started:.1f}s", file=sys.stderr)

        state, _ = sample(client, engines="", want_frames=False)
        emit({"sample": 0, "wall_s": round(time.time() - started, 2),
              "emulated_s": round(state["counts"]["cyc9"] / ARM9_HZ, 3), **state})
        print(f"[0] t=0.00s vcount={state['vcount']:3d} A9 pc={state['arm9']['pc']:08X} "
              f"A7 pc={state['arm7']['pc']:08X}", file=sys.stderr)

        step = max(1, args.step)
        max_rounds = args.max_rounds or 12_000_000
        stalled_samples = 0
        last_vcount = state["vcount"]
        tier3_last = None
        while True:
            if args.max_samples and samples >= args.max_samples:
                break
            last_emulated = state["counts"]["cyc9"] / ARM9_HZ
            step_rounds = 0
            try:
                if args.drive == "rounds":
                    step_rounds = max(1, args.rounds_per_sample)
                    if args.refine_at and last_emulated >= args.refine_at:
                        step_rounds = max(1, args.refine_rounds)
                    reply = client.cmd("run_rounds", count=step_rounds)
                    target, progressed = None, True
                elif args.drive == "cycles":
                    target = state["counts"]["cyc9"] + max(1, args.cycles_per_sample)
                    reply = client.cmd("run_cycles", arm9=target)
                    progressed = bool(reply.get("reached"))
                else:
                    target = state["counts"]["vblank9"] + step
                    reply = client.cmd("run_to_event", event="vblank9", count=target,
                                       stall=args.stall, max_rounds=max_rounds)
                    progressed = bool(reply.get("reached"))
                    if progressed and not args.max_rounds:
                        max_rounds = min(50_000_000, max(4_000_000, int(reply["rounds"]) * 8))
            except TimeoutError:
                emit({"sample": samples + 1, "wall_s": round(time.time() - started, 2),
                      "emulated_s": round(last_emulated, 3), "stalled": True,
                      "driver": args.drive, "driver_rounds": step_rounds,
                      "note": f"driver call did not return within {args.timeout}s"})
                print(f"stall: {args.drive} driver call did not return within {args.timeout}s "
                      f"(asked for {step_rounds} rounds at {last_emulated:.2f}s emulated) - "
                      f"the emulation is not progressing", file=sys.stderr)
                break

            samples += 1
            state, frames = sample(client, engines=args.engines, want_frames=not args.no_frames)
            emulated = state["counts"]["cyc9"] / ARM9_HZ
            record = {"sample": samples, "wall_s": round(time.time() - started, 2),
                      "emulated_s": round(emulated, 3), "vblanks": round(emulated * VBLANKS_PER_SECOND),
                      **state}
            emit(record)

            disp_a = state["dispcnt_A"]
            print(f"[{samples}] t={emulated:7.2f}s wall={record['wall_s']:.0f}s vcount={state['vcount']:3d} "
                  f"A9={state['arm9']['pc']:08X} A7={state['arm7']['pc']:08X} "
                  f"bgA={''.join('1' if b else '0' for b in disp_a['bg'])} objA={int(disp_a['obj'])} "
                  f"blankA={int(disp_a['forced_blank'])} vbIRQ={int(state['dispstat9']['vblank_irq'])}",
                  file=sys.stderr)

            if args.screenshots and frames:
                os.makedirs(args.screenshots, exist_ok=True)
                digest = hashlib.sha256(b"".join(frames[e][2] for e in sorted(frames))).hexdigest()
                if written.get("all") != digest:
                    written["all"] = digest
                    for engine, (width, height, rgb) in frames.items():
                        write_png(os.path.join(args.screenshots, f"t{emulated:07.2f}s-{engine}.png"),
                                  width, height, rgb)
                    if not args.no_montage and len(frames) == 2:
                        write_montage(os.path.join(args.screenshots, f"t{emulated:07.2f}s-ds.png"),
                                      [frames["A"], frames["B"]])

            if args.tier3:
                tier3 = state["tier3"]
                entries = (tier3.get("tier3_entries9"), tier3.get("tier3_entries7"))
                if entries != tier3_last or samples % max(1, args.tier3_every) == 0:
                    tier3_last = entries
                    coverage = client.cmd("tier3_coverage", max=16384)["entries"]
                    with open(args.tier3, "w", encoding="utf-8") as handle:
                        json.dump({"emulated_s": round(emulated, 3), "sample": samples,
                                   "stats": tier3, "coverage": coverage}, handle)
                    print(f"tier3: entries9={tier3['tier3_entries9']} insns9={tier3['tier3_insns9']} "
                          f"entries7={tier3['tier3_entries7']} insns7={tier3['tier3_insns7']} "
                          f"rejects9={tier3['clean_ram_rejects9']} rejects7={tier3['clean_ram_rejects7']}"
                          f" -> {args.tier3}", file=sys.stderr)

            seen.add((state["arm9"]["pc"], state["arm7"]["pc"], state["vcount"],
                      tuple(state["counts"].values())))

            stalled_samples = stalled_samples + 1 if state["vcount"] == last_vcount else 0
            last_vcount = state["vcount"]

            if not progressed or stalled_samples >= args.stall_samples:
                report = stall_report(client, args.stall_probe_cycles, reply)
                emit({"sample": samples + 1, "wall_s": round(time.time() - started, 2),
                      "emulated_s": round(emulated, 3), "stalled": True,
                      "unchanged_vcount_samples": stalled_samples, **report})
                for line in describe_stall(report, state):
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
        print(f"timeline: {out_path} ({samples} samples, {len(seen)} distinct states, "
              f"{time.time() - started:.1f}s wall)", file=sys.stderr)


if __name__ == "__main__":
    main()
