# Content contract: feature effects, damage, range, tile flags

Part of the content compatibility contract (ARCHITECTURE §3). Machine-checked by
`schemas/common/v1.schema.json` (`$defs/effect`, `effect_cost`, `effect_condition`) and
`tools/validate/validate_packs.py`. The engine side is `eng::sim::FeatureRegistry` (`sim/features.hpp`).

## 0. Version gating: when effects are applied

| document | effects | validator |
|---|---|---|
| `character@3`, `monster@3` | **applied by the engine** | kind-contract violations are errors; `cost: reaction` is an error (reserved) |
| `character@1` / `@2`, `monster@1` / `@2` | **inert descriptions**: the engine's @2->@3 migrations mark each effect `"inert": true` and never apply it, because those stat blocks may bake the numbers in (fl1's fighter writes `1d8+5` with Dueling included) | kind-contract findings are warnings only |

Effects are applied by the engine in @3 files, so the numbers an effect adds must **not** be
baked into the stat block. Dueling's +2 is not in the fighter's longsword damage (write `1d8+3`,
not `1d8+5`), and a monster `damage_bonus` is not folded into its action damage. To activate
effects on an old file, move it to @3 and de-bake its numbers in the same change. The sample
goblin is `monster@3`: Nimble Escape (`bonus_action_grant`) has no number to bake.

`inert` is reserved for the engine migrations. Authoring it is an error in every version of
every type, on effects and (at `@4`) on `reactions[]` / `bonus_actions[]` entries. The `@4`
schemas accept it so an in-memory migration result validates; a file on disk that carries it fails.

### 0.1 `@4` (docs/proposals/VERSION_BUMP_BATCH.md, row 2: schemas and validator only)

| document | effects | validator |
|---|---|---|
| `character@4`, `monster@4` | the `@3` kinds unchanged, plus `on_event`, `apply_modifier`, `grant_action`, `heal_pool`, `gaze` (closed arms of `common@2 $defs/effect`; `gaze`: VERSION_BUMP_BATCH 7.2p, `docs/CONTENT_CONVENTIONS.md` section 11); `cost: reaction` is legal | `checks_v4.TAG_TABLE` row; cross-reference checks 1-18 of the proposal's section 8.3 |
| a `3 -> 4` migrated file | effects of the frozen new-kind list (`on_event`, `apply_modifier`, `grant_action`, `heal_pool`, `save_effect`, `reaction`) carry `"inert": true`; parameters are **not** validated | accepted in memory; an authored `inert` is an error |
| `monster@5` (batch 3) | monster-local trait kinds `weapon_extra_dice`, `save_advantage`, `aura` (ghast Stench: `{radius_ft, on: start_turn, save {ability, dc}, condition, duration: until_start_of_next_turn, success_immunity_hours?}`, a creature-anchored aura from the start of combat; sim features.cpp `apply_aura`), `keen_senses` (`{senses: [sight\|hearing\|smell]}`; CONTENT_CONVENTIONS §30; sim features.cpp `apply_keen_senses`), `regen` (`{amount, suppressed_by}`; CONTENT_CONVENTIONS §31; sim features.cpp `apply_regen`), `freeze` (`{by, speed_sub}`; CONTENT_CONVENTIONS §42; sim features.cpp `apply_freeze`), `absorb` (`{by}`; CONTENT_CONVENTIONS §43; sim features.cpp `apply_absorb`), `innate_invisibility` (no fields; CONTENT_CONVENTIONS §44; sim features.cpp `apply_innate_invisibility`), and `horrific_appearance` (`{radius_ft, save, immunity_hours}`; CONTENT_CONVENTIONS §45; sim features.cpp `apply_horrific_appearance`). Death Glare is a monster@5 `save_effect` with `drop_to_zero`, `target_has_condition: frightened`, and `caster_sees_target` (CONTENT_CONVENTIONS §46) | `checks_v4.check_aura`, `check_keen_senses`, `check_regen`, `check_freeze`, `check_absorb`, `check_innate_invisibility`, `check_horrific_appearance`, and `check_death_glare` mirror the loader refusals |