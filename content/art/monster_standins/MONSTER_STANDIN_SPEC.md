# Monster Stand-in Spec (isoforge-engine)

**Status:** LOCKED 2026-10-28 (Zach) — project-agnostic default. Engine fallbacks, not final art. No project IP.

## Goal
Whole-body silhouette stand-ins for creatures that do **not** use the humanoid paper-doll.

## Size bins (feet pivot at bottom-centre)
| bin | canvas | feet pivot |
|---|---|---|
| tiny | 16×16 | (8,15) |
| small | 24×32 | (12,31) |
| medium | 32×48 | (16,47) — same as humanoid doll, for swap tests |
| large | 48×64 | (24,63) |

## Facings (v0)
4 iso diagonals: `se` `sw` `ne` `nw` (engine 4-facing slice). 8-facing optional later.
`sw`/`nw` are H-mirrors of `se`/`ne`; mirror is flip + 1px shift so pivot x = W/2 stays fixed on even widths.

## Art model
One whole-body PNG per frame (no paper-doll slots). Hard-edged integer pixels, real alpha (0/255 only).
Construction greys: fill `#5C6370`, mid `#8C7A62`, outline `#1E2228`, optional seam `#828282`.
Magenta `#FF00FF` only if a transparency key is required — prefer real alpha.

## Clip set (map missing humanoid clips onto these)
| clip | frames | fps | mode | notes |
|---|---|---|---|---|
| idle | 4 | 6 | loop | breath/bob |
| walk | 6 | 10 | loop | hop or sway |
| react | 3 | 12 | once | flinch |
| attack | 6 | 12 | once | lunge (covers melee) |
| cast | 4 | 10 | once | pulse |
| stun | 4 | 8 | loop | wobble |
| death | 5 | 8 | once, hold last | flatten |
| fall | 3 | 12 | loop | airborne |
| fall_impact | 3 | 12 | once | squash → rest |

## Naming
`mon_{size}_{facing}_{clip}_{ff:02d}.png`; optional strip `mon_{size}_{facing}_{clip}_strip.png`.

## Engine rules
- Clip modes: `loop` / `once` / `hold_last`.
- Missing clip → `idle`. Death holds last frame.
- Feet pivot fixed every frame (grounded frames paint the pivot pixel; airborne frames lift above it).
- Suggested aliases: climb/crawl → walk, hide → idle, melee → attack.

## Out of scope
Paper-doll, unique creature IP, dedicated climb/crawl/hide clips (engine aliases).
