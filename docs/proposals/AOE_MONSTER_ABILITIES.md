# Design note: monster area abilities on the spell area code

Status: accepted for PR 1 of the stack (tests and this note). Read against main `1103708` (#284). This PR does not change behaviour, and it does not edit `docs/SIM_EVENTS.md`.

## 1. What exists today
- Shapes: `rules5e::AreaShape {None, Cone, Cube, Cylinder, Line, Sphere}` (`engine/rules/include/rules5e/spells.hpp:21`). `SpellDef::area` / `area_size_ft` / `area_width_ft` / `area_height_ft` (`:361`). Content parse: `engine/core/sim/src/content.cpp:816`.
- Membership: `Combat::area_targets` (`engine/core/sim/src/combat.cpp:229-317`). It works on a doubled-coordinate integer grid, with no floats.
  - Cone: half-angle atan(1/2).
  - Line: perpendicular distance <= width/2.
  - Cube: either self-adjacent or centred on the aim tile.
  - Sphere and Cylinder: Chebyshev radius.
  - It already handles footprints: the origin is the centre of the caster's footprint, LoS is taken from any caster tile, and a target is hit if any of its footprint tiles is inside and seen.
- Saves: `SpellEffectKind::Save` with `OnSave::Half` (`spells.hpp:26`, enum at `:90`, fields at `:142` and `:331`). Half damage is integer division.
- Recharge: `recharge_min` on innate spells (`engine/rules/include/rules5e/types.hpp:192-203`) and on triggers (`:274-276`). A trigger `usage.recharge` is compiled at `content.cpp:533` (the comment at `:500` still says recharge is listed and not compiled; the branch below it does compile `recharge_min`). An action `usage.recharge` is compiled by `compile_action_usage` at `content.cpp:626` (`min` 2..6, `usage_kind` 1). The d6 is rolled at the start of the owner's turn and emits `RechargeRolled` (v7, `encounter.hpp:291`; `ActionUse` at `:521-524`).
- Multi-tile (#50/#51/#55/#180): `docs/proposals/LARGE_CREATURE_FOOTPRINT_AUDIT.md`, PRs A, B and C are done. That covers the footprint, occupancy, pathing, LoS, reach, OAs and the event schema v6 `Footprint` kind. Still open (listed again in the PR description; not this stack's work):
  - Cover for footprints.
  - Squeezing.
  - Grapple size limits.
  - Tiny/Small sharing a space.
  - Swarms.
  - Luau script-host exposure of the footprint.
  - Same-tile stacking of 1x1 creatures at spawn.
- Tags: `docs/SIM_EVENTS.md` at this commit still says the next free tag is **0xD6**. Open PR #287 (altitude PR1) takes 0xD6 and reserves 0xD7, and its draft of that file reserves 0xD8–0xDA for this stack. This PR does not allocate them. See section 4.

## 2. Proposal: one area path
1. **Reuse, don't fork.** A monster area action compiles into a synthetic `SpellDef` that has `area`, `area_size_ft`, `area_width_ft`, `save_ability`, `on_save=Half`, its dice and a fixed `save_dc`. This is the same pattern recharge actions already use (`encounter.hpp:521`, keyed by actor plus synthetic spell id). Targets come from `Combat::area_targets`, so nothing new is needed in rules for geometry.
2. **Recharge.** Use the existing `ActionUse` with `recharge_min`. Firing it expends the use, and the turn-start d6 restores it, so no new RNG draw site is added. Per-day uses (`uses_max`) take the same path.
3. **Saves.** Each target in `area_targets` order (slot order, which is deterministic) rolls once. A multi-tile creature is one target and rolls once, however many of its tiles are covered (SRD). Evasion and Legendary Resistance hooks stay where they already are for spells.
4. **Multi-tile origins.** A cone or a line starts at the centre of the footprint. A self-adjacent cube touches the edge of the footprint. The centre is what the sim does; it is not the SRD origin. Length measured from the centre stops short of the creature's edge (section 6). PR 1 pins that behaviour for 2x2, 3x3 and 4x4, odd and even centres, in every one of the 8 directions.
5. **Altitude forward-compat** (owned by another engineer; this note only reserves the hooks):
   - Absolute height is `h = ground_elev*5 + alt_ft`.
   - Keep `inside()` as the 2D test and add an optional vertical predicate that defaults to true:
     - Sphere: 3D Chebyshev, `|h_t - h_o| <= r`.
     - Cylinder: `0 <= h_t - h_base <= area_height_ft`.
     - Cube: `[h_o, h_o+side]`.
     - Cone and Line: slope check on the dz/dist ratio, using the same integer math.
   - A creature's vertical extent is `[h, h + height(size)]`, and any overlap counts as a hit.
   - Until altitude lands, every `h` is 0, so digests stay byte-identical.
   - Don't fold `h` into any area tag yet.

## 3. Bestiary v0.1 coverage

The draft pointed at `/workspace/bestiary_v01_missing.txt`. That file is not in the repository. The v0.1 list is the monsters named in the draft's section 3. Each one was checked against SRD 5.1 as mirrored by 5e-database commit `a6212beb4b278917c2cff41c2b04c4ef4f3e0d6f` (`src/2014/en/5e-SRD-Monsters.json`), the snapshot `docs/SRD_COMBAT_COVERAGE.md` pins. No other dragons were added.

The draft's table was wrong in these places: the young black dragon is a line, not a cone; the young white dragon's cold breath is Constitution DC 15, not Dexterity; the magma mephit has a Fire Breath as well as its death burst; the steam mephit's Death Burst is not half damage; the gorgon recharges on 5–6; the ankheg recharges on 6; Whelm recharges on 4–6 and a success pushes the creature out rather than halving damage; ghast Stench is an area. Brass and copper each have one Breath Weapons action with two shapes sharing one recharge.

### Area templates (breath and spray)

These are the `save_effect` actions a later content PR compiles. "Half" is `OnSave::Half`. "None" means the success text is not half damage.

| Monster | Size | CR | Action | Shape | Save | On success | Damage | Recharge |
|---|---|---|---|---|---|---|---|---|
| Young Black Dragon | Large | 7 | Acid Breath | line 30 ft × 5 ft | Dex DC 14 | half | 11d8 acid | 5–6 |
| Young White Dragon | Large | 6 | Cold Breath | cone 30 ft | Con DC 15 | half | 10d8 cold | 5–6 |
| Young Brass Dragon | Large | 6 | Breath Weapons, Fire Breath | line 40 ft × 5 ft | Dex DC 14 | half | 12d6 fire | 5–6, shared |
| Young Brass Dragon | Large | 6 | Breath Weapons, Sleep Breath | cone 30 ft | Con DC 14 | none (unconscious 5 minutes) | none | 5–6, shared |
| Young Copper Dragon | Large | 7 | Breath Weapons, Acid Breath | line 40 ft × 5 ft | Dex DC 14 | half | 9d8 acid | 5–6, shared |
| Young Copper Dragon | Large | 7 | Breath Weapons, Slowing Breath | cone 30 ft | Con DC 14 | none (slowed 1 minute) | none | 5–6, shared |
| Hell Hound | Medium | 3 | Fire Breath | cone 15 ft | Dex DC 12 | half | 6d6 fire | 5–6 |
| Chimera | Large | 6 | Fire Breath | cone 15 ft | Dex DC 15 | half | 7d8 fire | 5–6 |
| Dust Mephit | Small | 1/2 | Blinding Breath | cone 15 ft | Dex DC 10 | none (blinded) | none | 6 |
| Ice Mephit | Small | 1/2 | Frost Breath | cone 15 ft | Dex DC 10 | half | 2d4 cold | 6 |
| Steam Mephit | Small | 1/4 | Steam Breath | cone 15 ft | Dex DC 10 | half | 1d8 fire | 6 |
| Magma Mephit | Small | 1/2 | Fire Breath | cone 15 ft | Dex DC 11 | half | 2d6 fire | 6 |
| Gorgon | Large | 5 | Petrifying Breath | cone 30 ft | Con DC 13 | none (restrained, then petrified) | none | 5–6 |
| Ankheg | Large | 2 | Acid Spray | line 30 ft × 5 ft | Dex DC 13 | half | 3d6 acid | 6 |

Ankheg Acid Spray is only usable while the ankheg is not grappling a creature. Gorgon petrification uses the condition path (section 5, PR 4). Brass and copper choose one breath when the shared recharge is available.

`recharge_min` already supports 2..6. Dragon breaths, the hell hound, the chimera and the gorgon are 5–6. Mephit breaths and the ankheg are 6.

### Death bursts

These are triggers on death, emitted through the existing `FeatureTriggered`. They are not recharge actions. A 5 ft burst maps to a self-origin sphere of radius 1 tile. That radius is today's Chebyshev test ("within 5 feet" on this grid). The square-sphere gap in section 6 stays a follow-up; this mapping does not treat the square as the SRD circle.

| Monster | Size | CR | Shape | Save | On success | Effect |
|---|---|---|---|---|---|---|
| Dust Mephit | Small | 1/2 | within 5 ft | Con DC 10 | none | blinded 1 minute, repeat save |
| Ice Mephit | Small | 1/2 | within 5 ft | Dex DC 10 | half | 1d8 slashing |
| Steam Mephit | Small | 1/4 | within 5 ft | Dex DC 10 | none | 1d8 fire on a failure only |
| Magma Mephit | Small | 1/2 | within 5 ft | Dex DC 11 | half | 2d6 fire |

Innate spells on the same stat blocks stay on the spell path, not `area_targets`: dust mephit sleep, ice mephit fog cloud, steam mephit blur, magma mephit heat metal (spell save DC 10).

### Creatures in its space

Not a cube template. PR 5 stacks these on the zones/auras work, because they need "a creature in this footprint's space".

| Monster | Size | CR | Action | Who | Save | On success | Damage | Recharge |
|---|---|---|---|---|---|---|---|---|
| Air Elemental | Large | 5 | Whirlwind | each creature in its space | Str DC 13 | half the bludgeoning damage; not flung | 3d8+2 bludgeoning, then flung 20 ft and knocked prone | 4–6 |
| Water Elemental | Large | 5 | Whelm | each creature in its space | Str DC 15 | pushed out of the space (not half) | 2d8+4 bludgeoning; Large or smaller is grappled | 4–6 |

A flung whirlwind target that hits something, and the Dexterity save of a creature it is flung at, are riders on the failure. They are not the area save.

### Aura, not a breath

| Monster | Size | CR | Ability | Area | Save | Effect |
|---|---|---|---|---|---|---|
| Ghast | Medium | 2 | Stench | a creature that starts its turn within 5 ft | Con DC 10 | poisoned until the start of its next turn; a success grants 24 hours of immunity |

Turn Defiance (ghouls within 30 ft have advantage on saves against turning) is not an attack template.

### Named in the v0.1 list, and not an area attack

| Monster | Size | CR | Why it is out of scope |
|---|---|---|---|
| Earth Elemental | Large | 5 | Slam, Earth Glide, Siege Monster. No area. |
| Fire Elemental | Large | 5 | Fire Form is contact and entering a space. Illumination is light, not an attack. |
| Salamander | Large | 5 | Heated Body is contact damage, not an area. |
| Shield Guardian | Large | 7 | Spell Storing only. No inherent area. |
| Ettercap | Medium | 2 | Web is a single-target ranged attack, 30/60 ft, recharge 5–6. Not a line or a cube. |
| Wyvern | Large | 6 | Bite, claws and stinger. The stinger is a single-target poison save. |
| Couatl | Medium | 4 | Innate spells (detect, bless, cure wounds, sanctuary, shield, dream, and the rest). No area action. |
| Lamia | Large | 4 | Innate spells. Intoxicating Touch is a single-target melee spell attack. |
| Pseudodragon | Tiny | 1/4 | Bite and sting. No breath. |
| Wraith | Medium | 5 | Life Drain is single-target. Create Specter is one corpse within 10 ft. |

## 4. Proposed SIM_EVENTS tags (from 0xD8)

Not allocated by this PR. `docs/SIM_EVENTS.md` is left as it is on main. #287 claims 0xD6 (`Combat::hash`, `alt_ft`) and reserves 0xD7 (`log_hash`, `ActionRequest::alt_to`). That PR's draft also reserves the three rows below for this stack and moves the next free tag to 0xDB. Fill 0xD8 and 0xD9 in the sim-wiring PR, and 0xDA only with the altitude hook, after #287 has landed.

| Tag | Where | Payload | Gate |
|---|---|---|---|
| 0xD8 | `log_hash` | area resolution: action/spell id u32, shape u8, origin x/y i32, aim x/y i32, size_ft u16, width_ft u16, target count u16 plus ids u32 | step resolves a monster area action |
| 0xD9 | `log_hash` | per-target save: id u32, d20 natural u8, total i16, success u8, damage dealt u16 (after half) | same |
| 0xDA | reserved | vertical area extent (h_origin i32, h_span u16) | only once altitude is on |

`RechargeRolled` (v7) is unchanged, so no event-schema bump is needed for recharge. Death-burst triggers emit through the existing `FeatureTriggered`.

## 5. PR stack (small, each one keeps goldens green)
1. **Tests only.** This PR. `engine/core/sim/tests/area_footprint_tests.cpp` pins footprint-origin `area_targets` for Cone, Line, Cube and Sphere/Cylinder, cast by and hitting 2x2, 3x3 and 4x4 creatures, in each of the 8 directions where the shape has a direction. A target is one id even when several of its tiles are inside, including a partly covered 4x4. Line of sight is per tile. A save deals half on a success, one roll per creature, and the same seed repeats the event, state and log hashes. Existing goldens are not retuned. Cylinder height and the Chebyshev sphere are pinned as they work today and called out as follow-ups.
2. **Content.** Monster `area_effect` action block in the schema and loader that builds the synthetic `SpellDef`, reusing `recharge`/`uses`. No behaviour change for existing creatures.
3. **Sim wiring.** The AI driver picks the aim tile by maximising enemies minus allies on a deterministic tile scan. Add tags 0xD8/0xD9, gated, and update the VERSION_BUMP_BATCH entry. Do this after #287.
4. **Content batches.** Dragons and hell hound, then mephits (breath plus death burst), then ankheg, gorgon (petrify uses the condition path) and chimera. Add a golden per batch.
5. **Elemental space effects** (Whirlwind and Whelm) and the ghast Stench aura. These need "creatures in or entering its space", or a start-of-turn radius, on footprints, so stack them on the zones/auras code.
6. **Altitude hook.** A vertical predicate that is a no-op until the altitude owner's `h` lands. Reserve 0xDA. Do not take it in an earlier PR.

## 6. Gaps and risks

Follow-ups. This PR does not change them, and the tests pin the current behaviour so a later fix has to do so on purpose.

- **Cover on area saves.** Cover from footprints is unmodelled (`AttackContext::cover_ac_bonus` is caller-supplied and the area path does not set it), so a Dexterity save against an area never gains a cover bonus.
- **Square sphere.** Sphere membership is a Chebyshev radius, a square on this grid. That matches the sim's distance metric. It is not a circle.
- **Cylinder height.** A cylinder is the same 2D test as a sphere. `area_height_ft` is stored and ignored.
- **Centre origin shortens reach.** Cone and line length is measured from the footprint centre. The SRD origin is the creature's edge, so a Large or bigger caster loses the distance from that edge back to the centre. A 30 ft line from a 2×2 reaches 25 ft past the footprint, a 30 ft cone from a 3×3 reaches 25 ft past it, and a 30 ft line from a 4×4 reaches 20 ft. `area geometry: centre origin stops short of the printed length` pins those three. This PR does not move the origin.

Also, checked at this commit and left as they are:

- Ally avoidance in the AI is already in `driver.cpp` (the area-template branch of the save-action policy): it aims at a hostile's anchor, skips an aim that includes an ally, and keeps the first aim with the most hostiles. It does not scan every tile. The tile scan stays in PR 3.
- Action recharge is compiled (`content.cpp:626`). The "listed, not compiled" comment at `content.cpp:500` is the trigger-usage comment; trigger recharge itself is compiled at `:533`.
