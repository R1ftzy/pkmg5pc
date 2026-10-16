# Symbol maps

Named symbol maps recovered from static analysis. These name *addresses*
(reverse-engineered knowledge, safe to share); they never contain ROM bytes.

## Format

One JSON file per ROM + module, e.g. `black-arm9.json`, `black-ov0.json`,
`black-arm7.json`.

```json
{
  "game_code": "IRBO",
  "sha256": "<rom sha-256>",
  "module": "arm9",
  "base": "0x02004000",
  "notes": "source note file, e.g. arm9-init.md",
  "symbols": [
    {
      "address": "0x02004800",
      "name": "_start",
      "kind": "function",
      "size": null,
      "comment": "NitroSDK crt0; disables IME, CP15 init, stacks, VBlank wait, branch to main",
      "confidence": "high",
      "evidence": ["A9-03", "A9-07", "A9-10"]
    }
  ]
}
```

Rules:

- `address` is the runtime (RAM) address the module is loaded at, as a `0x...`
  string. Overlay files use the overlay's RAM address as `base`.
- `kind` is one of `function`, `data`, `label`, `rodata`, `bss`.
- `confidence` follows the notes convention (`high`/`medium`/`low`).
- `evidence` lists Evidence IDs from `../notes/*.md`.
- Do not add a symbol you cannot tie to an observation or a recorded inference.

## Consumers

- A future importer will load these into Ghidra / the recompiler / deterministic
  tests. Keep `name` stable once referenced elsewhere.
