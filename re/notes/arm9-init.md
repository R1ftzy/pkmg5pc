# arm9-init / boot entry & init sequence

## Target

- ROM: user-supplied Black dump in `roms/` (path varies)
- SHA-256: `b9979189d299a0231d01c8885c0d054b706976ffc75427ca59d84e0ce1493034`
- Region / revision: USA/EU, rev 0
- Game code: `IRBO`

(Same entry contract on White/Black 2/White 2; see `header.md`.)

## Tools

- `llvm-objcopy` / `llvm-objdump` 21.1.8 (LLVM-MinGW 20251216), 2026-10-14.
- `pkmn_romdump --extract` (this repo) for the decoded `arm9.bin`.

### Disassembly harness

`llvm-objdump` cannot read a raw blob, so wrap the decoded ARM9 as an ARM ELF:

```
llvm-objcopy -I binary -O elf32-littlearm -B arm \
    --rename-section .data=.text,alloc,load,readonly,code \
    extracted/black/arm9.bin arm9.elf
llvm-objdump -d --adjust-vma=0x02004000 --triple=armv5te-none-eabi  arm9.elf   # ARM
llvm-objdump -D --adjust-vma=0x02004000 --triple=thumbv5te-none-eabi arm9.elf  # THUMB
```

## Observations

- (`A9-01`) Header (see `header.md`): ARM9 rom offset `0x4000`, ram address
  `0x02004000`, entry `0x02004800`.
- (`A9-02`) Decoded `arm9.bin` is `681920` bytes (BLZ). File bytes
  `0x0000..0x07FF` (RAM `0x02004000..0x020047FF`) do **not** disassemble into
  coherent code; `0x0800` onward does. The first `0x800` bytes are high-entropy.
- (`A9-03`) At `0x02004800` (ARM):
  ```
  2004800: mov  r12, #0x04000000
  2004804: str  r12, [r12, #0x208]   ; REG_IME (0x04000208) = 0
  2004808: bl   0x02004A14           ; CP15 / cache init
  ```
- (`A9-04`) Literal pool `0x020049AC..0x020049FF`:

  | addr | value | used at | role (observed) |
  | --- | --- | --- | --- |
  | 0x020049AC | 0x02FFFF80 | 0x2004810 | fill dst #1 |
  | 0x020049B0 | 0x00000068 | 0x2004814 | fill len #1 |
  | 0x020049B4 | 0x02FFFFF0 | 0x2004820 | fill dst #2 |
  | 0x020049B8 | 0x00000010 | 0x2004824 | fill len #2 |
  | 0x020049BC | 0x02FFFC40 | 0x200482C | u16 flag, `if 0 -> 1` |
  | 0x020049C0 | 0x02004FD4 | 0x2004840 | stub src #1 (40 B) |
  | 0x020049C4 | 0x01FF8000 | 0x2004870 | stub copy dst / call target |
  | 0x020049C8 | 0x02FFFC28 | 0x200485C | u16 = 1 then arg |
  | 0x020049CC | 0x02FFFC24 | 0x2004868 | stub arg |
  | 0x020049D0 | 0x02FFFC26 | 0x200486C | stub arg |
  | 0x020049D4 | 0x02004F94 | 0x2004878 | stub src #2 (28 B) |
  | 0x020049D8 | 0x02004FFC | 0x20048AC | stub src #3 (52 B) |
  | 0x020049DC | 0x02FE0000 | 0x20048EC | stack base |
  | 0x020049E0 | 0x00000800 | 0x2004908 | per-mode stack gap |
  | 0x020049E4 | 0x05000000 | 0x2004938 | clear region |
  | 0x020049E8 | 0x07000000 | 0x2004944 | clear region |
  | 0x020049EC | 0x02004FB0 | 0x2004958 | table ptr |
  | 0x020049F0 | 0x01FF80F0 | 0x2004978 | stored to 0x02FE3FFC |
  | 0x020049F4 | 0x04000006 | 0x2004990 | REG_VCOUNT wait |
  | 0x020049F8 | 0x02005125 | 0x20049A0 | main entry (THUMB) |
  | 0x020049FC | 0xFFFF0000 | 0x20049A4 | return-to-BIOS address |
- (`A9-05`) Helper `0x02004A00` is a **word fill**: `add r12,r1,r2; cmp; strlt
  r0,[r1],#4; blt` — args `(r0=value, r1=dst, r2=len)`. Called with `r0=0` to
  zero `0x68` bytes at `0x02FFFF80` and `0x10` bytes at `0x02FFFFF0`.
- (`A9-06`) Helper `0x02004A14` reads CP15 `c1,c0,0` (`mrc p15,0,r0,c1,c0,0`)
  and tests bits — cache/MMU control init.
- (`A9-07`) Stack setup (calls `bl 0x02004A98` first), switching privilege modes:

  ```
  mov r0,#0x13; msr CPSR_c,r0   ; SVC   : sp = 0x02FE3FC0
  mov r0,#0x12; msr CPSR_c,r0   ; IRQ   : sp = 0x02FE3F7C
  mov r0,#0x1F; msr CPSR_fsxc,r0; System: sp = 0x02FE377C
  ```
  (base `0x02FE0000`, gap `0x800`, with `tst sp,#4` alignment fixups.)
- (`A9-08`) Helper `0x02004EB4`: `ldrb r0,[0x04004000]; and r0,r0,#3; cmp
  r0,#1` -> returns nonzero only when the low two bits equal 1. Caller
  `0x20048A4: bl 0x02004EB4; bne 0x20048D8` (skips the third stub unless set).
- (`A9-09`) Three small stubs are copied to a fixed low address and called:
  - `0x02004FD4` (40 B) -> `0x01FF8000`, then `blx 0x01FF8000`. Body: wait loop
    on two u16 registers (`ldrh` [r1], increment [r0], `beq` while [r1]
    unchanged).
  - `0x02004F94` (28 B) -> `0x01FF8000`, then `blx` with `(r0=0x02FFFC28,r1=4)`.
    Body: `ldrh [r0]`; spin 16 iters; retry.
  - `0x02004FFC` (52 B) -> `0x01FF801C`, then `blx`. Body manipulates a u16 at
    `0x04004004` (literal at `0x0200502C`), toggling bit 0 per `r0` and looping
    on `r1`.
- (`A9-10`) Tail: `ldrh r0,[0x04000006]; cmp r0,#0; bne` (spin until VBlank),
  then `ldr r1,[0x020049F8]=0x02005125; ldr lr,[0x020049FC]=0xFFFF0000; bx r1`.
- (`A9-11`) `0x02005124` disassembles as valid **THUMB**:
  ```
  2005124: push {r3,r4,r5,r6,r7,lr}
  2005126: bl   0x02005328
  200512a: bl   0x0200545C
  200512e: ldr  r1,[pc,#0x94]
  2005130: movs r0,#2
  2005132: blx  0x02084774
  ...
  ```

## Inferences

- (confidence: high) `0x02004800` is the NitroSDK `_start`/crt0: disable
  interrupts -> CP15 init -> clear scratch -> copy/run init stubs -> per-mode
  stack setup -> wait VBlank -> branch to main. Based on `A9-03,A9-05,A9-06,
  A9-07,A9-10`.
- (confidence: high) The game's SDK entry ("NitroMain"/`main`) is THUMB at
  `0x02005124`, entered via the odd address `0x02005125` with return-to-BIOS
  `0xFFFF0000` in `lr`. Based on `A9-11,A9-10`.
- (confidence: medium) Bytes `0x02004000..0x020047FF` are the DS "secure area"
  (2 KiB), normally decrypted by the BIOS on hardware; not executed by the SDK
  crt0. Based on `A9-02`.
- (confidence: medium) Addresses near `0x02FE0000`/`0x02FFFFF0` (stacks,
  scratch) exceed 4 MiB main RAM, so ARM9 main RAM is aliased across
  `0x02000000..0x02FFFFFF` (4x mirror of 4 MiB) — or the crt0 has a DSi-mode
  path. Based on `A9-04,A9-07`.
- (confidence: medium) The `0x04004000`/`0x04004004` register pair and the
  copied `0x01FF8000` stubs form a **mode/peripheral handshake** run
  conditionally at boot. Based on `A9-08,A9-09`.

## Unknowns

- Identity/semantics of register `0x04004000`/`0x04004004` (not a standard NTR
  register name).
- Why init stubs are relocated to `0x01FF8000`/`0x01FF801C` and what those
  routines handshake with.
- Whether the `0x04004000` path is a DSi-mode branch (ROM is NDSi-Enhanced).
- Exact boundary of the secure area and whether it stays encrypted in our
  decoded `arm9.bin`.
- The data table at `0x02004FB0` (contains `0x021542C0` = overlays' RAM base).

## Cross-ROM comparison

- Entry/stack/init contract is expected identical across the four ROMs (shared
  `header.md` entry `0x02004800`); not yet byte-diffed beyond header fields.
- B2W2 (`IREO`/`IRDO`) ARM9 is larger (`0x736A4`/`0x736CC` on ROM); its crt0 is
  presumed the same SDK version — verify by offset diff.

## Next

- Load `arm9.bin` (base `0x02004000`) and overlays into Ghidra; name `_start`,
  `NitroMain`, and the SDK init helpers (`0x02004A00` fill, `0x02004A14` CP15,
  `0x02004F20` copy); export symbol map to `re/symbols/`.
