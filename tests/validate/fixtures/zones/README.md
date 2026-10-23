# spell@1 `zone` fixtures (R-G11)

Valid spell@1 documents using the `zone` effect kind (docs/CONTENT_CONVENTIONS.md section 4), namespace `test.zone`:
Moonbeam (point cylinder, `move` with an action), Spirit Guardians (creature aura, `chooses_excluded`, `halve_speed`, `needs_conscious_owner`),
Fog Cloud / Darkness / Silence (flags only), Spike Growth (`zone_move_within`, no-save `damage`, group 0),
Web (`own_turn_only`, `while_in_zone`), Grease (`zone_appear` / `zone_enter` / `zone_end_turn`, no concentration),
Entangle (`zone_appear` + `while_in_zone`, Strength save; the SRD 20-ft square is a cube, see below).
`tools/validate/test_validate_packs.py` `SpellZones` validates them as one pack and runs its table of negative edits;
`tests/validate/cases/zone_*` are the run_negative / spell_loader_tests copies.

They are not part of any shipped pack (srd51.core gets zone spells in a later PR) and are not
under `tests/compat` (that corpus is frozen). `spell_loader_tests` loads them as positive loader fixtures.

`spells_pr2/` (R-G11 rules PR 2 keys, CONTENT_CONVENTIONS 4.6): Stinking Cloud (`loses_action`, `auto_success_*`), Wall of Fire
(`line`, `ring_size_ft`, `sided`, `side_ft`, `region`), Warding Aura (test-only D7 aura fields). Fog Cloud, Moonbeam and Spirit
Guardians above also carry `upcast_size_ft`, `shapechanger_disadvantage` and `evil_damage_type`.

## Entangle (`spells/entangle.json`)

Positive specimen for T&C to author `srd51.spell.entangle` in `srd51.core`. It reuses Web's frozen zone keys
(`shape`, `size_ft`, `anchor`, `flags`, `triggers[].on`, `while_in_zone`, `effect.kind` / `ability` / `condition`).
No new key and no new hash tag.

| | Web | Entangle |
|---|---|---|
| area | point `cube` 20 | point `cube` 20. The SRD area is a 20-ft square; `AreaShape` has cube, and Grease's square is the same cube |
| terrain | `difficult`, `light_obscured` | `difficult` |
| who saves | `zone_start_turn`, and `zone_enter` with `own_turn_only` | `zone_appear` only (creatures in the square when it appears) |
| save | Dexterity, or restrained | Strength, or restrained |
| escape | `while_in_zone`: an action, a Strength check against the zone DC | the same existing escape |
| duration | concentration, 1 hour | concentration, 1 minute |
| cast | level 2, range 60, sorcerer/wizard, V S M | level 1, range 90, druid, V S |

`while_in_zone` is the flag that binds the restraint to the zone, so the Strength check, leaving the square, and the
spell ending (concentration or the minute timer) all clear it. The SRD line is "until the spell ends"; a forced exit
also ends this specimen, because that is how `while_in_zone` works. Copy the `effects` array and re-id the document
`srd51.spell.entangle`.
