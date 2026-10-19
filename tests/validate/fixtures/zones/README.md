# spell@1 `zone` fixtures (R-G11)

Valid spell@1 documents using the `zone` effect kind (docs/CONTENT_CONVENTIONS.md section 4), namespace `test.zone`:
Moonbeam (point cylinder, `move` with an action), Spirit Guardians (creature aura, `chooses_excluded`, `halve_speed`, `needs_conscious_owner`),
Fog Cloud / Darkness / Silence (flags only), Spike Growth (`zone_move_within`, no-save `damage`, group 0),
Web (`own_turn_only`, `while_in_zone`), Grease (`zone_appear` / `zone_enter` / `zone_end_turn`, no concentration).
`tools/validate/test_validate_packs.py` `SpellZones` validates them as one pack and runs its table of negative edits;
`tests/validate/cases/zone_*` are the run_negative / spell_loader_tests copies.

They are not part of any shipped pack (srd51.core gets zone spells in a later PR) and are not
under `tests/compat` (that corpus is frozen). `spell_loader_tests` loads them as positive loader fixtures.

`spells_pr2/` (R-G11 rules PR 2 keys, CONTENT_CONVENTIONS 4.6): Stinking Cloud (`loses_action`, `auto_success_*`), Wall of Fire
(`line`, `ring_size_ft`, `sided`, `side_ft`, `region`), Warding Aura (test-only D7 aura fields). Fog Cloud, Moonbeam and Spirit
Guardians above also carry `upcast_size_ft`, `shapechanger_disadvantage` and `evil_damage_type`.
