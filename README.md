# pkmnbwb

A clean native PC (Windows/Linux) reimplementation of Pokémon Black/White and
Black 2/White 2, driven by user-supplied ROMs. See `AGENTS.md` for contributor
guidance and `Plan.md` for the roadmap.

This repository contains **no game content**. You must own and supply the ROMs.

## Layout

- `rom/` — ROM & Nitro format layer (headers, NitroFS/FNT/FAT, NARC, codecs)
- `tools/` — local tooling: `romdump` (extraction), `bankgen` (title banks)
- `re/` — reverse-engineering notes and evidence
- `tests/` — unit and golden-data tests
- `cmake/` — build integration: the `ndsrecomp` runner + SDL3 superbuild
- `banks/` — generated title banks (git-ignored; produced from your own ROM)

## Requirements

- **CMake >= 3.24** and a generator: **Ninja** (recommended) or Visual Studio.
- A **C++20** compiler: `clang++` on Windows (LLVM-MinGW works and is what the
  presets pin) or `gcc`/`clang` on Linux.
- **Git** — `third_party/ndsrecomp` is a pinned submodule.
- Network access on the first *runner* configure: CMake clones SDL3 from GitHub.
  (The clone uses `git`, so it still works where `raw.githubusercontent.com` is
  blocked.)

SDL3 is **not** a manual prerequisite — the runner configure fetches and builds
it automatically (see [Runner](#runner-the-bootable-build)). Nothing outside
`rom/`, `tools/` and `tests/` needs it.

Windows: the presets pin `CMAKE_CXX_COMPILER=clang++`. Put `clang++`, `cmake`,
`ninja` and `ctest` on `PATH`.

## Build

Every ROM-independent target (`rom/`, `tools/`, `tests/`) builds with no ROM and
needs no submodule checkout:

```sh
# Configure (pick the preset for your OS)
cmake --preset windows-debug          # or: linux-debug

# Build
cmake --build --preset windows-debug

# Run the test suite
ctest --preset windows-debug
```

Presets (`CMakePresets.json`): `windows-debug`, `windows-release`, `linux-debug`,
`linux-release`. Output goes to `build/<preset>/`; the test binary is
`build/<preset>/tests/pkmn_tests[.exe]`, the tools are under
`build/<preset>/tools/`.

Once the runner is enabled, `ctest` also runs `ndsrecomp`'s own tests. Two of
those are environment-sensitive on Windows and can fail without affecting the
runner — see [Troubleshooting](#troubleshooting).

## Runner (the bootable build)

Running the game needs **title banks** recompiled from your own ROM plus the
`ndsrecomp` runner. This is **off by default** (`PKMNBWB_BUILD_RUNNER=OFF`), so
CI and the plain build never touch it.

Banks must exist **before configuring**: the runner globs them at configure time
and fails closed on an empty group. The order below matters.

### 1. Bootstrap the recompiler

`third_party/ndsrecomp` is a submodule, and the recompiler is a standalone
project. Its bootstrap script inits the submodule (including the nested
`arm-recomp-core`, whose pinned URL is broken and gets repointed at GitHub),
stages the toml++ header, builds `nds_recompile`, and generates the BIOS banks:

```sh
# Windows
pwsh tools/bootstrap_ndsrecomp.ps1

# Linux / macOS
sh tools/bootstrap_ndsrecomp.sh
```

Add `-BuildRunner` / `--build-runner` to also build a standalone runner under
`third_party/ndsrecomp/runner/build/`. **Not needed here** — step 4 builds the
runner as part of this project, which is the only way to have SDL3 supplied via
CMake.

### 2. Build this project's tools

```sh
cmake --preset windows-debug
cmake --build --preset windows-debug
```

This produces `pkmn_bankgen`, which step 3 needs.

### 3. Generate the banks

```sh
build/windows-debug/tools/pkmn_bankgen.exe \
    "ROM_PATH --recompiler third_party/ndsrecomp/recompiler/build/nds_recompile.exe
```
e.g.

```sh
build/windows-debug/tools/pkmn_bankgen.exe \
    "roms/pkmn-black.nds" banks/black \
    --recompiler third_party/ndsrecomp/recompiler/build/nds_recompile.exe
```

(Drop the `.exe` on Linux.) This writes to the git-ignored `banks/black/`:

| Output | Meaning |
| --- | --- |
| `irbo_arm9_main_00..07.c`, `irbo_arm9_main_dispatch.c` | ARM9 bank: 5265 functions, 8 shards |
| `irbo_arm7_main_00..07.c`, `irbo_arm7_main_dispatch.c` | ARM7 bank: 49 discovered functions |
| `bin/*.bin`, `config/*.toml` | decoded images and recompiler configs |

`pkmn_bankgen` derives every input from the ROM (load address, size and entry
point come from the header and overlay table), so nothing ROM-derived is
hand-written or committed. Options:

- `--shards N` — split the bank into N body shards (default 8; **minimum 2**).
- `--emit-only` — write `bin/` + `config/` and stop before running the
  recompiler.

Only ARM9 + ARM7 static banks are generated. Overlays are deliberately left to
the runner's runtime live-overlay path.

### 4. Configure and build with the runner

```sh
cmake --preset windows-relwithdebinfo -DPKMNBWB_BUILD_RUNNER=ON
cmake --build --preset windows-relwithdebinfo
```

Use the **`-relwithdebinfo`** preset for anything you intend to measure or
profile; `windows-debug` is `-O0` and its timings mislead (the first recorded
boot numbers were all Debug). Debug stays for the test suite.

`PKMNBWB_BUILD_RUNNER=ON` (a) checks that banks exist, (b) fetches and builds
**SDL3 3.4.18** via FetchContent (static, tests/examples off), then
(c) `add_subdirectory`s the runner with `NDS_BOOTSTRAP_FIRMWARE=ON`,
`NDS_SDL_BACKEND=SDL3`, `NDS_ENABLE_COMPUTE_RENDERER=OFF`. The first configure
clones and builds SDL3, so it takes several minutes.

Two cache variables select which ROM the banks belong to:

| Variable | Default | Purpose |
| --- | --- | --- |
| `PKMNBWB_BANKS_DIR` | `banks/black` | bank directory from step 3 |
| `PKMNBWB_TITLE_ROM_SHA1` | Black's SHA-1 | the ROM those banks came from |

For White / Black 2 / White 2, point both at that title's bank dir and SHA-1; the
runner registers a bank group only for a ROM whose SHA-1 matches.

### 5. Run

```sh
build/windows-relwithdebinfo/third_party/ndsrecomp/runner/nds_runner.exe \
    bios 250000000 --boot direct --generated-firmware --freebios \
    --rom "roms/<your-rom>.nds" \
    --interactive
```

- The two **positional** arguments are the BIOS directory (a scratch dir; the
  runner writes a generated Wi-Fi identity into it) then the **ARM9 cycle
  budget**. Without `--interactive` the run executes the budget and exits with a
  CPU and graphics report instead of opening a window.
- `--freebios` supplies the BSD-2 BIOS images vendored inside `ndsrecomp`; it is
  effectively required, since without it the runner wants retail BIOS dumps and
  aborts with `dump verification failed`. Both `--freebios` and
  `--generated-firmware` require `--boot direct`.
- `--save-path <file>` persists a battery save; `--no-save` disables it.
- Diagnostics: `--diagnostics on --diagnostics-dir <dir>` writes a performance
  bundle, and a coverage manifest is written next to the ROM by default.
- For automated eyes, `--serve` starts the same run headlessly with the JSON
  debug server on port 19842 (`third_party/ndsrecomp/TCP.md`), and
  `tools/runwatch.py` drives it: it logs a timeline of vblank counts, ARM9/ARM7
  PCs, DISPCNT for both engines and a hash per frame, writes a PNG plus a
  console-layout (top over bottom) montage whenever the screen changes, and —
  if the watched counter stops advancing — takes a stall report instead of
  waiting out the protocol's round cap.

  ```sh
  python tools/runwatch.py --runner build/windows-relwithdebinfo/third_party/ndsrecomp/runner/nds_runner.exe \
      --rom "roms/<your-rom>.nds" --screenshots banks/screenshots --out banks/timeline.jsonl
  ```

  Screenshots are ROM-derived output: keep them in the git-ignored `banks/` tree.

Status: the pipeline is verified end-to-end — banks generate, link, and register
against the ROM SHA-1 at run time, both CPUs execute, and the ARM9 reaches
NitroSDK init. **The game now renders:** engine A enables BG1 at `vblank9=240`
(~4 s of emulated time) and the captured frame is the Pokémon Company + Nintendo
boot splash (bottom screen correctly blank). It does **not** yet reach the title
screen/menu. Optimized marginal throughput is ~26 % of real-time. See
`re/notes/recompilation.md` and the M2.3 entries in `Plan.md`.

### Troubleshooting

- `refusing to start: dump verification failed` — add `--freebios`.
- `PKMNBWB_BUILD_RUNNER=ON but no generated title banks were found` — run step 3
  first; check `PKMNBWB_BANKS_DIR` if the banks are somewhere else.
- Two `ndsrecomp` tests are environment-sensitive on Windows and can fail
  without affecting the runner: `gpu2d_window_test` overflows the default thread
  stack (`0xC00000FD`; the `nds_runner` target gets a 64 MB stack, the test does
  not), and `live_index_lock_test` (Python-driven) is intermittent — its index
  writers sometimes fail to spawn.

## ROMs (required for the ROM-dependent tests)

Place your own legally obtained ROMs in `roms/` (git-ignored). Any dump
filename works, e.g.:

```
roms/<your-rom>.nds
```

Supported titles are the USA/EU NDSi-Enhanced dumps of Black, White, Black 2
and White 2. Dumps are referred to by path only; no dump's release filename is
recorded in this repo.

No ROM is ever committed, and no ROM is required to build or for CI.

## Testing

There are two layers of tests:

1. **ROM-independent (pure unit tests)** — compression codecs and hash
   primitives. These always run and are what CI executes (CI has no ROMs).
2. **ROM-dependent** — NitroFS/overlay/NARC parsing against a real ROM. If no
   ROM is found, the test prints `(skipped: no ROM found; set PKMNBWB_ROM)` and
   passes, so the suite stays green without a ROM.

### How a ROM is located

The ROM test resolves the ROM in this order:

1. The `PKMNBWB_ROM` environment variable, if set to a `.nds` path.
2. Otherwise, the first `*.nds` file found in `roms/`.

To test a specific ROM, set the variable **before** running:

```sh
# Windows (PowerShell)
$env:PKMNBWB_ROM = "C:\path\to\your-rom.nds"
ctest --preset windows-debug --output-on-failure

# Linux / macOS
PKMNBWB_ROM="/path/to/black.nds" ctest --preset linux-debug --output-on-failure
```

### Run everything

```sh
ctest --preset windows-debug --output-on-failure
```

### Run the test binary directly (see per-test detail)

`ctest` hides stdout on success. To see the ROM inventory, NARC counts, overlay
matches, etc., run the binary directly:

```sh
# Windows
build/windows-debug/tests/pkmn_tests.exe

# Linux
./build/linux-debug/tests/pkmn_tests.exe
```

The binary accepts an optional **name filter** as its first argument (substring
match), e.g.:

```sh
build/windows-debug/tests/pkmn_tests.exe lz11       # only runs lz11_roundtrip
build/windows-debug/tests/pkmn_tests.exe nds        # only runs nds_overlays_decompress
```

### What the tests cover

Compression round-trips (`tests/compression_test.cpp`):

- `rle_roundtrip`, `lz10_roundtrip`, `lz11_roundtrip`,
  `huffman4_roundtrip`, `huffman8_roundtrip` — encode then decode and compare.
- `decompress_rejects_bad_input` — malformed input throws instead of crashing.

ROM inventory (`tests/rom_overlay_test.cpp`):

- `nds_overlays_decompress` — parses the header, decompresses the ARM9 (BLZ),
  walks the NitroFS FNT/FAT tree, sniffs NARC archives, and BLZ-decompresses
  every ARM9 overlay, checking each against its declared RAM size.

Extraction (`tests/extract_test.cpp`):

- `extract_manifest_deterministic` — extracts a subset twice and re-verifies
  every hash and size.

Hashes (`tests/sha1_test.cpp`, `tests/sha256_test.cpp`):

- `sha1_known_vectors`, `sha256_known_vectors` — published test vectors.
- `sha1_padding_boundaries` — lengths that straddle SHA-1's padding rules,
  cross-checked against an independent implementation.

### Expected output (example)

```
[ RUN  ] rle_roundtrip
[ PASS ] rle_roundtrip
...
  title='POKEMON B' code='IRBO' arm9=456856 ovl9=7584
  arm9 blz: 456856 -> 681920
  nitrofs: 247 files
  narc: 237 archives, 54054 members, 1 named, 0 bad
  overlays: 229 compressed (229 ram-matched), 8 raw matched, 0 mismatched
[ PASS ] nds_overlays_decompress

11 test(s) run, 0 failure(s)
```

A healthy run shows `0 mismatched`, `0 bad`, and no `FAIL` lines.

## Tools

`pkmn_romdump` inspects a ROM without running the game. Build first, then:

```sh
build/windows-debug/tools/pkmn_romdump.exe "roms/<your rom>.nds" [options]

# Options
--list              # print the NitroFS file tree (id, size, path)
--narcs             # list every NARC archive, its member count and names
--overlays          # list ARM9 overlays and BLZ decode sizes
--extract <dir>     # write a full extracted/ inventory + SHA-256 manifest
--narc-limit <n>    # with --extract: expand only the first n NARC archives
```

Options can be combined (e.g. `--list --overlays`). With no options it prints
the header (title, game code, ARM9/overlay/FNT/FAT offsets) and the decompressed
ARM9 size.

`pkmn_bankgen` generates the runner's title banks from your ROM — see
[Runner](#runner-the-bootable-build).

`tools/runwatch.py` is the bring-up harness. It launches the runner with
`--serve` and samples the running game over the debug protocol, so one long run
produces a progress curve (vblank counts, ARM9/ARM7 PCs, DISPCNT enables, frame
hashes) rather than a single endpoint. `--screenshots` writes each change as a
PNG plus a top-over-bottom montage, and a counter that stops advancing produces a
stall report (interrupt state, halt reasons, a controlled `run_cycles` probe)
rather than a silent hang. Requires only Python 3; writes to the git-ignored
`banks/` tree by default. `tools/ndsquery.py` sends ad-hoc one-shot commands to a
running server.

### Extracting a ROM

```sh
build/windows-debug/tools/pkmn_romdump.exe "roms/<your rom>.nds" --extract extracted/black
```

This writes (all git-ignored under `extracted/`):

```
extracted/black/arm9.bin             decoded ARM9
extracted/black/overlays/ovNNN.bin   decoded ARM9 overlays
extracted/black/nitrofs/<path>       named NitroFS files (raw)
extracted/black/narc/<archive>/<id>  expanded NARC members
extracted/black/manifest.sha256      "<sha256>  <path>" for every file
```

The manifest uses the standard `sha256sum` format and is deterministic across
runs; verify it with `sha256sum -c manifest.sha256` (Linux) from inside the
output directory. Full extraction writes tens of thousands of files and takes a
couple of minutes; use `--narc-limit <n>` for a quick partial run.

## Formatting / lint

The project uses clang-format (`.clang-format`, WebKit-based). Before finishing
any change, check that your source files conform (clang-format does not recurse
directories, so list the files):

```sh
# Linux / macOS
clang-format --dry-run --Werror $(find rom tools tests \( -name '*.cpp' -o -name '*.h' \))

# Windows (PowerShell)
clang-format --dry-run --Werror (Get-ChildItem rom,tools,tests -Recurse -Include *.cpp,*.h).FullName

# Apply the fixes in place (same file list, replace -i for --dry-run --Werror)
clang-format -i rom/src/nds.cpp
```

## Legal

The program ships without Nintendo/Game Freak assets and requires the user's
own ROMs at run time. See `NOTICE` and `AGENTS.md` section 2.
