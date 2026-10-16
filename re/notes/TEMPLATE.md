# <subsystem>

## Target

- ROM file: `<name>`
- SHA-256: `<hash>`
- Region / revision: `<region / rev>`
- Game code: `<code>`

## Tools

- <tool> <version>

## Observations

Material directly returned by a tool, with locations.

- (Evidence `<ID>`): `<what the tool returned>` @ offset `0x...`

## Inferences

Conclusions drawn from the observations above. Mark confidence.

- (confidence: high/medium/low) `<conclusion>` — based on `<Evidence IDs>`

## Unknowns

- `<open question>`

## Cross-ROM comparison

Differences vs other builds, by SHA-256 and offset (never by eyeballing).

- Black vs White: `<diff>`
- BW vs B2W2: `<diff>`
