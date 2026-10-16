# overlays / ARM9 overlays

## Target

All four ROMs (see `header.md` for SHA-256).

Tool: `pkmn_romdump --overlays` and `pkmn_tests` (`nds_overlays_decompress`),
clang 21.1.8, 2026-10-14.

## Observations

### Overlay table entry (32 bytes, ARM9 table at header 0x50)

Verified by dumping raw bytes at `0x73A00` (Black). Field layout matches GBATEK:

| offset | meaning | Black ov0 value |
| --- | --- | --- |
| 0x00 | Overlay ID | 0 |
| 0x04 | RAM address | 0x021542C0 |
| 0x08 | RAM size | 0x420 |
| 0x0C | BSS size | 0 |
| 0x10 | Static init start | 0x021546C4 |
| 0x14 | Static init end | 0x021546C8 |
| 0x18 | File ID (into FAT) | 0 |
| 0x1C | **compressed size** | 0x0300040C |

GBATEK labels 0x1C "Reserved (zero)", but these ROMs populate it.

### Observed 0x1C encoding (Black, all 237 entries)

- high byte `0x03`: 229 entries; low 24 bits == FAT file length.
- high byte `0x02`: 8 entries; low 24 bits == 0; FAT file length == RAM size.
- `low24 == file length` for exactly the 229 `0x03` entries; otherwise 0.

Raw hexdump of `0x73A00`:
```
ov0: 00 00 00 00 C0 42 15 02 20 04 00 00 00 00 00 00
     C4 46 15 02 C8 46 15 02 00 00 00 00 0C 04 00 03
ov1: 01 00 00 00 C0 42 15 02 00 04 00 00 00 00 00 00
     9C 46 15 02 A0 46 15 02 01 00 00 00 00 00 00 02
```

### Overlay file contents

Every overlay file begins with raw ARM/THUMB code, e.g. Black ov0 starts
`F0 B5 83 B0 14 1C 26 68` (`F0 B5` = THUMB `push {r4,r5,r6,r7,lr}`). No overlay
starts with a BIOS compression magic byte (0x10/0x11/0x24/0x28/0x30).

### Facts / unknowns

- (confidence: high) `RAM size` for every overlay is `>=` its FAT file length
  (0 files larger, 8 equal, 229 smaller).
- (confidence: high) All overlays decode with **BLZ** to exactly `RAM size`.

## Inference: overlays are BLZ ("Bottom LZ") compressed

(confidence: high)

- DS overlays use a "bottom LZ" scheme (CUE's `blz.c`; also PeterLemon
  `Nintendo_DS_Compressors`). It can leave an **unencoded prefix** at the start
  of the file (hence raw THUMB code up front) and stores the encoded part
  reversed at the **end** of the file, described by a footer.
- The `0x1C` field therefore is not a simple "0 = uncompressed" flag as GBATEK
  implies: `0x03` marks a BLZ-compressed overlay (low 24 = compressed length ==
  file length), `0x02` marks an uncompressed overlay (0x1C == `0x02000000`,
  file length == RAM size).
- Decoding rule used: `compressed = (field & 0x00FFFFFF) != 0`.
- The **ARM9 binary is also BLZ-compressed**: Black ARM9 `0x6F898` on ROM ->
  `681920` bytes decoded.

### BLZ decode (from CUE `blz.c`)

```
inc_len = u32le[file_size - 4]
if inc_len == 0:
    dec_len = file_size - 4; payload_len = 0; raw_len = dec_len   # raw file
else:
    hdr_len   = file[file_size - 5]            # must be 0x08..0x0B
    enc_len   = u32le[file_size - 8] & 0xFFFFFF
    dec_len   = file_size - enc_len
    payload_len = enc_len - hdr_len
    raw_len   = dec_len + enc_len + inc_len
copy file[0..dec_len) -> out                       # raw prefix
reverse bytes file[dec_len .. dec_len+payload_len) # "invert" packed part
# LZ loop over reversed packed bytes, read forward:
#   flags byte; per bit: 0=literal, 1=match
#   match: pos16 = b0<<8 | b1; len = (pos16>>12) + 3; dist = (pos16 & 0xFFF) + 3
reverse out[dec_len .. raw_len)                    # "invert" decoded part
result length = number of bytes written
```

## Evidence

- `pkmn_romdump --overlays "roms/<Black>.nds"`:
  `total overlays: 237, compressed: 229`; every entry prints `dec` == `ram`,
  no `MISMATCH`.
- `pkmn_tests` `nds_overlays_decompress`:
  `overlays: 229 compressed (229 ram-matched), 8 raw matched, 0 mismatched`;
  `arm9 blz: 456856 -> 681920`.
- Reference: CUE `blz.c` (GPLv3), PeterLemon/Nintendo_DS_Compressors
  `english.txt`, GBATEK "NitroROM/NitroARC File Systems".

## Cross-ROM comparison

- Black/White: overlay table at 0x73A00, 237 entries.
- Black 2/White 2: overlay table at 0x77800, 344 entries.
- ARM9 rom size: BW 0x6F898/0x6F8A4; B2W2 0x736A4/0x736CC.
- Overlay counts and ARM9 sizes verified for Black only via decompression;
  B2W2 overlay decompression not yet spot-checked.

## Unknowns

- Whether any overlay in B2W2 differs in encoding (assumed identical scheme).
- What the `0x02` vs `0x03` high byte distinguishes besides compression.
- Overlay load/init semantics (static init start/end handling) — see
  `arm9-init.md`.
