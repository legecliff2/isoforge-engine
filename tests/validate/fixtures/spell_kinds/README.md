# spell@1 R-G15 kind fixtures (sim #120)

Valid spell@1 documents for the kinds the loader compiles since #120 (docs/CONTENT_CONVENTIONS.md section 6), namespace
`test.kinds`: Sleep and Color Spray (`hp_pool`), Dispel Magic (`dispel` with the `check` object), Revivify (`revive`, priced
consumed material with `components.m.item` `srd51.item.diamond`), Lesser Restoration (`end_condition`), Acid Arrow (`spell_attack` + `delayed_damage` rider) and Heat Metal
(`sustained_damage`). `tools/validate/test_validate_packs.py` `SpellKindsR120` validates them as one pack and runs its negative
table; `tests/validate/cases/kinds_*` are the run_negative / spell_loader_tests copies. Not part of any shipped pack and not under
`tests/compat` (frozen).
