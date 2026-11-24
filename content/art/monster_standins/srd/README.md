# SRD medium monster art (PR-A prep, smokes #21–#30)

This directory stores the canonical 4-facing, 9-clip medium kits for the forty creatures in `SMOKE21.md`–`SMOKE30.md`. It is a local branch commit only (`art/monster-medium-pr-a`). No pull request has been opened.

## Why not `frames/{bin}/{facing}/`

The engine's stand-in loader is still bin-only. `MonsterStandinKit::load` (`game/presentation/src/standin.cpp`) reads:

`content/art/monster_standins/frames/{bin}/{facing}/mon_{bin}_{facing}_{clip}_{ff:02d}.png`

Those folders hold the generic silhouette stand-ins. This commit does **not** overwrite them, and it does not change `content/packs/srd51.core/art/clips.json` or any monster content row. Per-creature frames cannot be displayed until the engine can load a kit by creature as well as by bin.

## Layout

```
content/art/monster_standins/srd/{slug}/medium/{facing}/mon_{slug}_medium_{facing}_{clip}_{ff:02d}.png
content/art/monster_standins/srd/clips.json          # art manifest for these 40 kits
content/art/monster_standins/srd/SMOKE21.md … SMOKE30.md
```

`{facing}` is `se`, `sw`, `ne`, or `nw`. The 9 clips and frame counts match `MONSTER_STANDIN_SPEC.md`: idle 4, walk 6, react 3, attack 6, cast 4, stun 4, death 5, fall 3, fall_impact 3 (38 frames × 4 facings = 152 PNG per creature). Canvas 32×48, pivot (16,47).

## Roster

| smoke | slugs |
|---|---|
| 21 | azer, gargoyle, invisible-stalker, xorn |
| 22 | blink-dog, dryad, green-hag, satyr |
| 23 | sea-hag, barbed-devil, bearded-devil, chain-devil |
| 24 | erinyes, hell-hound, lemure, night-hag |
| 25 | rakshasa, succubus-incubus, basilisk, death-dog |
| 26 | doppelganger, ettercap, grick, harpy |
| 27 | medusa, mimic, rust-monster, gray-ooze |
| 28 | shrieker, violet-fungus, swarm-of-bats, swarm-of-beetles |
| 29 | swarm-of-centipedes, swarm-of-insects, swarm-of-poisonous-snakes, swarm-of-quippers |
| 30 | swarm-of-rats, swarm-of-ravens, swarm-of-spiders, swarm-of-wasps |

Source files were copied from the local art tree `/workspace/isoforge-default-art/monster-srd/` after the 2026-11-24 tiny/small/medium/large dedupe. Backups (`_v1`, `_v2`, `_soft`, `_soft2`) were not included.
