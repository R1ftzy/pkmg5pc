# Plan.md

Roadmap for **pkmnbwb** — a native PC (Windows/Linux) reimplementation of
Pokémon Black/White and Black 2/White 2, driven by user-supplied ROMs.

This file is the source of truth for *what we build, in what order, and why*.
It intentionally separates **committed** milestones from **open** decisions.
Items marked **(open)** must be proposed before implementation (see `AGENTS.md`).

---

## 0. Problem statement

Four Gen 5 ROMs exist:

- Black / White (NDSi Enhanced, 256 MiB) — "BW"
- Black 2 / White 2 (NDSi Enhanced, 512 MiB) — "B2W2"

They share a Nitro/NitroFS base and much of the Game Freak engine, but differ in
overlays, data, and content. We want **one PC program** that:

1. Loads any of the four ROMs the user supplies.
2. Plays them with correct behavior (overworld, battles, menus, save).
3. Presents both generations from **one launcher**, with cross-version content
   behind a setting.
4. Ships **no copyrighted content**.
5. Later, adds modern rendering and New Game+ features without rewriting game
   logic.

This is a multi-year effort. The plan below front-loads de-risking: prove the
toolchain and a hardware abstraction layer on a vertical slice before
committing to a full-game strategy.

---

## 1. Hard constraints

- No ROM, save, or extracted asset is ever committed (`AGENTS.md` §2).
- Enhancement features (16:9, shadows, 60 fps, NG+) are **out of scope** until
  the core port milestone (M6) is met.
- Game logic never touches the GPU/OS directly; all hardware goes through the
  HAL.

---

## 2. Strategy (the central open decision)

There are three viable ways to get Gen 5 running natively. They are not equally
cheap, and the right choice depends on what the RE phase finds.

| Option | How | Pros | Cons |
| --- | --- | --- | --- |
| **A. Matching decompilation** | Find/adopt an existing pret-style decomp of BW/B2W2, compile it natively, implement platform layer | Highest fidelity, maintainable, easy modern rendering | Depends on a decomp existing and matching; large up-front |
| **B. Static recompilation** | Recompile ARM9/ARM7 machine code to C/LLVM, reimplement hardware | No decomp needed; byte-exact logic | Complex recompilation, hard to read/modify, HW fidelity work |
| **C. Engine reimplementation** | New engine reads the ROM's data files and reproduces logic | Full control, clean code | Reproducing an entire commercial engine by hand is enormous |

**Decision (D1, resolved 2026-10-14): strategy B — static recompilation.**

Evidence: no complete Gen 5 decompilation exists. Gen 1–4 decomps are mature
(pret), but Gen 5 is early: `pret/pokeblack`, `squiddonaut/pokeblack`,
`pokemodding/pokeblack` are partial disassembly efforts and `PlatinumMaster/
pokewhite2` "does not build any executables." By contrast, DS static
recompilation is proven feasible (`mstan/ndsrecomp` recompiles ARM9/ARM7 to C
and reaches the firmware main menu; `AetiasHax/ds-decomp` provides delink/
Ghidra/objdiff tooling). Therefore we recompile the games' ARM9/overlay/ARM7
code to C++ and implement the hardware as a HAL.

- **Reuse over reinvention:** adopt an existing NDS static recompiler as the
  recompiler + runner rather than writing one from scratch. **Decided (D7):** adopt
  `RetroPortingToolKit/ndsrecomp` pinned at tag `v0.6.10` **as-is**, keeping its
  melonDS-derived 3D GE (software + compute) and Wi-Fi device models, and
  license this project **GPL-3.0-or-later** to match. (`main` is PolyForm
  Noncommercial — unusable; the melonDS subtrees are GPL-3.0, the fork's own
  code at `v0.6.10` is MIT and GPL-compatible.) Recorded in
  `re/notes/recompilation.md`.
- **Method family:** this project is the **static-recompilation** family — the
  same as **Sonic Unleashed Recompiled** and **Zelda 64: Recompiled**
  (`N64Recomp`), *not* the decompilation-port family of **Ship of Harkinian**.
  A decomp port would need a full matching Gen 5 decompilation, which does not
  exist; a Ship-of-Harkinian-style path is only reachable later via the HLE
  layer.
- **Not an emulator:** we do not interpret/emulate the game at runtime. Game
  code is statically recompiled and linked natively; only hardware is
  reimplemented (HAL). There is no shipped emulator core. The unavoidable
  "runtime" that services memory-mapped hardware registers is a port, not an
  emulator.
- Revisit strategy **A** only if a matching Gen 5 decomp matures; the recompiled
  code and HAL would remain reusable.
- Option **C** alone is not planned.

Regardless of choice, the **HAL + renderer** work is shared.

---

## 3. Milestones

Legend: `[ ]` pending, `[~]` in progress, `[x]` done. Each milestone lists
**acceptance criteria** (definition of done).

### M0 — Foundation & RE toolchain — **status: done**

- [x] Initialize git repo, `.gitignore` (ROMs, saves, extracted, build, idb).
      (Repo initialized; commits are left to the maintainer.)
- [x] CMake skeleton + `CMakePresets.json` for `windows-*` and `linux-*`.
- [x] CI: build + `ctest` on both OSes (asset-free fixtures only).
- [x] `.clang-format` / `.editorconfig` / license (**GPL-3.0-or-later**) +
      `NOTICE` stating the program ships without ROM content.
- [x] REA/Ghidra note templates under `re/notes/`.

**Done when:** `cmake --preset linux-debug && ctest` passes on an empty tree
with a placeholder test. — **met** (verified locally on `windows-debug`; CI
covers Ubuntu and Windows).

### M1 — ROM & asset extraction pipeline (`tools/`) — **status: in progress**

- [x] `romdump`: parse NDS header (title, game code, ARM9/ARM7 offsets/sizes/
      entries, overlay tables, FNT/FAT) for all four ROMs.
- [x] `romdump --extract <dir>`: write the full inventory + a SHA-256 manifest
      (`manifest.sha256`, `sha256sum` format). Verified deterministic across
      runs on Black (54539 files, 0 skipped).
- [x] NitroFS: walk FNT/FAT (directory main-table + sub-tables), list files.
- [x] NitroFS: extract by path into `extracted/` (`nitrofs/<path>`).
- [x] Decompressors: LZ10 (`0x10`), LZ11 (`0x11`), Huffman4 (`0x24`),
      Huffman8 (`0x28`), RLE (`0x30`) — round-trip unit-tested.
- [x] BLZ ("bottom LZ"): decompress ARM9 and ARM9 overlays. All 229 compressed
      BW overlays and the ARM9 binary decode to their exact declared sizes.
- [ ] Decompressor: Huffman `0x20` variant (low priority; not observed yet).
- [x] NARC: parse header/`BTAF`/`BTNF`/`GMIF`, enumerate members (Black: 237
      archives, 54054 members; empty-BTNF and named-BTNF both handled).
- [x] NARC: extract members into `extracted/` (`narc/<archive>/<id>`, nested
      containers recursed as `<id>.narc` + `<id>.d/`).
- [x] Overlays: read per-ROM overlay table, decompress, verify size vs RAM.
- [x] Overlays: dump decoded `ovNNN.bin` into `extracted/`.
- [x] SHA-256 (`pkmn::sha256`): portable, known-answer tested; used for the
      extraction manifest.
- [ ] Asset parsers: NCGR/NCLR/NSCR/NCER/NANR (2D), NSBMD/NSBTX/NSBMA/NSBTA
      (3D), SDAT→SSEQ/SBNK/SWAR/SWAV/STRM (audio) (not started).
- [ ] Identify NARC member formats (sniff magic: `RGCN`/`RLCN`/`BMD0`/`SDAT`/…).
- [x] ROM-format schemas documented in `re/notes/header.md`,
      `re/notes/overlays.md`, and `re/notes/nitrofs.md`.

**Next up (M1 remainder):** identify NARC member formats (sniff member magic),
then implement the 2D/3D/audio asset parsers on top of the extracted tree. This
unblocks M2's asset/decomp investigation.

**Done when:** a fresh checkout, pointed at a user ROM path, produces a complete
`extracted/` inventory whose SHA-256 manifest matches across repeated runs, and
`tests/` covers every decompressor round-trip.

### M2 — Faithful boot to the main menu (static recompilation)

The goal of M2 is the real thing: run the game's own code (statically
recompiled) against the HAL until **Black's title screen → press Start → main
menu** appears. Target: push through to the 2D logo/menu visuals (M2.3+);
Windows and Linux are developed in lockstep. BIOS is flexible (HLE preferred,
user-supplied BIOS optional). Logos are 2D, so a visible win precedes the 3D GE.

##### M2.0 — Strategy + RE setup

- [x] Evaluate decomp availability; resolve D1 as **B** (§2).
- [~] Stand up static analysis: `objdump` entry/crt0 pass done (`arm9-init.md`);
      Ghidra project for `arm9.bin` (base `0x02004000`) + overlays still to do;
      symbol map seeded at `re/symbols/black-arm9.json`.
- [x] Locate ARM9 entry (`0x02004800`), crt0, and the THUMB SDK main
      (`0x02005124`); recorded in `re/notes/arm9-init.md` with Evidence IDs.
- [x] Select the recompiler + runner (D7): adopt `ndsrecomp` @ `v0.6.10`
      **as-is** (melonDS 3D GE + Wi-Fi retained); license this project
      **GPL-3.0-or-later**. Method = the recomp family (Sonic Unleashed
      Recompiled / Zelda64Recomp). Recorded in `re/notes/recompilation.md`.

##### M2.1 — Execution substrate (headless)

- [~] Recompile ARM9 + overlays (and the ARM7 image) to C++ with a runner that
      keeps original RAM addresses; live-code fallback path for undecoded/mutable
      code. **ARM9 + ARM7 static banks now generate, link, and are wired into the
      build** (`pkmn_bankgen` → `banks/<title>/`, `PKMNBWB_BUILD_RUNNER`); see
      `re/notes/recompilation.md`. Overlays are deliberately left to the runner's
      live-overlay path. Discovery is uneven: ARM9 yields 5265 functions, ARM7
      only 49, so most ARM7 code runs interpreted.
- [x] Memory bus, CP15/TCM, WRAM, IRQ + timers, DMA, IPC/FIFO, SPI/firmware
      defaults; a **reimplemented BIOS** (FreeBIOS, `--freebios`) so no retail
      BIOS dump is required. Supplied by the adopted runner; now exercised by a
      real boot of Black (`re/notes/recompilation.md`).
- [x] **Gate:** ARM9 runs crt0 → `NitroMain` with correct register/RAM state.
      Observed: the ARM9 executes past `NitroMain` (`0x02005124`) into NitroSDK
      code at `0x02079xxx`, and the game programs CP15 DTCM itself.

##### M2.2 — Filesystem + OS (LLE, not HLE)

The adopted runner does **not** HLE the NitroSDK. It refuses "load-bearing HLE"
(`ndsrecomp` `docs/dispatch_architecture.md`), recompiles the game's *own*
NitroSDK code, and emulates the cartridge interface instead. There is no FS
layer to write: the game's file calls are recompiled code talking to an
emulated cart. Our `rom/` layer is therefore an **offline** library (bank
generation, M6 save I/O, M7 launcher detection), not a runtime component.

- [ ] Confirm the emulated cart path serves the game's own reads; no HLE seam.
- [ ] **Gate:** the game loads its own data files (e.g. `gfl_font.dat`,
      `skb.narc`).

##### M2.3 — Video HLE + renderer (first M2 visual target)

- [~] Map VRAM/PAL/OAM/BG registers; implement the DS 2D compositor
      (text/affine/bitmap BG, OBJ, windows, blending, master brightness).
      Supplied by the adopted runner, which documents 2D as pixel-exact on the
      firmware paths it exercises and does implement windows (its own
      `ISSUES.md` listing them as unimplemented is stale). Display
      FIFO/capture is genuinely missing.
- [~] Bring up the SDL3 window + render backend (both OSes) and present both
      256×192 screens. CMake fetches SDL3 (3.4.18, static) and the runner builds
      and runs against it; `--interactive` opens the window. Not yet shown with
      visible ROM output.
- [ ] GE (`GXFIFO`) stubbed initially; enough later for the title scene.
- [ ] **Gate:** boot splash / logos render on both screens. **Not met.** A
      250 M-cycle direct boot never enables any BG/OBJ layer (GPU2D reports
      `A text[0..4]=0/0/0/0/0`, `direct 0 frames`) and runs at ~0.75 % of
      real-time. See `re/notes/recompilation.md`.

##### M2.4 — Input, timing, audio

- [ ] Scanline/vblank + IRQ timing; key input + touch through the HAL.
- [ ] SDAT/SSEQ playback via the ARM7 sound path.
- [ ] **Gate:** intro/title runs with input and music.

##### M2.5 — Reach the main menu

- [ ] Iterate remaining subsystems until title → Start → menu text renders.
- [ ] **Gate:** **Black's main menu displays and is navigable. This is the first
      real de-risking gate.**

**Done when:** Black boots from the user's ROM to its navigable main menu on
Windows and Linux, entirely through recompiled game code + the HAL (no emulator
core, no direct OS/GPU calls from game code).

### M3 — Overworld vertical slice

- [ ] Map system: matrix + headers + area data + object/event tables loaded from
      ROM (`re/notes/map-system.md`).
- [ ] DS 2D engine: BG layers (NSCR+NCGR+NCLR) and OBJ sprites composited.
- [ ] DS 3D GE emulation: NSBMD/NSBTX draw for outdoor maps; camera.
- [ ] Script VM: interpret at least movement/lock/release/warp commands
      (`re/notes/script-vm.md`).
- [ ] Text system: render a message box from the ROM text archive
      (`re/notes/text-system.md`).

**Done when:** you can boot a save (or new game), render a town route, walk with
the D-pad, trigger a warp, and read a text box — all through the HAL.

### M4 — Battle system

- [ ] Pokémon/move/item/trainer data loaders (`re/notes/pokemon-data.md`).
- [ ] Battle flow: wild + trainer battles, turn resolution, damage calc, AI.
- [ ] Battle presentation: sprites, HP bars, move FX (DS GE/2D).
- [ ] Audio: SSEQ playback for battle music, SFX via the ARM7 audio HAL.

**Done when:** a full wild and trainer battle can be fought to completion with
correct results and sound.

### M5 — Full game systems

- [ ] Menus (party, bag, Pokédex, PC, options, map).
- [ ] Overworld events, items, encounters, trainers, evolution, trades.
- [ ] Entralink / Driftveil / post-game hooks as reachable (best effort).
- [ ] B2W2-specific content paths and engine deltas.

**Done when:** a save can be played for a long session across maps with menus,
items, and battles working.

### M6 — Save compatibility (**core port complete**)

- [ ] Byte-exact `.sav`/`.dsv` read/write (0x24000, block rotation + checksums).
- [ ] Import existing save, play, export, reload in emulator/hardware.
- [ ] Golden round-trip tests (synthetic fixtures, no real save committed).

**Done when:** import → play → export produces a byte-identical save and the
in-game load accepts the exported file. **Enhancements may begin after this.**

### M7 — Launcher & multi-ROM

- [ ] Launcher UI: pick ROM, show detected title/game code.
- [ ] One binary drives all four games; load the right title profile per ROM.
- [ ] Cross-version content toggle (setting) — define exactly which data swaps.

**Done when:** all four ROMs launch and play from one menu.

### M8 — Modern rendering (post-M6)

- [ ] Widescreen/16:9 with correct dual-screen layout and safe areas.
- [ ] Resolution scaling (render at target res, HUD in logical pixels).
- [ ] Post-FX chain (bloom, color, AA, etc.) on the composited frame.
- [ ] Optional: shadow maps for GE 3D scenes.
- [ ] Native 60 fps overworld decoupled from game logic timing.

**Done when:** each effect is toggleable and off by default; enabling them does
not change game logic results (verified by save/frame comparisons).

### M9 — New Game+ / New Game carry (post-M6)

- [ ] NG+: restart with harder gym leaders/trainers while retaining the party.
- [ ] Cross-title carry: finish Black → start Black 2 with the Black team.
- [ ] Carry survives the `.sav` format (extend a sidecar, don't corrupt saves).
- [ ] Difficulty scaling table (levels, AI, items) configurable.

**Done when:** a completed save can start either a harder same-title run or a
cross-title B2W2 run with the previous party.

---

## 4. Cross-cutting concerns

- **Determinism:** game logic must be frame-rate independent; enhancements read
  state, they don't mutate timing.
- **Fidelity harness:** record (state, input) → expected result fixtures so the
  port can be diffed against the emulated original.
- **Performance budget:** target 60 fps at 1080p baseline on mid-range hardware;
  profile the compositor early.
- **Localization:** text pulled from the ROM at runtime; UI strings from a small
  in-repo table (no ROM text committed).

---

## 5. Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| No usable Gen 5 decomp | Strategy A collapses | **Resolved:** strategy B chosen (M2.0) |
| Recompiler coverage / undecoded or self-modifying code | Wrong behavior, crashes | Live-code fallback; trace diff vs reference during bring-up |
| Recompiler/bring-up tool license | Can't ship or vendor | **Resolved:** adopt `ndsrecomp` as-is under GPL-3.0-or-later (D7) |
| DS 3D GE fidelity (fixed-point, polygon quirks) | Visual/behavioral bugs | Unit-test GE math; compare against emulator frames |
| Scale of full-game logic | Years of work | Vertical slices; ship overworld before battle |
| Save format misread | Data loss for users | Golden round-trip tests; never ship without M6 |
| Legal exposure | Project death | `AGENTS.md` §2; ROM-at-runtime only |
| Enhancement regressions | Broken base game | Effects off by default; logic/visual separation |

---

## 6. Open decisions

- **D1:** Strategy A vs B vs hybrid (§2) — **resolved: B (static
  recompilation)**, 2026-10-14.
- **D2:** Renderer abstraction: hand-rolled RHI vs bgfx vs sokol.
- **D3:** C++20 (default) vs Rust for new isolated components.
- **D4:** Which cross-version content is toggleable in M7, and default state.
- **D5:** Scope of B2W2 post-game/Entralink for the initial port.
- **D6:** Whether NG+ difficulty is data-only or adds new AI.
- **D7:** Recompiler source — **resolved: adopt `ndsrecomp` @ `v0.6.10` as-is
  (melonDS 3D GE + Wi-Fi retained), license this project GPL-3.0-or-later**
  (2026-10-14; supersedes the earlier "strip GPL, keep MIT" decision). Method
  family = static recompilation (option 1: faithful floor).
- **D8:** BIOS handling — **resolved: HLE SWIs via `freebios` (BSD-2) by
  default**; a user-supplied BIOS is optional, never shipped.

---

## 7. References (no content embedded)

- NDS hardware: GBATEK; melonDS/DeSmuME as behavioral references.
- Nitro formats: Nitro System docs, Tinke, lowlines' Nitro tooling.
- Gen 5 data: DSPRE (open source) as a format reference.
- Decomp ecosystem: pret org decomps (Gen 3–4); Gen 5 efforts
  (`pret/pokeblack`, `squiddonaut/pokeblack`, `PlatinumMaster/pokewhite2`) —
  early, not yet buildable.
- DS static recompilation: `mstan/ndsrecomp` (ARM9/ARM7 → C, SDL host),
  `Myth7x/ndsrecomp` (ROM → native C/CMake project), `AetiasHax/ds-decomp`
  (delink/Ghidra/objdiff tooling).

Do not vendor or copy their content; cite and reimplement.

---

## 8. Progress log

Newest first. Keep entries short; details and evidence live in `re/notes/`.

- **2026-10-16 (M2.3 attempt: Black boots, logos do not render yet)** — the
  runner is in the build and Black boots: the banks register against the ROM
  SHA-1, both CPUs execute, and the ARM9 passes `NitroMain` into NitroSDK init
  (so the M2.1 gate holds). The M2.3 gate does **not**: after 250 M ARM9 cycles
  (~7.5 s emulated, ~0.75 % of real-time) no BG/OBJ layer is ever enabled, so
  nothing is displayed. Cost is ARM7-dominated — the ARM7 idles in BIOS with
  1.37 M tier-3 interpreter entries. Boot command, measurements and unknowns are
  in `re/notes/recompilation.md`. No push (per the no-push-until-logos rule).

- **2026-10-16 (M2.1 substrate: runner in the build + Black title banks)** —
  Black's ARM9/ARM7 banks now generate and the adopted runner is part of the
  build (`PKMNBWB_BUILD_RUNNER`); overlays are left to the runner's live path.
  Two findings re-scoped the plan: the runner HLEs **no** NitroSDK/FS/OS surface
  (it refuses load-bearing HLE and emulates the cart instead), so **M2.2 is LLE,
  not HLE** and `rom/` is an **offline** library; and there is **no retail-title
  precedent** in `ndsrecomp` — we are the first. Details and evidence in
  `re/notes/recompilation.md`.

- **2026-10-16 (review + course corrections)** — reviewed the previous session's
  work and ratified it with the maintainer: **strategy stays static
  recompilation**, and **GPL-3.0-or-later is adopted deliberately** (we reuse
  the melonDS 3D GE + Wi-Fi; melonDS is GPL-3.0-or-later and no permissive DS 3D
  renderer exists). Decisions recorded in `docs/adr/0001-static-recompilation.md`
  and `docs/adr/0002-gpl-adoption.md`; language pillared in `GLOSSARY.md`.
  The previous `engine/` library is the **ROM & Nitro format layer**, not the
  game engine: renamed `engine/` → `rom/`. Removed the 332 MB `extracted/` tree
  (regenerable from the ROM) and the `temp.md` scratch. Still open: wiring
  `ndsrecomp` into the top-level build (today only `rom/`, `tools/`, `tests/`
  build). M2.1 next: install SDL3 (fetch from github.com —
  `raw.githubusercontent` is blocked), generate the Black ARM9/overlay/ARM7
  title banks (`NDS_TITLE_BANK_DIR`; needs a recompiler config for arm9 + 237
  overlays), then attempt a headless boot.

- **2026-10-14 (ndsrecomp submodule + builds)** — `ndsrecomp` @ `v0.6.10` is a
  **pinned git submodule** at `third_party/ndsrecomp` (`aa40b575`). Fresh-clone
  bootstrap: `tools/bootstrap_ndsrecomp.ps1` / `.sh` — inits the submodule +
  nested `arm-recomp-core`/`freebios`, fixes the broken relative
  `arm-recomp-core` URL (also recorded as a parent `.gitmodules` override),
  stages `toml.hpp` from `marzer/tomlplusplus` v3.4.0 (`raw.githubusercontent`
  is blocked), generates the FreeBIOS + placeholder retail BIOS banks, and
  builds. **Recompiler builds** (`nds_recompile`); `ctest` 5/5. **Runner
  builds** (`nds_runner`, headless, `NDS_SDL_BACKEND=NONE`,
  `NDS_BOOTSTRAP_FIRMWARE=ON`); `ctest` 32/33 (`gpu2d_window_test` =
  `STATUS_STACK_OVERFLOW`, a Windows stack-size quirk in the unwind test). The
  kept melonDS **3D GE (GPU3D.cpp, GPU3D_Soft.cpp, Savestate.cpp)** and **Wi-Fi
  (Wifi.cpp, WifiAP.cpp, net/, libslirp)** + bridges (`gpu3d.cpp`,
  `wifi_net.cpp`) all compile — validating the GPL adoption. Retail-BIOS banks
  are placeholders built from the FreeBIOS images (runtime uses FreeBIOS by
  default). Blocked on boot: retail `firmware.bin` banks, Black title banks,
  SDL3.

- **2026-10-14 (license migration)** — switched this project to
  **GPL-3.0-or-later** (replaces the earlier MIT decision). Adopt `ndsrecomp` @
  `v0.6.10` **as-is**: keep its melonDS-derived 3D GE (software + compute) and
  Wi-Fi. No melonDS/GPL stripping and no 3D/Wi-Fi stubs. `LICENSE` rewritten,
  `NOTICE`/attribution updated. GPL is confined behind `gpu3d.h` / `wifi.h`;
  the runner core stays clean for later re-forking if ever desired.

- **2026-10-14 (M2.0, D7/D8 resolved)** — recompiler + runner chosen: adopt
  `ndsrecomp` @ **`v0.6.10`** (**superseded by the GPL migration above**; `main`
  is PolyForm Noncommercial) (**option 1: faithful floor**). Method family = static recompilation (**Sonic Unleashed Recompiled /
  Zelda64Recomp**), not decomp-port (**Ship of Harkinian** — no Gen 5 decomp).
  Graphics + sound come from the runner (3D GE + ARM7 sound), not stubbed.
  D8: BIOS HLE via `freebios` (BSD-2). See `re/notes/recompilation.md`.

- **2026-10-14 (M2.0)** — boot analyzed; recompiler tooling decided.
  - `re/notes/arm9-init.md`: located `_start` @ `0x02004800` (NitroSDK crt0:
    IME off, CP15 init, `0x01FF8000` stub copies, per-mode stacks, VBlank wait)
    and THUMB `NitroMain` @ `0x02005124` (entered via `0x02005125`, return-to-
    BIOS `0xFFFF0000`). Evidence IDs `A9-*`.
  - `re/notes/recompilation.md` (D7): initially proposed a minimal in-house
    recompiler; **superseded** — adopt `ndsrecomp` @ `v0.6.10`. `main` is
    PolyForm Noncommercial (unusable); `Myth7x/ndsrecomp` is unlicensed;
    `freebios` (BSD-2) is usable. License posture later migrated to
    GPL-3.0-or-later (see newer entry). D8 = BIOS HLE.
  - Seeded `re/symbols/` (format README + `black-arm9.json`).
  - Disassembly harness: wrap decoded `arm9.bin` as an ARM ELF via
    `llvm-objcopy`, disassemble with `llvm-objdump` (ARM + THUMB).

- **2026-10-14 (M2 plan)** — D1 resolved as **static recompilation (B)**.
  - §2 rewritten: Gen 5 decomps not usable; DS static recompilation proven
    viable. Reuse an existing recompiler where license-permitted; no emulator
    core is shipped.
  - M2 split into **M2.0–M2.5** (RE setup → execution substrate → FS/OS HLE →
    video HLE/renderer → input/timing/audio → main menu). First visual target is
    M2.3 (2D logos); Windows and Linux in lockstep.
  - New open decisions **D7** (recompiler source) and **D8** (BIOS handling).

- **2026-10-14 (later)** — M1 extraction pipeline landed.
  - `engine/`: added `sha256` (known-answer tested) and `extract` module
    (`to_directory`, `manifest_text`); `narc::file_data` now returns empty
    members instead of an error.
  - `tools/romdump`: `--extract <dir>` writes `nitrofs/<path>`, decoded
    `overlays/ovNNN.bin`, `arm9.bin`, expanded `narc/...` members, plus a
    `manifest.sha256` (`sha256sum` format). `--narc-limit N` caps expansion.
  - Verified on Black: 54539 files, 0 skipped; full manifest is byte-identical
    across two runs and independently re-hashed with an external SHA-256 tool.
  - Tests: `sha256_known_vectors`, `extract_manifest_deterministic` (extracts a
    subset twice and re-verifies every hash/size). All green.
  - M1 remaining: NARC member format identification and the 2D/3D/audio parsers.

- **2026-10-14** — M0 complete; M1 parsing/codecs landed.
  - Foundation: CMake presets (win/linux × debug/release), warnings target,
    clang-format/editorconfig, GPL-3.0-or-later `LICENSE` + `NOTICE`, REA note
    templates.
  - Library (`engine/`, target `pkmn::core`): `compression` (LZ10, LZ11,
    Huffman4/8, RLE, **BLZ**), `nds` (header, overlays, FAT, FNT walker),
    `narc` (archive parsing).
  - Tools (`pkmn_romdump`): header dump, ARM9 BLZ size, `--list`, `--narcs`,
    `--overlays`.
  - Tests (`pkmn_tests`, run via `ctest`): codec round-trips + bad-input, plus a
    ROM-driven inventory test (header, ARM9 BLZ, FNT/FAT, NARC, all overlays).
  - Verified on all four ROMs (SHA-256 in `re/notes/header.md`).

