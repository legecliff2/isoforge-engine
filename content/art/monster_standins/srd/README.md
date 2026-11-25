# SRD medium monster art

This directory stores the canonical 4-facing, 9-clip medium kits for **37** SRD creatures. The frames are Long Dusk Mapping B. Twenty-six kits landed in [PR #449](https://github.com/legecliff2/isoforge-engine/pull/449) and were remapped to Mapping B in [PR #458](https://github.com/legecliff2/isoforge-engine/pull/458). Eleven more — ten Group D creatures that now have content rows, plus couatl — are added as Mapping B kits here.

## Why not `frames/{bin}/{facing}/`

The generic silhouette stand-ins stay at:

`content/art/monster_standins/frames/{bin}/{facing}/mon_{bin}_{facing}_{clip}_{ff:02d}.png`

This tree does not overwrite them, and it does not change `content/packs/srd51.core/art/clips.json` or any monster content row. The renderer loads a creature from `srd/{slug}/{bin}/` when that directory exists, and otherwise keeps the generic bin kit.

## Layout

```
content/art/monster_standins/srd/{slug}/medium/{facing}/mon_{slug}_medium_{facing}_{clip}_{ff:02d}.png
content/art/monster_standins/srd/clips.json          # art manifest for these 37 kits
content/art/monster_standins/srd/SMOKE21.md … SMOKE30.md
```

`{facing}` is `se`, `sw`, `ne`, or `nw`. The 9 clips and frame counts match `MONSTER_STANDIN_SPEC.md`: idle 4, walk 6, react 3, attack 6, cast 4, stun 4, death 5, fall 3, fall_impact 3 (38 frames × 4 facings = 152 PNG per creature). Canvas 32×48, pivot (16,47).

## Roster (26 from PR #449 / #458)

| smoke | slugs |
|---|---|
| 21 | gargoyle, invisible-stalker, xorn |
| 22 | blink-dog, dryad, green-hag, satyr |
| 23 | sea-hag |
| 24 | hell-hound |
| 25 | succubus-incubus, basilisk, death-dog |
| 26 | ettercap, grick, harpy |
| 27 | medusa, rust-monster, gray-ooze |
| 28 | shrieker, violet-fungus, swarm-of-bats |
| 29 | swarm-of-insects, swarm-of-poisonous-snakes, swarm-of-quippers |
| 30 | swarm-of-rats, swarm-of-ravens |

## Added (11 Group D + couatl)

| smoke | slugs added |
|---|---|
| 21 | azer |
| 23 | barbed-devil, bearded-devil |
| 24 | erinyes, lemure, night-hag |
| 28 | swarm-of-beetles |
| 29 | swarm-of-centipedes |
| 30 | swarm-of-spiders, swarm-of-wasps |
| 17 | couatl (no SMOKE17 note in this repo; the production record stays local) |

## Held back (kept local, not in this tree)

`chain-devil` and `rakshasa` still have no content spec row on main (held for Rules contracts). `doppelganger` and `mimic` stay held. All Huge and Gargantuan kits stay out.

## Facings / mirrors

SW/NW are not exact mirrors of SE/NE. A pixel comparison of all 1,976 west frames of the original 26 kits (horizontal flip plus the known +1px pivot shift, matching engine `hflip_pivot`, and also the art-script variant that then clears columns 0 and 31) found:

- 1,086 / 1,976 exact under the art-script flip+shift+clear-edges transform. 200 of those also match engine `hflip_pivot` with no extra edge clear.
- 890 / 1,976 differ further (hand-retouched flips: many walk, attack, and death frames have large deltas; a smaller set are 2px-class edge or bridge touch-ups).
- No kit is an exact `hflip_pivot` match on every west frame. `swarm-of-bats` is the only kit whose 76 west frames are all exact under the clear-edges variant.

The 11 added kits also leave `mirrored_from` null, so a lookup will not treat SW/NW as derived; those west frames are hand-retouched flips rather than exact mirrors.

## Source

The original 26 grey kits were copied from `/workspace/isoforge-default-art/monster-srd/` after the 2026-11-24 tiny/small/medium/large dedupe, then remapped to Long Dusk Mapping B. The 11 added kits were copied from the verified Mapping B tree `/workspace/isoforge-default-art/monster-srd/frames_long_dusk/`. Original construction greys remain the local source, so the remap is reversible. Backups (`_v1`, `_v2`, `_soft`, `_soft2`) were not included.
