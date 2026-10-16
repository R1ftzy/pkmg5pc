# recompilation / static-recompilation tooling (D7)

## Context

Plan §2 resolves D1 as **static recompilation (B)**: recompile the games' ARM9 +
overlays + ARM7 to C++, implement the hardware as a HAL, ship no emulator core.
D7 asks *which* recompiler to use. Constraint: this project is
**GPL-3.0-or-later** (see Decision), so GPL-derived components (melonDS trees) are
compatible; proprietary/GPL-incompatible code still may not be vendored
(`AGENTS.md` §2.7/§4).

## Method families (settled 2026-10-14)

There are two ways a "PC port" of a console game gets made. We are squarely the
**static-recompilation family**; the other family needs source we do not have.

| Project | Method | Game code source | Hardware seam |
| --- | --- | --- | --- |
| **Sonic Unleashed Recompiled** | **Static recompilation** (XenonRecomp: PPC→C++; XenosRecomp: Xenos shaders→HLSL) | generated C++ from the retail binary | full **runtime** (mem mgmt, FS, sync, audio, video output) |
| **Zelda 64: Recompiled** (Majora's Mask; OoT planned) | **Static recompilation** (`N64Recomp`, MIPS→C) | generated C from the ROM | full hardware **runtime** |
| **ndsrecomp** (this project's recompiler + runner) | **Static recompilation** (ARMv4T/v5TE→C++) | generated C++ from `arm9.bin`/overlays/ARM7 | dual-CPU runtime + HAL |
| Ship of Harkinian (the well-known OoT PC port) | **Decompilation port** (OoT decomp + `libultraship`) | **human-written C source** (the decomp) | thin platform layer |

Naming trap: the famous OoT PC port (**Ship of Harkinian**) is *not* a recomp —
it is a **decompilation port**, possible only because a full matching
decompilation of OoT exists. The genuine "Zelda recomp" is **Zelda 64:
Recompiled** (`N64Recomp`), which is the same family as Sonic Unleashed
Recompiled and `ndsrecomp`.

Recomp projects never ship an emulator core: the CPU's machine code is
translated ahead of time and the result runs natively. What remains is a
**runtime** that services the hardware the translated code pokes through
memory-mapped registers ("memory management, file system, synchronization,
audio, and, most importantly, video output" — Unleashed Recompiled's lead).
That runtime is unavoidable in *every* recomp port; it is not an emulator
wrapper.

**Decision: option 1 — the faithful floor (recomp family).** We are
Sonic-Unleashed-Recompiled / Zelda64Recomp, *not* Ship of Harkinian, because
**no Gen 5 decompilation exists** to port. A Ship-of-Harkinian-style approach
for Gen 5 is only reachable later by hand-writing native replacements over the
recompiled code — i.e. the HLE layer.

## Candidate tools (surveyed 2026-10-14)

| Tool | What it is | License | Verdict |
| --- | --- | --- | --- |
| `RetroPortingToolKit/ndsrecomp` (ex `mstan/ndsrecomp`) @ **`v0.6.10`** | DS static recompiler + runtime; ARM9/ARM7 dual-CPU scheduler; HLE layers | **MIT** (repo own code, this tag) | **Adopt** (pinned tag; see below). |
| `RetroPortingToolKit/ndsrecomp` @ `main` | newer (GameCube-/Mario-64-style work, `PPC`?) | **PolyForm Noncommercial 1.0.0** + GPL-3.0 combined | **Do not use.** |
| `mstan/arm-recomp-core` (submodule `external/arm-recomp-core`) | the recompiler core | **MIT** | Usable |
| `mstan/freebios` (submodule `third_party/freebios`) | DraStic BIOS replacement | **BSD-2-Clause** | Usable (D8) |
| `Myth7x/ndsrecomp` | ROM → standalone C/CMake project (Python generator) | **No license file** | **Unusable.** Black/B2W2 listed as not booting. |
| `AetiasHax/ds-decomp` (`dsd`) | decomp toolkit: extract, delink, `dis`, `objdiff`, symbols | **MIT** | Usable (RE tooling) |

### License boundary inside ndsrecomp (v0.6.10)

- **MIT** — repo's own code: `recompiler/`, `runner/` (core), `tools/`,
  `launcher/`, `bios/`, docs, plus the `arm-recomp-core` submodule.
- **BSD-2-Clause** — `third_party/freebios`.
- **GPL-3.0-or-later** — everything derived from **melonDS**:
  `runner/vendor/melonds*`, `runner/src/melonds_compute` (accelerated 3D), and
  the `oracle/` differential harness.

Correction (2026-10-14, after reading the tree): the GPL scope is **larger**
than the first survey suggested. `THIRD_PARTY_ATTRIBUTION.md` is explicit:

- GPL-3.0-or-later = **all of** `runner/vendor/melonds/` (the melonDS **3D
  geometry engine, both the software rasterizer `GPU3D_Soft` and the OpenGL
  compute renderer**), `runner/src/melonds_compute/`, the Wi-Fi device model +
  net glue (`Wifi`/`WifiAP`/`net/`), `runner/src/wifi_net.cpp`, and `oracle/`.
- MIT = the rest of `runner/` — including the **2D compositor (`gpu2d.cpp`)**,
  the **SPU (`spu.cpp`)**, `bus/io/vram/scheduler/cp15`, `net/*` (host-side),
  `network_savestate_guard.*`, `title_patches`, the profiler/debug server.

Crucially, GPL is confined behind two clean C-style bridge headers
(`runner/src/gpu3d.h`, `runner/src/wifi.h`): `melonDS::NDS g_nds` exists **only**
in `gpu3d.cpp`, and the core (`bus/io/vram/gpu2d/spu/scheduler/state`) contains
**zero** `melonDS::` references. This isolation is why the GPL boundary stays
clean and auditable even though we now ship the melonDS device models.

## Decision (D7) — resolved

**Adopt `RetroPortingToolKit/ndsrecomp` pinned at tag `v0.6.10`
(`aa40b575b6f68a537f24ff3e4c94b567baa25587`) as the recompiler + runner
(its `recompiler` + `runner`) *as-is*, keeping its melonDS-derived 3D GE
(software + compute) and Wi-Fi, and license this project GPL-3.0-or-later.**

> This **supersedes** the earlier "delete the melonDS GPL subtrees and stay MIT"
> decision. The project deliberately migrated to GPL-3.0-or-later so the full,
> working 3D GE and Wi-Fi stack can be reused verbatim — deleting them would
> have removed *all* 3D rendering (soft and compute) and forced stubs.

Rationale:

- `v0.6.10` is the last tag whose **own** code is MIT; its melonDS trees are
  GPL-3.0-or-later. MIT is GPL-compatible, so the combined work is licensed
  GPL-3.0-or-later (a permitted one-way relicence). `main` is PolyForm
  Noncommercial and cannot be combined into anything.
- "Don't reinvent the wheel": the runtime/HAL is the expensive part of M2, and
  `ndsrecomp` already ships one (its recompiler handles autoload tables, static
  copies, ARM7 stack trampolines, relocated-code aliases, override tables, and a
  live-opcode fallback — exactly the architecture we would build).
- Its demonstrated titles do **not** include Gen 5 (firmware menu is the
  baseline), so expect real bring-up, not a turnkey boot.
- Prior in-house-recompiler plan (this file's old content) is **superseded**.

### Adoption architecture (option 1: faithful floor)

1. **Runner** = `ndsrecomp` runner: dual-CPU (ARM9+ARM7) scheduler, memory bus,
   MMIO dispatch, CP15/TCM, DMA, timers/IRQ, IPC/FIFO, cart/NitroFS, 2D/3D PPU,
   audio.
2. **Game code** = static banks generated from the user's ROM (`arm9.bin`,
   overlays at RAM addrs, ARM7 image) → C++ linked into the runner. Generated
   banks are immutable; we patch/extend via HLE wrappers + descriptors (never by
   editing generated code).
3. **Our integration** = feed NitroFS reads from the M1 `nds`/`narc` layer;
   keep the melonDS 3D GE (software floor by default, compute optional) and the
   ARM7 sound path enabled.
4. **Graphics + sound are part of the runner**, not stubbed by us: enable the
   2D compositor, the GE (3D added as the title scene needs it), and the ARM7
   sound path (recompiled driver + host audio device) from the start.

### BIOS (D8)

HLE the BIOS SWIs by default via `freebios` (BSD-2-Clause; no dump required). A
user-supplied BIOS stays an optional input, never shipped.

## References

- `RetroPortingToolKit/ndsrecomp` README / `PRINCIPLES.md` / `HLE_ARCHITECTURE.md`
  (architecture reference; licenses above). `v0.6.10` tree.
- Sonic Unleashed Recompiled / XenonRecomp (the direct analog of this approach).
- `N64Recomp` / Zelda64Recomp (the N64 analog).
- Ship of Harkinian / OoT decomp (the decomp-port family we cannot use).
- `mstan/freebios` (BSD-2-Clause); `AetiasHax/ds-decomp` (MIT).
- `overlays.md` (BLZ), `header.md` (load addresses), `arm9-init.md` (entry/init).

---

## Integration findings (2026-10-16) — v0.6.10, verified in-tree

Tool/ROM: `nds_recompile` + `nds_runner` @ `aa40b575` built with LLVM-MinGW
clang (Release, Ninja). Target ROM: Black USA/EU NDSi, 256 MiB.

### Corrections (2026-10-16, re-verified at HEAD)

- **The submodule hazard below is already resolved.** `git ls-tree HEAD
  third_party/ndsrecomp` returns `160000 commit
  aa40b575b6f68a537f24ff3e4c94b567baa25587` — the `v0.6.10` tag, not
  `origin/main` — and `HEAD:.gitmodules` carries both entries (the
  `third_party/ndsrecomp` one and the nested
  `third_party/ndsrecomp/external/arm-recomp-core` override). Only the working
  tree was ever wrong. The findings below are kept as the record of what was
  observed; treat their "must fix before the first push" as done.
- **The throughput figures in "First boot of Black" are void.** That run used
  `build/windows-debug` (`CMAKE_BUILD_TYPE=Debug`, 49 MB runner), so neither
  "~0.75 % of real-time" nor any timing derived from it describes an optimized
  build. Bring-up runs now use an optimized configuration; re-measure before
  drawing conclusions. (The 250 M-cycle run's own profile implies ~72 s wall,
  not the ~2491 s recorded here — an unexplained ~35× internal inconsistency in
  that log, another reason to discard the numbers rather than quote them.)

### Optimized build (2026-10-16) — the Debug caveat resolved

Same command, same ROM, `cmake --preset windows-relwithdebinfo
-DPKMNG5PC_BUILD_RUNNER=ON` (whole runner and every bank shard at `-O2 -g`):

| 30 M ARM9 cycles | Debug | RelWithDebInfo |
| --- | --- | --- |
| wall | 60.32 s | **4.98 s** |
| scheduler (emulation) | 16.358 ms/1000 rounds → 4.38 s | **6.332 ms/1000 rounds → 1.69 s** |
| fixed startup | ~55.9 s | **~3.3 s** |

- **Observation.** Marginal throughput on the optimized build is **~17.7 M ARM9
  cycles/s ≈ 26.4 % of real-time** (was ~6.3 % on Debug). Emulation is 2.6×
  faster and the fixed pre-scheduler cost is 17× smaller.
- **Observation.** Scheduler split at this budget is ARM9 2.515, **ARM7 2.387**,
  devices 0.946 ms/1000 rounds — the ARM7 is now ~38 % of scheduler time rather
  than the 71 % seen on Debug at 250 M, so coverage work still matters but is no
  longer the whole story.
- **Inference (resolves the startup-tax question).** The ~35 s ROM read and
  ~12 s SHA-1 both collapse into the ~3.3 s fixed cost once optimized. **No local
  patch series is warranted**; the vendored reader needs no change, and
  `third_party/ndsrecomp` stays pristine with no exceptions.
- **Observation.** `event_counts` exposes `insn9`/`insn7` and `cyc9`/`cyc7`
  alongside the vblank counters, which makes emulated-time progress measurable
  from the debug server alone (`tools/runwatch.py`).
- **Observation (smoke run, 60 M cycles, `--serve`).** At vblank9=60 the ARM9 is
  at `0x02076ED0` and at vblank9=120 at `0x020882D8` — different SDK addresses,
  not one spinning routine — while both engines report `bgA=0000 objA=0` and a
  uniform framebuffer (all 49152 pixels lit, identical hash on A and B, i.e. the
  backdrop with no layer enabled). Emulated `cyc9` at vblank9=120 is 156.5 M.
- **Unknown.** Whether a layer is ever enabled within tens of seconds of
  emulated time; the long `runwatch` run decides it.

### First visible frame (2026-10-16)

- **Observation.** `tools/runwatch.py` sampled every 60 vblanks and stopped on
  the first enabled layer (`banks/timeline-boot.jsonl`). At vblank9 = 60/120/180
  both engines report `bg=0000 obj=0` and the framebuffers are identical
  (`sha256 4ae2e6e6…`, all 49152 pixels lit — the backdrop, no layer). At
  **vblank9 = 240** (`cyc9 = 290,996,340` ≈ 4.34 s emulated) **BG1 is enabled on
  both engines** and the frames change: A `c63792f0001255…`, B `30343c48af6268aa…`.
- **Observation.** Frame A is the **Pokémon Company + Nintendo** boot splash,
  light-on-white as expected mid-fade; frame B is blank white. Written locally
  as PPM/PNG under git-ignored `banks/frames/` — ROM-derived output, never
  committed.
- **Inference (high).** The M2.3 gate is met: the game's own recompiled 2D path
  draws the boot splash, so BG setup, VRAM, palettes and the compositor are all
  live. The earlier "no BG/OBJ layer is ever enabled" finding was a **near
  miss** — that run reached ~223 vblanks; the layer turns on at 240.
- **Unknown.** Whether the sequence proceeds through the remaining logos to the
  title screen, and whether the ARM7 Tier-3 load (still ~38 % of scheduler time)
  starts to dominate as scenes get busier.

### `vblank9` is a guest-controlled counter, not a progress measure (2026-10-16)

- **Observation (source).** `io.cpp` increments `g_counts.vblank9` only inside
  `if (g_dispstat[0] & 0x0008u)` at `g_vcount == 192` — it counts *VBlank IRQs
  actually raised*, which requires the ARM9 to have DISPSTAT bit 3 (VBlank IRQ
  enable) set. `nds_event_value("vblank9")` and `io_state`'s `num_frames` both
  read that same counter. The raster itself is independent: VCOUNT starts
  sweeping at t≈0.03 s, and `vbIRQ` is still 0 until t≈0.37 s.
- **Consequence.** A harness that advances the guest with
  `run_to_event event=vblank9` stops making progress for as long as the guest
  has that IRQ disabled — normal across scene transitions and fades. Two
  bring-up runs ended this way (at `vblank9` 4980 and 258). The submodule's own
  probes are unaffected because the firmware paths they target leave VBlank IRQs
  enabled throughout.
- **Correction.** The "freezes at `vblank9=4980`" note above is **void as
  stated**: the counter stopped, not the machine. The ARM9 kept executing
  (`cyc9` advancing) for the whole period.
- **Method.** `tools/runwatch.py` now advances on scheduler rounds
  (`run_rounds`), which no guest state can stall, and treats VCOUNT (`0x04000006`)
  and DISPSTAT (`0x04000004`) as ground truth. `run_cycles` is not a safe driver
  either: the ARM9 cycle counter stops while the CPU is halted, so it returns
  `reached=false` on any halted CPU — which is how a cycle-driven attempt ended
  at t≈4.15 s with the raster demonstrably alive (VCOUNT changing, `vblank9`
  228→246 across the probe, `cyc9` +20 M in one call).

### The submodule pointer must record the tag, not `main`

- **Observation.** `git submodule status` prints `+aa40b575… (v0.6.10)`: the
  submodule's checked-out HEAD is tag `v0.6.10`, but the **staged gitlink**
  (`git ls-files -s third_party/ndsrecomp`) is
  `3a57236bb23d25dcb4caad7d58d733311062ff5e`, which is `origin/main` ("Merge pull
  request #26 … hle/mkds-vector-20261004"). The two commits are unrelated —
  neither is an ancestor of the other (`git merge-base --is-ancestor` exits 1) —
  and only `aa40b575` carries the `v0.6.10` tag.
- **Inference.** Committing the gitlink as staged would pin the project to
  rolling `main`, contradicting every place this repo records `v0.6.10` — and
  specifically the licence basis: `v0.6.10` is the last tag whose own code is MIT
  (see above), while `main` is PolyForm Noncommercial. The pointer must record
  `aa40b575b6f68a537f24ff3e4c94b567baa25587` before the first push.
- **Observation.** The same applies to `.gitmodules`: the staged copy has only
  the `third_party/ndsrecomp` entry, while the working tree also carries the
  `third_party/ndsrecomp/external/arm-recomp-core` entry that fresh clones need.

### The runner is LLE, not HLE

- **Observation.** Grepping the runner for `FS_OpenFile`/`FS_ReadFile`/`OS_*`
  finds nothing; `docs/dispatch_architecture.md:103-108` states "No load-bearing
  HLE and no interpreter on the Tier-1 hot path"; `ISSUES.md:569` calls the
  "LLE-faithful floor … untouchable. No stubbed SWIs". The only HLE is opt-in
  per-title CPU-math replacement (`HLE_ARCHITECTURE.md`), driven by a
  `--hle-manifest` whose schema is exactly `version` + `[program]` +
  `[[routine]]` (`recompiler/finder/config.cpp:433-457`).
- **Inference.** No NitroSDK/FS/OS HLE exists or is needed. The game's own
  NitroSDK code is recompiled, and the cart interface is emulated. Our M1
  `nds`/`narc` layer therefore has **no runtime role** — it is an offline
  library (bank generation now; save I/O and launcher detection later). Plan
  M2.2 was rewritten accordingly.

### There is no retail-title precedent

- **Observation.** `README.md:64-66`: "SM64DS, Mario Kart DS, and Prime Hunters
  are separate title repositories with their own exact-ROM gates, generated
  banks, and release status." No SM64DS banks/config/generator exist in-tree;
  `title_patches.cpp` is compiled C++ keyed on hardcoded addresses
  (`runner/src/title_patches.cpp:15`), not a per-title data directory.
  Glob `**/title_patches/**` and `**/registration-order*.txt`: no files.
- **Observation.** `recompiler/finder/config.cpp:2` and `config.h:4` reference
  `docs/TOML_SCHEMA.md`; the file does not exist. The authoritative schema is the
  parser: `[program]` requires `load_address`/`size`/`entry_pc`; `[identity]`
  requires `sha1` (of the exact `--bin` bytes, verified at
  `config.cpp:650-692`); `[[entry_point]]`, `[[data_range]]`, `[[code_copy]]`,
  `[[jump_table]]`, `[[exclude_func]]` are optional.
- **Inference.** We are the first to bank a Gen 5 title; expect bring-up, and
  expect to discover the working config shape ourselves.

### Banking mechanics that constrain the tool

- **Observation.** `recompiler/src/main.cpp:1621-1630`: a `_NN` shard suffix is
  only appended when `shards > 1`. The runner globs bodies as
  `runner/CMakeLists.txt:137-138` `*_[0-9][0-9].c`, so **`--shards 1` produces
  `<bank>.c` and is silently ignored** — minimum `--shards 2`. Every source name
  must also contain `_arm9_`/`_arm7_` (`:256-265`, CPU is inferred from the
  filename), and a group missing bodies *or* dispatch is fatal (`:175-179`).
- **Observation.** Banks are gated by exact ROM SHA-1
  (`NDS_TITLE_ROM_SHA1`, `runner/src/main.cpp:2130-2134`) and the bank directory
  is **compile-time only** — there is no CLI flag. The runner must therefore be
  configured *after* banks exist; bank generation is a pre-configure step.
- **Observation.** `FunctionFinder::run` has a default `max_functions = 4096`
  (`finder/function_finder.h:185`) and `recompiler/src/main.cpp:1431` calls it
  with no argument, but **Black's ARM9 reports 5265 discovered functions**, so
  the cap did not bind on this path. No fork is required (D-question resolved:
  accept). ARM7 reports only **49** functions — most ARM7 code will run through
  the live interpreter.
- **Observation.** Overlays have no static bank pipeline; the runner's design is
  runtime live-overlay compilation with content-validated dispatch rows
  (`docs/overlay-strategy.md:191-215`, `runner/src/live_overlay.cpp`). Static
  per-overlay banks are research-only. We bank ARM9 + ARM7 only.

### ARM7 discovery is thin because the image relocates into WRAM

- **Observation.** Recompiling the ARM7 image from `entry_pc = 0x02380000`
  discovers only **49 functions**; the finder reports 2 unresolvable indirect
  targets in `0x03800000..0x03810000`, and `stored_ptr_seeds=0`.
- **Inference.** `0x03800000` is ARM7 WRAM. The ARM7 image copies part of itself
  into WRAM and dispatches there — the same "ARM7 runtime-RAM bank" pattern the
  recompiler's own notes describe for SM64DS. Discovery cannot follow a code copy
  nobody declared, so most ARM7 code falls through to the live interpreter.
- **Unknown.** Whether interpreter speed is enough for the ARM9↔ARM7 FIFO
  handshake to complete in usable time. The config-level fix, if it is not, is to
  declare the copy (`[[code_copy]]`, or `[[data_range]]` plus `[[entry_point]]`
  seeds in a per-title hint file) and regenerate. Deliberately deferred until a
  first boot shows whether it matters.

### The 2D path is further along than its own issue tracker says

- **Observation.** `docs/accuracy_burndown.md:275-299` claims 2D is "pixel-exact
  and full speed on the exercised firmware paths" but lists windows as
  unchecked, and `ISSUES.md:553` says Win0/Win1/OBJ-window/WINOUT are
  "unimplemented". `runner/src/gpu2d_window.h:34-52` implements all of them and
  `gpu2d.cpp:1145-1172` calls into it.
- **Observation.** Main-memory display FIFO/capture is genuinely missing
  (`ISSUES.md:554`; `gpu2d.cpp:1201,1352,1979`).
- **Observation.** `gpu2d_window_test` — the suite for exactly this code — exits
  `0xC00000FD` (`STATUS_STACK_OVERFLOW`) when run directly in this Debug build.
  `runner/CMakeLists.txt:728-733` gives `nds_runner` a 64 MB stack
  (`-Wl,-stack_size,0x4000000`) *because* the dual-CPU boot nests deeply, but the
  test target is not given that flag. So the "all of them implemented" claim rests
  on the source, not on a passing on-Windows test.
- **Observation.** `live_index_lock_test` (Python-driven, driven from
  `runner/CMakeLists.txt:1084-1086`) fails in this environment with
  `index writer failed with 1`. Unrelated to the banks.
- **Inference.** A 2D boot logo is plausibly renderable; the docs' issue list is
  stale, so treat runtime behaviour as the authority.

### Build shape

- **Observation.** ndsrecomp has **no root `CMakeLists.txt`** — `runner/` and
  `recompiler/` are standalone projects. The runner resolves SDL3 with
  `find_package(SDL3 CONFIG REQUIRED)` (`runner/CMakeLists.txt:693`) with no
  FetchContent, so a parent configure must supply SDL3. Neither CMakeLists
  references `CMAKE_SOURCE_DIR`/`PROJECT_SOURCE_DIR`, so `add_subdirectory` is
  safe.
- **Observation.** `docs/BUILD_PERFORMANCE.md:38-39` records a reference corpus
  of 466 generated C files / ~1.32 GiB of source. Our Black ARM9 + ARM7 banks are
  ~33 MB across 16 body shards + 2 dispatch files — three orders of magnitude
  smaller, so the build is not the risk the docs imply.

### Generated artifacts (this session)

| Bank | Source | Load | Entry | Functions | Shards |
| --- | --- | --- | --- | --- | --- |
| `irbo_arm9_main` | `0x6F898` BLZ → `0xA67C0` | `0x02004000` | `0x02004800` | 5265 | 8 |
| `irbo_arm7_main` | `0x28F84` raw | `0x02380000` | `0x02380000` | 49 | 8 |

Hashes (SHA-1), independently re-verified with an external tool:

- ROM `26ad0b9967aa279c4a266ee69f52b9b2332399a5`
- decoded ARM9 `fd4edf3f83b602e79e41ccaaec2270364178a4dd`
- decoded ARM7 `364259352fffdcd15b75c2d9e997d2ed0d877190`

### First boot of Black (2026-10-16)

Verified command (repo root; the two positionals are the BIOS scratch directory
then the ARM9 cycle budget):

```
build/windows-debug/third_party/ndsrecomp/runner/nds_runner.exe \
    bios 250000000 --boot direct --generated-firmware --freebios --no-save \
    --rom "roms/<your-rom>.nds"
```

- **Observation.** Without `--freebios` the run aborts before executing anything:
  `[load] arm9 bios: missing/empty`, `[load] arm7 bios: missing/empty`,
  `refusing to start: dump verification failed`. With it, the run proceeds.
  `main.cpp:1565-1572` and `1573-1581` show `--freebios` and
  `--generated-firmware` each require `--boot direct`; only retail BIOS +
  firmware dumps unlock `--boot lle`.
- **Observation.** Bank registration works at runtime:
  `[dispatch] registered configured title banks for
  26ad0b9967aa279c4a266ee69f52b9b2332399a5`, then
  `Dispatch arm9: total=2615052` / `arm7: total=1545427`. The generated banks
  are live, not dead code.
- **Observation.** At 250 000 000 ARM9 cycles (~7.5 s of emulated time,
  2.49 M scheduler rounds, ~2491 s wall) the run is at **~0.75 % of real-time**:
  - ARM9 `PC=0x02004E3C`, `LR=0x02079114`, ARM mode, inside an SDK copy/expand
    routine spanning `0x02004E28-0x02004E74` (disassembled from
    `banks/black/bin/irbo_arm9_main.bin` with `llvm-objcopy -I binary` +
    `llvm-objdump`). It is doing real work, not a hard hang: the destination
    pointer advanced from `0x02159F20` (at 30 M cycles) to `0x021603E0`.
  - ARM7 `PC=0x00001158` (ARM7 BIOS), `IF=0x00000000`, `IE=0x01040099`: idling in
    BIOS on an interrupt, stack already set up at `0x0380FB68` (ARM7 WRAM),
    `LR=0x037F80E8`.
  - GPU2D composited 42816 scanlines but reported `A text[0..4]=0/0/0/0/0`,
    `B=0/0/0/0/0`, `direct 0 frames`: **no BG or OBJ layer was ever enabled**, so
    nothing is displayed. M2.3's logo gate is **not** met.
- **Observation.** The cost is ARM7-dominated: `Scheduler profile: 28.794 ms/1000
  rounds (ARM9 4.789, ARM7 21.065, devices 1.599)`, with the ARM7 dispatcher at
  `cache_hit/absent/slow=186482/37107038/7598` (37.1 M absent lookups).
- **Observation.** The coverage manifest
  (`roms/...-coverage-20261016-015352-part00.json`) reports `tier3_entries9=80329`,
  `tier3_entries7=1374176`, `tier3_insns9=3173756`, `tier3_insns7=34366147`.
  A large share of executed code falls through to the tier-3 interpreter - some
  of it at ARM9 addresses *inside* the bank's own `0x02004000-0x020AA7C0` range
  (e.g. `0x02005640`, thumb, 201 hits).
- **Inference (medium).** The ARM7 stays in BIOS because `--boot direct` does not
  execute the firmware (`[boot] direct boot (opt-in): cartridge entry state
  established, firmware not executed`), and its wait loop needs a VBlank IRQ
  that only appears once the ARM9 enables the display - which it has not done
  yet. The two observations are therefore consistent with ordinary early boot,
  not a proven deadlock.
- **Inference (medium).** Reaching game-drawn logos needs the ARM9 to finish
  megabytes of SDK/file-system init *and* to run overlay code (Gen 5 game logic
  lives in `ovNNN`). With most of that interpreted, the current speed puts the
  logos well outside this session.
- **Unknown.** Why so much in-range ARM9 code falls to tier-3; whether the
  live-overlay JIT (`--live-overlay-enable --live-overlay-command gcc`; the
  LLVM-MinGW `gcc` wrapper exists on this machine) changes the picture; whether
  an ARM7 `[[code_copy]]`/WRAM declaration materially cuts the interpreter load.

Confidence: high on all observations (read from source or produced by the tool
this session). The boot above resolves "the correct boot flags for this title"
(`--boot direct --generated-firmware --freebios`); the M2.3 expectation is now a
measured negative. Unknowns: whether the 49-function ARM7 bank slows the FIFO
sync below usable speed; whether the emulated cart path suffices for the game's
own file reads; why in-range ARM9 code falls to tier-3.

### Re-measurement (2026-10-16, same day) — supersedes the numbers above

Same flags, same (Debug) configuration; logs in `banks/bench-*.log`.

| ARM9 budget | wall | scheduler emulation | marginal cyc/s | % of 67.03 MHz |
| --- | --- | --- | --- | --- |
| 10,000,000 | 53.2 s | 1.5 s | — | 0.28 % |
| 30,000,000 | 60.3 s | 4.4 s | — | 0.74 % |
| 250,000,000 | 112.8 s | 59.4 s | **4.19 M** | **6.26 %** |

- **Observation.** "~2491 s wall" above is a ~22× error: the same scenario takes
  **112.8 s**, and the log's own profile figure was the closer one. "~0.75 % of
  real-time" describes a ~30 M run, not a 250 M one. Marginal (steady-state)
  throughput is ~6.3 % of real-time.
- **Observation.** ~52 s of *every* run is a fixed cost incurred before the
  scheduler starts: ~35 s reading the 256 MiB ROM through
  `std::istreambuf_iterator` (`runner/src/main.cpp:141-146`; ~7.3 MiB/s against
  803 MiB/s for the same read via the OS) and ~12 s in the runner's hand-rolled
  SHA-1 over it (`main.cpp:1695`; ~21 MiB/s). Two host-side operations that
  should together be well under 2 s.
- **Observation.** Every runner and bank object in this build was compiled
  **`-O0`** (`build/.../nds_runner.dir/src/main.cpp.obj: ...
  CXX_COMPILER__nds_runner_unscanned_Debug`, `FLAGS = -g ...`, no `-O`), so the
  absolute magnitude of every figure here is inflated. Nothing in these numbers
  separates "Tier 3 is inherently slow" from "this build is unoptimized".
- **Observation (corrects the entry above).** The ARM7 is **not** idling in BIOS
  as an interpreted loop. `freebios_arm7` *is* registered under `--freebios` and
  carries an exact dispatch row for `0x00001158`
  (`generated/freebios_arm7_dispatch.c`), and the ARM7's Tier-3 histogram holds
  **zero** BIOS entries. Its cost is **unbanked WRAM code**: 1.27 M of its
  1.374 M Tier-3 entries are ARM7 WRAM (`0x0380xxxx`, hottest `0x03806BA4`) and
  the shared-WRAM view (`0x037Fxxxx`, hottest `0x037FE558`), which is 71 % of the
  250 M run's scheduler time.
- **Observation.** The hottest in-range ARM9 Tier-3 PC is `0x02019A7C` (thumb,
  `call`, 91,917 hits), which appears **nowhere** in
  `banks/black/irbo_arm9_main_dispatch.c` — a **discovery** gap, not a
  relocation one. 192 of 210 ARM9 Tier-3 entry points lie inside the banked
  range (`0x02004000-0x020AA7C0`).
- **Inference.** The two coverage gaps are distinct and both config-level:
  relocated ARM7 code needs `[[code_copy]]`/seeds; undiscovered in-range ARM9
  entry points need seeded `[[entry_point]]`s. Feeding the coverage manifest
  back into `pkmn_bankgen` closes the second gap with no recompiler fork.
- **Unknown.** How much emulated time the ARM9 needs to reach the first enabled
  layer. At 250 M cycles (3.7 s of DS time, not the 7.5 s claimed above) it is
  still inside one banked SDK expand routine (`0x02004E28`, caller
  `LR=0x02079114`) having emitted ~25.8 KiB, and no BG/OBJ layer has been
  enabled at any point. Whether that routine is legitimately that expensive or
  pathologically slow in emulated time is unresolved.

