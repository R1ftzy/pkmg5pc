# Static recompilation, not emulation or a decomp port

**Status:** accepted (2026-10-14)

The Gen 5 titles have no usable matching decompilation, and we do not want to
ship an emulator core, so game code is statically recompiled to native C++ and
only the hardware is reimplemented (a HAL). We adopt `ndsrecomp` as the
recompiler + runner rather than writing either from scratch.

Considered: a matching-decomp port (none exists for Gen 5), and a from-scratch
engine reimplementation (enormous). Both rejected.
