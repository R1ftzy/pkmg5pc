# nitrofs (FNT / FAT file system)

## Target

All four ROMs (see `header.md` for SHA-256).

## Tools

- `pkmn_romdump --list` (`nds::list_files`), `pkmn_tests`
  (`nds_overlays_decompress`), clang 21.1.8, 2026-10-14.

## Observations

### Header locations

| ROM | FNT off | FNT size | FAT off | FAT size | FAT entries |
| --- | --- | --- | --- | --- | --- |
| Black (IRBO) | 0x2F0E00 | 0x410 | 0x2F1400 | 0xF20 | 484 |
| White (IRAO) | 0x2F0E00 | 0x410 | 0x2F1400 | 0xF20 | 484 |
| Black 2 (IREO) | 0x363400 | 0x4F0 | 0x363A00 | 0x14B0 | 662 |
| White 2 (IRDO) | 0x363400 | 0x4F0 | 0x363A00 | 0x14B0 | 662 |

FAT entry = 8 bytes (`u32 start`, `u32 end`, absolute ROM offsets). Number of
FAT entries = `FAT size / 8`.

### FNT layout (matches GBATEK "NitroROM File Systems")

- FNT base = header 0x40 offset.
- **Directory main-table** at `FNT+0`, 8 bytes per directory (ID F000h..):
  - 0x00 `u32`: offset to this directory's sub-table (relative to FNT base)
  - 0x04 `u16`: ID of first file in the sub-table
  - 0x06 `u16`: for root (F000h), total number of directories; for sub-dirs,
    the parent directory ID.
- **Sub-tables** at `FNT+offset`, terminated by a `0x00` byte:
  - `0x00` = end of sub-table
  - `0x01..0x7F` = file entry, length = value, then `len` ASCII name bytes.
    File IDs are assigned incrementally from the directory's "first file ID".
  - `0x81..0xFF` = sub-directory entry, length = `value - 0x80`, then `len`
    name bytes, then `u16` sub-directory ID (recurse into its main-table entry).

### File-id / overlay relationship

File IDs `0..(overlay_count-1)` are the ARM9 overlays (FAT entries 0..236 for
BW, 0..343 for B2W2). Named NitroFS files then follow contiguously:

- Black/White: 237 overlays + **247** named files = 484 = FAT entries.
- Black 2/White 2: 344 overlays + **318** named files = 662 = FAT entries.

### Sample tree (Black)

```
000237     2320 gfl_font.dat
000238    60336 skb.narc
000239    12164 soundstatus.narc
000240   227088 titledemo.narc
000241 51533312 wb_sound_data.sdat
000242     2924 a/0/0/0
000243    54228 a/0/0/1
...
000252      812 a/0/1/0
```

## Inferences

- (confidence: high) FNT and FAT parse cleanly and mutually agree: every named
  file's ID is `< FAT entry count`, and `overlay_count + named_files == FAT
  entries` for all four ROMs — independent cross-validation of the parsers.
- (confidence: high) `a/0/<archive>/<member>` paths name members of the primary
  NARC archives (`a/0/0`, `a/0/1`, ...), matching the known Gen 5 layout.

## Unknowns

- Exact NARC member typing for each `a/0/N` archive (NCGR/NCLR/NSBMD/SDAT/...)
  — next step (see `Plan.md` M1).
- Whether any `a/0/*` members are themselves BIOS-compressed (LZ10/LZ11); not
  yet scanned.

## NARC (Nitro ARChive) members

NARC is a nested filesystem (GBATEK "NitroARC"). Layout:

- `NARC` header (0x10): magic, BOM `FFFEh`, version `0100h`, file size,
  chunk size `0x10`, number of chunks (`3`).
- `BTAF` chunk: `u16 num_files`, `u16 reserved`, then FAT entries
  (`u32 start`, `u32 end`, relative to the GMIF image start).
- `BTNF` chunk: FNT data (identical structure to the NitroROM FNT; base =
  `BTNF + 8`), padded to 4-byte alignment.
- `GMIF` chunk: raw member data; FAT offsets are relative to `GMIF + 8`.

Chunk iteration advances by `align4(offset + chunk_size)`.

### Observations

- Of the 247 named NitroFS files (Black), **237** begin with the `NARC` magic:
  the top-level `.narc` files (`skb.narc`, `soundstatus.narc`, `titledemo.narc`)
  and every `a/<x>/<y>/<z>` member. Total member count: **54054** (Black).
- Most Gen5 NARCs have an **empty BTNF sub-table** (0 names): e.g. `a/0/0/0` is
  a 16-byte BTNF whose root directory entry points at an immediate `0x00`
  terminator. File names are optional and usually omitted.
- Exceptions exist; e.g. `a/2/3/2` has 1 named member:
  `battle_pocket.dat` (id 0).
- Example `a/0/0/0` raw dump (first 0x44 bytes):

```
4E 41 52 43 FE FF 00 01 6C 0B 00 00 10 00 03 00   NARC....l.......
42 54 41 46 34 00 00 00 05 00 00 00 00 00 00 00   BTAF4...........
28 02 00 00 28 02 00 00 50 04 00 00 50 04 00 00   (...(...P...P...
90 06 00 00 90 06 00 00 D0 08 00 00 D0 08 00 00   ................
42 54 4E 46 10 00 00 00 04 00 00 00 00 00 01 00   BTNF............
47 4D 49 46 18 0B 00 00                           GMIF....
```

### Inferences

- (confidence: high) `a/<x>/<y>/<z>` are NARC containers (not single assets);
  their members must be unpacked before format parsing (NCGR/NCLR/NSBMD/...).

### Unknowns

- Member format/type per archive — not yet identified (next: sniff member
  magic bytes, e.g. `RGCN`/`RLCN`/`BMD0`/`SDAT`).
- Whether some members are BIOS-compressed (LZ10/LZ11) or BLZ.

## Extraction output (`romdump --extract <dir>`)

`pkmn::extract::to_directory` (Black, NDSi Enhanced, 2026-10-14) writes:

```
<dir>/arm9.bin                 decoded BLZ ARM9        (1 file)
<dir>/overlays/ovNNN.bin       decoded overlays        (237 files)
<dir>/nitrofs/<path>           raw named NitroFS files (247 files)
<dir>/narc/<archive>/<id>      NARC member, 4-digit id (54054 files)
<dir>/manifest.sha256          <sha256>  <path>  (sha256sum format)
```

Counts (Black): **54539** files written, **0** skipped, 344272100 bytes
(`overlay_count + named_files + members == 237 + 247 + 54054 + 1 arm9`).
Nested NARCs (if any) are written as `<id>.narc` plus a `<id>.d/` tree; none
observed in Black.

Determinism (acceptance for M1):

- manifest entries are sorted by path; two independent runs produce
  byte-identical `manifest.sha256`
  (`sha256 = 29851932d23a9e7b594ee20eea9901a8320bd8d869701d01dad7a6523b416031`).
- every listed hash was re-verified with an external SHA-256 tool.

Empty NARC members (`start == end`) are written as zero-byte files rather than
treated as errors; 5640 such members exist in Black (e.g. unused palette slots).

## Cross-ROM comparison

- Black/White share FNT/FAT offsets (identical layout). Black 2/White 2 share
  their own offsets. Verified by SHA-256-identified ROMs, parsed with the same
  code.
- White/White 2 counts identical to Black/Black 2 (247 / 318 named files).

## Evidence

- `pkmn_romdump --list "roms/<Black>.nds"` -> `total files: 247`; first named
  file id 237 (`gfl_font.dat`), last 483.
- `pkmn_romdump --list "roms/<Black 2>.nds"` -> `total files: 318`.
- `pkmn_tests` `nds_overlays_decompress` asserts `bad_id == 0` (all FNT file
  IDs within FAT range).
