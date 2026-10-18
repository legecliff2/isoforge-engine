# @4 / item@1 validator fixtures

- `pack/`: a valid fixture pack (monster@4, character@4, item@1) for `test_v4_validator.py`; the runner wraps it in a
  throwaway pack `test.v4` (api_level 2, depends on `srd51.core`). Table-driven negative cases edit one document.
- `at3/`: decorated `@3` documents (VERSION_BUMP_BATCH.md section 8.4): a goblin with a descriptive `special` breath
  weapon, `reactions[]` and `bonus_actions[]`; a fighter with `on_event` / `apply_modifier` trait effects; a file
  carrying `inert_mechanics` from the 2->3 migration (merge test); and a file whose `on_event` has bogus parameters.
  `test_migrations_v4.py` runs them through the Python reference migration and validates the result in memory.
  They are inputs for Engine Core's digest goldens (row 3: migrated form must digest like the `@3` source).

These files are not part of any shipped pack and are not under `tests/compat` (that corpus is frozen).
