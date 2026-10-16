# pkmnbwb

A native PC reimplementation of the Generation V Pokémon games, driven by a
user-supplied ROM and built on statically recompiled game code plus a hardware
layer.

## Language

**Static recompilation**:
Translating a program's machine code ahead of time into native C++ that is
compiled and linked like any other code; the guest's instructions are never
interpreted at run time.
_Avoid_: emulation, transpilation

**Recompiler**:
The tool that performs static recompilation, turning a ROM's ARM9/ARM7/overlay
code into native translation units.
_Avoid_: engine

**Runner**:
The runtime that hosts recompiled code, supplying the memory map, CPU services,
devices, and present loop those translated units expect. It does not interpret
the game's instructions.
_Avoid_: emulator, engine

**Engine**:
_Deprecated as a bare term._ It ambiguously meant both the recompiler+runner
and our own format library. Say **recompiler**, **runner**, or **ROM layer**.

**Oracle**:
An independent, known-good implementation of the same hardware (e.g. stock
melonDS), run separately to compare frames, RAM, or saves against our port.
_Avoid_: reference emulator

**ROM layer**:
Our portable library for reading a user's ROM — the NDS header, NitroFS, NARC,
overlay tables, and the Nitro compression codecs. It is an **offline** layer
(bank generation, save I/O, launcher ROM detection): the running game reads its
cartridge through the runner's emulated cart, never through this library.
Contains no game logic and ships no game content.
_Avoid_: engine, core

**Bank**:
The generated native translation unit produced for one recompiled image (the
ARM9 binary, one overlay, or the ARM7 image), generated from the user's own ROM
and never committed.
_Avoid_: module, blob

**Title bank**:
The complete set of banks for one game title; producing them needs a recompiler
configuration for that title.

**Bank group**:
The set of generated banks the runner registers together for one ROM, held in a
single directory and gated by that ROM's SHA-1.
_Avoid_: bank dir, bank set

**HAL** (hardware abstraction layer):
The seam through which game logic reaches hardware devices; game logic never
touches a device or the GPU directly.

**HLE** (high-level emulation):
Implementing a device's or BIOS's behaviour directly instead of executing its
original code.

**DS GE**:
The Nintendo DS's fixed-function 3D "Geometry Engine".
