# Adopt GPL-3.0-or-later, reusing melonDS's 3D GE and Wi-Fi

**Status:** accepted (2026-10-16)

To get a working DS 3D GE and Wi-Fi stack without rebuilding either,
we reuse `ndsrecomp`'s vendored melonDS trees, which are GPL-3.0-or-later
(`ndsrecomp`'s own code is MIT). Linking them makes the shipped binary a GPL
combined work, so this project is licensed GPL-3.0-or-later.

Considered: staying MIT, which would require writing the DS GE ourselves — no
permissively-licensed DS 3D renderer exists (DeSmuME and melonDS are both GPL).
Rejected. The bridge headers (`gpu3d.h`, `wifi.h`) are an engineering seam, neither a legal
boundary. Note the license only binds on distribution.
