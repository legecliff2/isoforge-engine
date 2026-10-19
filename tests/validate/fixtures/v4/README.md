# @4 / item@1 validator fixtures

- `pack/`: a valid fixture pack (monster@4, character@4, item@1) for `test_v4_validator.py`; the runner wraps it in a
  throwaway pack `test.v4` (api_level 2, depends on `srd51.core`). Table-driven negative cases edit one document.
- `at3/`: decorated `@3` documents (VERSION_BUMP_BATCH.md section 8.4): a goblin with a descriptive `special` breath
  weapon, `reactions[]` and `bonus_actions[]`; a fighter with `on_event` / `apply_modifier` trait effects; a file
  carrying `inert_mechanics` from the 2->3 migration (merge test); and a file whose `on_event` has bogus parameters.
  `test_migrations_v4.py` runs them through the Python reference migration and validates the result in memory.
  They are inputs for Engine Core's digest goldens (row 3: migrated form must digest like the `@3` source).

These files are not part of any shipped pack and are not under `tests/compat` (that corpus is frozen).

- `expected4/`: the Python reference migration's output for each `at3/` file (same file name), written with sorted keys.
  Engine Core's C++ test compares `monster_3_to_4` / `character_3_to_4` output against these as JSON.
  Mapping: decorated_goblin and merge_goblin use monster_3_to_4; decorated_fighter and bogus_inert_fighter use character_3_to_4.

- `extra/`: valid documents for `test_v4_validator.py` `TimedModifiersAndBonusAttacks` (added on top of `pack/` one at a time):
  a character@4 using every timed-modifier property (`consume`, `speed_sub`, `ac_floor` / `ac_floor_dex`, `per_turn`, `when`),
  spell@1 `apply_modifier` spells (True Strike, Guiding Bolt rider) and a monk with the bonus-attack feature conventions
  (`martial_arts`, `flurry_of_blows` + `ki` pool, `two_weapon_fighting`, `light` / `unarmed` / `monk` properties).
