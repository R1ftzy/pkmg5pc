# header / ROM identity

## Target

Four user-supplied NDSi-Enhanced USA/EU ROMs in `roms/` (git-ignored).

| Game | Size | SHA-256 |
| --- | --- | --- |
| Black | 268435456 | `b9979189d299a0231d01c8885c0d054b706976ffc75427ca59d84e0ce1493034` |
| White | 268435456 | `b288bb061fd646894f5059f55cd0a1efb13b4f0cfd3d9e06e9e42a5bd9431ac6` |
| Black 2 | 536870912 | `2e6b2415354aa41471bc7617068dce059a59931bf5c4348a264f8043f297683a` |
| White 2 | 536870912 | `3e50aec3db401332175a5d2b5fe2a68ac1a05ec63995dba9d1506b1b51837446` |

Tools: `pkmn_romdump` (this repo), clang 21.1.8, 2026-10-14.

## Observations

Header fields read via `pkmn::nds::parse_header` (all four ROMs), offsets per
GBATEK cart header:

| Field | offset | Black | White | Black 2 | White 2 |
| --- | --- | --- | --- | --- | --- |
| title | 0x00 | `POKEMON B` | `POKEMON W` | `POKEMON B2` | `POKEMON W2` |
| game code | 0x0C | `IRBO` | `IRAO` | `IREO` | `IRDO` |
| ARM9 rom offset | 0x20 | 0x4000 | 0x4000 | 0x4000 | 0x4000 |
| ARM9 entry | 0x24 | 0x2004800 | 0x2004800 | 0x2004800 | 0x2004800 |
| ARM9 ram addr | 0x28 | 0x2004000 | 0x2004000 | 0x2004000 | 0x2004000 |
| ARM9 size (rom) | 0x2C | 0x6F898 | 0x6F8A4 | 0x736A4 | 0x736CC |
| ARM7 rom offset | 0x30 | 0x2C7E00 | 0x2C7E00 | 0x33A400 | 0x33A400 |
| FNT offset/size | 0x40/0x44 | 0x2F0E00 / 0x410 | 0x2F0E00 / 0x410 | 0x363400 / 0x4F0 | 0x363400 / 0x4F0 |
| FAT offset/size | 0x48/0x4C | 0x2F1400 / 0xF20 | 0x2F1400 / 0xF20 | 0x363A00 / 0x14B0 | 0x363A00 / 0x14B0 |
| ARM9 ovl off/size | 0x50/0x54 | 0x73A00 / 0x1DA0 | 0x73A00 / 0x1DA0 | 0x77800 / 0x2B00 | 0x77800 / 0x2B00 |
| ARM9 ovl count | | 237 | 237 | 344 | 344 |
| ARM7 overlay table | 0x58/0x5C | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |

## Inferences

- (confidence: high) Game codes `IRBO/IRAO/IREO/IRDO` (rev 0) confirm the four
  target titles; BW share ROM size and FNT/FAT/overlay offsets, as do B2W2.
- (confidence: high) ARM9 ram address `0x02004000`, entry `0x02004800` on all
  four — shared boot contract.
- (confidence: high) ARM9 size (0x2C) is the **compressed** size on ROM; the
  decompressed ARM9 is larger (see `overlays.md`).
- (confidence: medium) No ARM7 overlays exist (both table fields zero) on all
  four; ARM7 code lives in the single ARM7 binary.

## Unknowns

- ARM9/ARM7 binary internal layout beyond the boot entry/init sequence
  (see `arm9-init.md`; entry, crt0, and THUMB main located).
- NitroFS file tree / NARC inventory (see `nitrofs.md`; FNT/FAT + NARC parsed).
- Whether the four ROMs are strictly the targeted revisions (only rev-0 game
  codes observed).
