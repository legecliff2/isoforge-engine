# Content compatibility: schema-version gates + compat behaviour goldens

Shipped content packs must keep playing the same after an engine upgrade. A pack that was valid
under the old engine must keep the same mechanics under the new one. This doc gives the two rules
that make that true. The first case was PR #30 (feature hooks): fl1's `fighter_1.json` already
baked Dueling into its longsword (`1d8+5`). When the engine started applying `damage_bonus`
effects, that fighter would have hit for `1d8+7`.

## 1. Gate new mechanics by schema version

If a field that used to be descriptive becomes mechanical, or an existing field changes meaning,
the engine must not reinterpret old files.

- Bump the content type's schema version in `engine/core/assets` (`content_migrations.hpp`).
- Activate the new behaviour **only** for files authored at the new version.
- Add a migration from the previous version that keeps old files' meaning explicit. For example,
  `character_2_to_3` marks each `features[]/traits[]` `effect` `"inert": true`.
- The consumer (`sim::ContentLibrary::add`) honours the marker. Inert effects are listed in
  `Archetype::inert_features` and are never applied.
- Migrations chain (v1 -> v2 -> v3), so every older file goes through the gate.
- Content that wants the new behaviour is moved to the new version. Its numbers are de-baked in the
  same change: the sample fighter is `character@3` with longsword `1d8+3` plus an active Dueling
  effect.
- Add the matching `schemas/<type>/vN.schema.json`. Tools & Content owns these files, so the
  engine adds a minimal one and flags it to T&C.

Current gates:

| type | version | gate |
|---|---|---|
| character | @3 | feature effects (`damage_bonus`, `bonus_action_grant`, `self_heal`, ...) are active only for @3 files. Effects from @1/@2 files are inert. |
| character, monster | @3 | **defenses and multi-component damage**: `damage_resistances`, `damage_immunities`, `damage_vulnerabilities`, `condition_immunities` (mapped onto `rules5e::Combatant` masks) and every `damage[]` entry after the first (`Attack::extra_damage`). Pre-@3 files listed these as text and the engine read none of them (only `damage[0]`). Migration 2 -> 3 records the ones a pre-@3 file carries in a top-level `inert_mechanics` array (`damage_resistances`, ..., `damage[1..]`); `ContentLibrary` ignores exactly those and lists them in `Archetype::inert_mechanics`. Files carrying none are left byte-identical. Pinned by `compat_defenses_tests.cpp`. |
| character, monster | @3 | **creature type, size, spellcasting**: `type` -> `Combatant::creature_type`, `size` -> `Archetype::size` (stored only), and the `spellcasting` block (`ability`, `caster_level`, `slots`, `pact`, `spells`) -> slots / caster level / ability / `spells_known`, which lets the creature cast through `sim::Combat` (CastSpell, event schema v5). The 2 -> 3 migration adds `spellcasting` to `inert_mechanics` when a pre-@3 file carries a block, and `creature_type` when its `type` is not `humanoid` (the pre-@3 sim treated everything as Humanoid); plain humanoid files stay byte-identical. `size` is read for every version (no behaviour). Pinned by `compat_defenses_tests.cpp` (pre-@3 decorated with spellcasting and `undead` still reproduces the fl1 digests) and `casting_tests.cpp` (caster golden, pinned once on the engine that first cast). |
| monster | @3 | the same gate (`monster_2_to_3`). The sample goblin is `monster@3`: Nimble Escape is active, and no numbers are baked in (`bonus_actions` is empty, the scimitar is a plain `1d6+2`). fl1's monster@1 goblin keeps Nimble Escape descriptive. |

## 2. Pin old content with a compat behaviour golden

The `tests/compat/<pack>` fixtures are frozen copies of shipped content. Never edit them.
Each fixture gets a behaviour golden. fl1 has two:

- `engine/core/sim/tests/compat_fl1_tests.cpp`: the fighter fight.
- `compat_defenses_tests.cpp`: fl1 copied to a temp dir with resistances/immunities/vulnerabilities/condition immunities and a second damage entry bolted onto both stat blocks (declared @1 and @2). Fights must equal the undecorated fl1 goblin digests: the decoration was never read, so the pins are the same numbers, computed on main `6012567` before the gate.
- `compat_fl1_goblin_tests.cpp`: the goblin's stat block, an inert Nimble Escape, and per-goblin attacks, hits, damage dealt and taken, and final hp, over 5 seeds. Pinned on main 19a372a, before the monster gate.

Each golden has these parts:

- **Semantics check:** stat blocks as the engine sees them. For example, the fl1 longsword is
  `1d8+5` with to-hit 5.
- **Seeded fights:** a few seeds run a **fixed command script** that lives in the test file. The
  script is: attack the first conscious hostile when adjacent, else step along the path, else end
  the turn; always take reaction windows. The script is melee-only, so new ranged or line-of-sight rules can't move it. It never uses the built-in policy or Luau AI, so AI and
  policy work does not change these values. Only rules-level behaviour does.
- **Digest:** an FNV hash of the rules events present since sim v0 (initiative, turns, moves,
  attacks, damage, downs, deaths, death saves, encounter end), plus final hp, dead flag and position
  per combatant. Event-schema versions and newly added event kinds are excluded. Additive event
  contract changes therefore do not trip the golden.
- **Pin values:** computed on the engine **before** the change under test (fl1: `main` at
  `c09ea79`, before #30).

**Policy:** these values never change. If the golden fails, the engine has changed what old
content does. Fix the engine (add a schema gate); do not re-pin. The only exception is an explicit
Director-approved rules fix, recorded in the PR.
