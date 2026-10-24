# Legal actions and item material: golden and AI impact

Branch `fix/rules-legal-actions-spells`. Two rules changes sit under this note:

- `legal_actions` offers a self-range modifier (Blur, Expeditious Retreat) with `target` = the caster, and offers Heat Metal once per eligible metal item (`equip_item` from `manufactured_metal()`).
- item `material` decides that eligibility. Absent is non-metal. The field is not hashed.

## Goldens

No golden pin was edited. `ItemDef::material` is not folded into `loadout_digest` (0xD0) or `Combat::hash`, and the new `legal_actions` rows are not a hash input. A scripted fight submits the commands in the test, not the legal list, so a longer list does not move a scripted digest.

Pinned suites that cover this (unchanged pins, re-run on this branch):

| Suite | What it pins | Why this branch does not move it |
|---|---|---|
| `rules5e_tests` `golden: dice expression rolls` and `golden: d20 modes, initiative, attacks, damage` | rules dice / attack hashes | no material, no `legal_actions` |
| `sim_tests` `compat fl1` / `compat fl1 goblin` / `compat defenses` | frozen fl1 fights, scripted melee | script never reads `legal_actions`; fl1 has no Heat Metal |
| `sim_tests` `caster golden` | scripted wizard vs goblins | the script casts from its own list, not `legal_actions` |
| `sim_tests` `event goldens` | `NearestEnemyPolicy` + `run_to_end` on the sample chunk, digest `7766538371569885080` | the sample fight has no Blur or Heat Metal caster |
| `sim_tests` `equipment end-to-end` | scripted equipment fight, hash `18080299159498595558` | scripted; material is not in the hash |
| `vertical_slice_tests` | headless slice, including `log_hash` | same driver; the slice party does not cast these spells |
| `procgen_tests` content-hash golden | chunk hashes | item JSON is not in the chunk hash |

## AI and playtest replays

No scenario changes its choice.

The headless driver (`engine/core/sim/src/driver.cpp`, `next_command` / `run_to_end`) does not pick a `CastSpell` out of `legal_actions()`. It builds spell commands itself:

- Modifier spells (Blur and the other self-range modifiers) are never cast. The driver says it never casts modifier spells.
- Heat Metal is never cast. `spell_kind_command` only sustains a sustained-damage effect that is already on the board, and that request sets `sustain`, not `equip_item`. The new per-item cast rows are never selected.
- `legal_actions()` is read in two places, both filtered to `ActionKind::UseObject`: `equipment_command` (draw / stow / drop / pick up) and `light_command` (light / douse). Extra `CastSpell` rows do not change which of those entries is first.

So a bard, druid, or sorcerer who knows Heat Metal or Blur (`srd51.core` `bard_level_3`–`5`, `druid_level_4`–`5`, `sorcerer_level_4`–`5`) still does not cast them under this policy. Nothing in the AI or playtest replays newly prefers those rows.

The equipment AI replay (`equipment AI replay golden`, hash `363147187033031642`) includes a javelin thrower. Javelin `material` is now `wood`. That value is not an attack input and not a hash input, and the driver does not Heat Metal the javelin, so the thrown attack and the pin stay as they are.

## Verification

`tools/ci_local.sh` passed with the pins above still in the tree (no golden file edited).

- GCC Debug: 48/48. That includes `content_validate` and the `content_validate_*` suite (`content_validate_equipment` among them), `rules5e_tests` (the two rules goldens, spell batch zh, legal spell actions), `sim_tests` (compat fl1, compat fl1 goblin, compat defenses, caster golden, event goldens, the scripted equipment end-to-end pin, and the AI fights: equipment replay, reckless, stand up, spell follow-ups), `vertical_slice_tests`, and `procgen_tests`.
- Clang Release: 37/37. Same C++ suites. This job follows CI and skips the `python_only` label; `content_validate` itself still ran.

Nothing failed, and no pin was rewritten.
