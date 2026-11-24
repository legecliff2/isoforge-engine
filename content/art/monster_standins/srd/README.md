# SRD medium monster art (PR #449)

This directory stores the canonical 4-facing, 9-clip medium kits for the **26** SRD creatures in [PR #449](https://github.com/legecliff2/isoforge-engine/pull/449). The frames are construction greys. Branch: `art/monster-medium-pr-a`.

## Why not `frames/{bin}/{facing}/`

The engine's stand-in loader is still bin-only. `MonsterStandinKit::load` (`game/presentation/src/standin.cpp`) reads:

`content/art/monster_standins/frames/{bin}/{facing}/mon_{bin}_{facing}_{clip}_{ff:02d}.png`

Those folders hold the generic silhouette stand-ins. This PR does **not** overwrite them, and it does not change `content/packs/srd51.core/art/clips.json` or any monster content row. These frames show in game once R&P's render PR A adds the per-monster lookup.

## Layout

```
content/art/monster_standins/srd/{slug}/medium/{facing}/mon_{slug}_medium_{facing}_{clip}_{ff:02d}.png
content/art/monster_standins/srd/clips.json          # art manifest for these 26 kits
content/art/monster_standins/srd/SMOKE21.md … SMOKE30.md
```

`{facing}` is `se`, `sw`, `ne`, or `nw`. The 9 clips and frame counts match `MONSTER_STANDIN_SPEC.md`: idle 4, walk 6, react 3, attack 6, cast 4, stun 4, death 5, fall 3, fall_impact 3 (38 frames × 4 facings = 152 PNG per creature). Canvas 32×48, pivot (16,47).

## Roster (26 in PR #449)

| smoke | slugs in this PR |
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

## Held back (kept local, not in this PR)

Smokes #21–#30 originally covered 40 creatures. These 14 have no content spec row on main and were left out of PR #449. Their frames are kept local (source art under `/workspace/isoforge-default-art/`, plus a copy in `pr-a-dropped-14/`):

azer, barbed-devil, bearded-devil, chain-devil, erinyes, lemure, night-hag, rakshasa, doppelganger, mimic, swarm-of-beetles, swarm-of-centipedes, swarm-of-spiders, swarm-of-wasps

## Facings / mirrors

SW/NW are not exact mirrors of SE/NE. A pixel comparison of all 1,976 west frames (horizontal flip plus the known +1px pivot shift, matching engine `hflip_pivot`, and also the art-script variant that then clears columns 0 and 31) found:

- 1,086 / 1,976 exact under the art-script flip+shift+clear-edges transform. 200 of those also match engine `hflip_pivot` with no extra edge clear.
- 890 / 1,976 differ further (hand-retouched flips: many walk, attack, and death frames have large deltas; a smaller set are 2px-class edge or bridge touch-ups).
- No kit is an exact `hflip_pivot` match on every west frame. `swarm-of-bats` is the only kit whose 76 west frames are all exact under the clear-edges variant.

`clips.json` therefore leaves `mirrored_from` null so a lookup will not treat SW/NW as derived.

## Source

Source files were copied from the local art tree `/workspace/isoforge-default-art/monster-srd/` after the 2026-11-24 tiny/small/medium/large dedupe. Backups (`_v1`, `_v2`, `_soft`, `_soft2`) were not included.
