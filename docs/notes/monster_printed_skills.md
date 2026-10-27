# Design note: monster printed skills (Lich-approved 2026-10-24)

Status: design only. Nothing here is implemented on main. The implementation lands later as its own determinism PR in a
VERSION_BUMP batch that does nothing else, and Wizard approves its golden regen.

## Problem (evidence, refreshed against origin/main 5e38517)

No loader reads a stat block's `skills` object. `Combatant::skill_proficiencies`
(engine/rules/include/rules5e/types.hpp:510) is never set from content. The only non-test code touching it copies or merges
it: engine/core/sim/src/combat.cpp:2671, engine/rules/src/encounter.cpp:6440, 6451 and 6470. Only the Python validator reads
`skills` (tools/validate/validate_packs.py:230-234). `skill_modifier` (engine/rules/include/rules5e/checks.hpp:12) adds
proficiency only when that bit is set. So every monster's skill checks today use the ability modifier alone. That covers
Hide's Stealth roll (encounter.cpp:5486), passive Perception against hiders (encounter.cpp:5495, checks.hpp:19), surprise
(encounter.cpp:6339), and the grapple/shove Athletics/Acrobatics contests (encounter.cpp:4648, 4773-4796). Printed totals
such as the goblin's `"stealth": 6` are dropped, and so are expertise totals. (Historical, from the original 7a0e59d survey: on the Tier-A stack the same lines sit at
checks.hpp:12-22 and encounter.cpp:5615/5624/6451, with the §25 override in passive_perception.)

## Rulings

1. **Load monster `skills` as printed totals.** Add a per-skill `int8` override on the Combatant (presence mask plus value);
   absent means derived. Proficiency and expertise bits are not inferred, so the printed total wins. PCs stay derived
   (character files are untouched).
2. **`skill_modifier`** returns the override when there is one, plus the effective-score modifier delta:
   `modifier(current score) - modifier(score before any ability_reduction)`. That way Strength drain (shadow,
   feat/monster-strength-drain) or a future Wisdom drain moves a printed Athletics or Perception exactly as it moves a
   derived one.
3. **Derived passive Perception** becomes `10 + printed Perception` when one is printed, else `10 + Wis mod`. The §25
   comparison (`passive_perception_reason: srd_printed`, validator and loader) is rebased on that rule. The census is below.
4. **Static content, so no hash tag.** The override is archetype data, like `passive_perception_override` (§25, never
   hashed). It changes outcomes only through rolls and comparisons, which the existing hashes already cover.
5. **Keen senses** are mechanical in CONTENT_CONVENTIONS §30 (advantage on a Wisdom (Perception) check that relies on a listed sense). They do not change the printed passive, and this note does not specify them.

## Passive Perception census under rule 3

Source: 5e-bits/5e-database a6212be `src/2014/en/5e-SRD-Monsters.json` (the table pinned in
tests/validate/data/srd51_monsters_5e_database_a6212be.json). There are 334 monsters; 333 print a passive Perception. A
mismatch means printed passive != 10 + printed Perception (or 10 + Wis mod when no Perception is printed).

| Monster | Printed passive | Rule 3 | Perception in table | Note |
|---|---|---|---|---|
| black bear | 13 | 11 | none | The table has no Perception proficiency. Our pack prints `perception: 3`, which gives 13, so it matches once loaded. |
| blink dog | 10 | 13 | +3 | Real SRD inconsistency: keep `srd_printed`. |
| half-red dragon veteran | 12 | 10 | none | The table has no Perception; check the SRD text before authoring. |
| spider | 12 | 10 | none | Same as above. |
| swarm of ravens | 15 | 11 | none | Same as above. |

Result: the same five names as before. Under the engine today (Wis mod only) 167 of the 333 would mismatch. With rule 3
the only remaining mismatches are blink dog, plus the four where the table carries no Perception skill. Of the five, only
black bear is authored in srd51.core, and it matches through its printed skill. At origin/main 7a0e59d (historical), 0 of the 54 authored
monster files mismatch under rule 3.

## Impact estimate (scratch implementation, discarded)

Historical (measured at origin/main 7a0e59d, not re-run on 5e38517): I implemented rules 1-2 on origin/main 7a0e59d in a throwaway worktree (override mask and value on Combatant, a
`skills` parse for monster documents in ContentLibrary::add, the early return in skill_modifier; main has no
ability_reduction, so the delta is 0 there). Then I ran the full ctest on linux-clang-release: 48/48 passed, including
rules golden_tests, the tests/goldens stream goldens, and the sim compat and replay tests. A probe confirmed the change was
live: the goblin's Stealth went from +2 to +6. The scratch code and its build dir are deleted.

**Goldens and replay hashes that move: none.** No golden or replay scenario has a monster with a printed skill make a
skill check. The fixtures never Hide or run stealth surprise, and their grapples and shoves never involve a monster with
printed Athletics/Acrobatics (ape, grimlock). The 26 authored monsters whose numbers change (every check that uses those
skills) are: ape, black bear, black dragon wyrmling, brown bear, bugbear, cultist, dire wolf, giant hyena, giant poisonous
snake, giant spider, giant weasel, giant wolf spider, goblin, grimlock, guard, hyena, jackal, lizardfolk, mastiff, medusa,
noble, orc, scout, spy, wolf and worg. The implementation PR should still regenerate goldens under Wizard's approval and
should add fixtures that do move: a goblin Hide against passive Perception, and an ape grapple contest.
