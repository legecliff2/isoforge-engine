# Summons and transforms (R-G12): rules / sim co-design proposal

Status: **proposal, docs only.** The RULES section is written by Rules. The SIM CONTRACT section is a stub for Engine Core to
complete. Content / schema needs are listed for Tools & Content. Open questions (section 5) need Director rulings before code.
Tracks `docs/SRD_COMBAT_COVERAGE.md` §11.1 row 27 / §11.5 order 12 (R-G12) and the rows it unlocks: Wild Shape, Find Familiar,
Pact of the Chain, Pact of the Blade (bonded weapon part only), Spiritual Weapon, Conjure Animals, Shapechanger (werewolf,
doppelganger), plus Animate Dead and Polymorph (outside today's L1-3 / CR tables, listed for completeness).

Source: SRD 5.1 (CC-BY-4.0), as mirrored for `SRD_COMBAT_COVERAGE.md` §12. Code facts are as of main at `cfa4be9` (after #145).

## 1. Where things stand today

- **The roster is fixed at `begin_encounter`.** `EncounterState::combatants` and `order` (`InitiativeEntry{id, total, dex_mod}`)
  are built once; nothing appends a creature mid-encounter. Sim's `Combat::create` builds combatants from content and slots
  (tokens) from the encounter chunk; there is no spawn path.
- **Ids are caller-chosen** (`CombatantId = uint32_t`); sim assigns them at create. Nothing reserves ids for creatures that
  appear later.
- **Concentration** is one `Combatant::concentration_spell`; ending it drops that spell's effects, modifiers and zones
  (`ActiveSpellEffect`, `ActiveModifier`, `ActiveZone`), not creatures.
- **Stat blocks are flat on `Combatant`** (abilities, AC, HP, speed, attacks, multiattacks, features, traits, spell slots).
  There is no "base form" vs "current form" split.
- **Shapechanger** is only a content fact (`Combatant::shapechanger`, R-G11 PR 2, used by Moonbeam's disadvantage).
- Rests and Hit Dice (R-G16) work on a kept roster outside encounters; summons that last past an encounter (Find Familiar,
  Find Steed, Animate Dead's control) need that roster too.

## 2. RULES section (Rules: filled in)

### 2.1 SRD 5.1 behaviour to support

| Case | SRD 5.1 rule (summary) | Initiative | Control | Duration / end | Combat-relevant extras |
|---|---|---|---|---|---|
| Conjure Animals (L3) | fey spirits in beast form: one CR 2, two CR 1, four CR 1/2 or eight CR 1/4; upcast multiplies count | **one shared roll for the group** | verbal command, no action; without commands they defend themselves | concentration, 1 hour; at 0 HP disappears | Director picks the beasts (SRD: the DM chooses) |
| Find Familiar (L1) | celestial / fey / fiend in a listed animal form; can't attack; can take Help etc. | **own initiative**, acts on its own turn | telepathic, no action | until dismissed or 0 HP | caster may deliver touch spells through it (reaction) |
| Pact of the Chain (Warlock 3) | Find Familiar plus imp / pseudodragon / quasit / sprite; the familiar can attack with the warlock's reaction... **no**: the warlock forgoes one of its attacks to let the familiar attack with its reaction | own | as Find Familiar | as Find Familiar | "Attack" action trade (Q6) |
| Find Steed (L2) | celestial / fey / fiend steed (warhorse, pony, camel, elk, mastiff); shares self-targeting spells | **acts on the rider's initiative when mounted** (mounted combat rules); otherwise own | telepathic | until dismissed or 0 HP | mounted combat (R-G17 adjacent; out of scope here, Q9) |
| Spiritual Weapon (L2) | a spectral weapon, not a creature: bonus action to create and attack (melee spell attack, 1d8 + mod force), bonus action on later turns to move 20 ft and attack | none (acts in the caster's turn) | caster's bonus action | 1 minute, **no concentration** | not a target; no HP; upcast +1d8 per 2 levels |
| Animate Dead (L3) | skeleton or zombie from a corpse; bonus action to command (all within 60 ft); 24 hours control, recast to keep | own (shared? the SRD says nothing: Q2) | **bonus action**, one command for any or all; without a command it only defends | 24 hours (control), no concentration | needs corpses (sim / content) |
| Conjure Minor Elementals / Woodland Beings (L4), Conjure Elemental (L5) | as Conjure Animals; the elemental turns hostile when concentration breaks | shared group | verbal | concentration | hostile-on-break (Q5) |
| Polymorph (L4) | a creature becomes a beast of CR <= its level/CR; new game statistics incl. HP; mental scores replaced; reverts at 0 HP with **excess damage carried over**; can't cast or speak | keeps its own turn / initiative | the creature itself (its own choices) | concentration, 1 hour; unwilling: Wis save | shapechangers are unaffected; gear melds |
| True Polymorph (L9) | as Polymorph, permanent after 1 hour | as Polymorph | as Polymorph | concentration, then permanent | outside our levels (not planned) |
| Wild Shape (Druid 2) | action (bonus action for Circle of the Moon, not SRD Land); beast of max CR by level; keep mental scores, proficiencies, features; take beast physical scores, HP, Hit Dice; revert at 0 HP with overflow; uses 2 per short rest; lasts hours = half druid level | keeps its own | itself | bonus action to revert, 0 HP, unconscious, death, duration | can't cast; can concentrate on a spell cast before; gear: meld / wear / drop |
| Shapechanger trait (werewolf, wereboar, ..., vampire, doppelganger) | action to polymorph into a listed form; **statistics other than size are the same** (werewolf: AC changes in some forms, attacks differ); equipment not transformed; reverts on death | keeps its own | itself | until it changes back (action) | lycanthropy curse out of scope; immunity to nonmagical non-silvered damage already modelled |

### 2.2 Concepts and data model (proposed; all appended, defaults keep today's behaviour)

Two independent mechanisms:

**A. Summons: new combatants mid-encounter.**

- `SummonSpec` (content, per spell / feature): `kind` (Creature | Weapon), `archetypes` (content ids; the caller picks, Q1),
  `count`, `initiative` (Own | SharedGroup | SummonerTurn | RiderTurn), `control` (None | Verbal | BonusAction | Telepathic),
  `concentration_link` (bool), `ends_at_zero_hp` (disappears, no death saves, no corpse), `hostile_on_break` (Conjure Elemental),
  `duration` (the spell's).
- `Combatant::summoner` (CombatantId, 0 = none), `Combatant::summon_source` (spell / feature id), `Combatant::summon_group`
  (shared initiative group id), `Combatant::commanded` (this turn's command received; bonus-action control).
- `EncounterState::next_id` is **not** added: ids come from the request (sim chooses; determinism in 2.6).
- **Spiritual Weapon is not a combatant** (Q3): an `ActiveSummonWeapon{owner, spell_id, timed}` row; its attack is a
  bonus-action request `ActionKind::CastSpell` with `sustain`-like semantics (reuse R-G15 `sustain` with a weapon flag) that
  rolls a melee spell attack. Its position is sim's (a token without a Combatant).

**B. Transforms: a creature's statistics replaced for a while.**

- `Combatant::base` (an optional saved stat block: `FormStats` = abilities, AC, hp / hp_max / temp hp, speed, attacks,
  multiattacks, size, creature type, resistances / immunities / vulnerabilities, senses fact bits, traits) and
  `Combatant::form` (`FormKind`: None | Polymorph | WildShape | Shapechanger), `form_source` (spell / feature id).
- What is replaced vs kept (per kind, SRD):

| field | Polymorph | Wild Shape | Shapechanger (werewolf) |
|---|---|---|---|
| Str / Dex / Con | beast | beast | same (stat block lists per form) |
| Int / Wis / Cha | beast | **kept** | same |
| HP / hp_max | beast (fresh) | beast (fresh) | same pool |
| Hit Dice | beast | beast | same |
| AC, speed, attacks, multiattack | beast | beast | per form (content lists them) |
| saving throw / skill proficiencies | beast | **kept + beast's, higher of each** | same |
| class features, feats, racial traits | lost | **kept if the beast can physically use them** (content flag; Q7) | same |
| spellcasting | can't | can't cast; concentration continues | per form (vampire bat can't) |
| conditions, exhaustion, concentration | kept | kept | kept |
| temp HP | kept? (Q8) | kept? (Q8) | kept |
| equipment | melds (no AC / attack from gear) | meld / wear / drop (content: meld) | not transformed |

- **Reverting with HP overflow** (Polymorph, Wild Shape): when form HP reaches 0, the creature reverts and **excess damage carries
  over** to the base HP (SRD: "any excess damage carries over to its normal form"); it does not die unless the base form drops to 0.
  Damage that would kill outright (massive damage) is checked against the form's maximum first, then the overflow against the
  base. Rules implements this inside `damage_creature` (one place), emitting `Reverted` then the carried `DamageDealt`.
- Shapechanger forms do not use overflow: one HP pool.

### 2.3 Requests (proposed)

- **Summon**: the casting request (`CastSpell` of a spell with `SummonSpec`) carries `summon_ids` (new combatant ids, sim-chosen,
  ascending) and `summon_archetypes` (indices into the spec's list, the caster's / Director's choice) and `summon_tiles` (opaque,
  sim). Rules validates count and choice; **sim supplies the new `Combatant` records** through a hook
  `EncounterHooks::build_summon(state, spec, archetype, id) -> Combatant` (rules never builds a stat block from content).
- **Command**: `ActionKind::CommandSummons` (appended) with `summon_ids` (empty = all) and `command` (None | Act | Defend), costing
  the summoner's bonus action for `control = BonusAction` (Animate Dead), free for Verbal / Telepathic. A summon without a
  command this turn takes only Dodge / defensive options (`legal_actions` restricts it; Q4).
- **Transform**: `ActionKind::UseFeature` (Wild Shape, shapechanger) or the casting request (Polymorph) with `form_archetype`
  (content id index) → `EncounterHooks::build_form(state, creature, archetype) -> FormStats`.
- **Revert**: `ActionKind::Revert` (appended): bonus action (Wild Shape), action (shapechanger); also automatic (0 HP, death,
  duration, concentration end).
- **Dismiss**: `ActionKind::Dismiss` (Find Familiar: action; ends Spiritual Weapon: none in SRD).

### 2.4 Initiative placement

- **Own initiative** (Find Familiar, Animate Dead per Q2): rules rolls `d20 + Dex mod` for the new creature at summon time from
  the encounter rng (same stream, after the cast's own rolls) and inserts it into `order` by the existing sort (total, then
  dex_mod, then id); it takes its first turn when its slot comes up. If its slot is earlier in the current round than the
  current actor, its first turn is next round (no extra turn this round).
- **Shared group** (Conjure Animals and the conjure family): one roll for the group; one `InitiativeEntry` per creature with the
  same total, ordered by id; they act consecutively.
- **Summoner's turn** (Spiritual Weapon): no entry.
- **Rider's turn** (Find Steed when mounted): out of scope (Q9).
- Removal: a summon that ends (0 HP, dismissed, concentration) is removed from `order` at the end of the current step; if it is
  the current actor, its turn ends first (`TurnEnded`, then removal). `turn_index` is adjusted so the next actor is unchanged.

### 2.5 Concentration links

- `concentration_link` summons end with the summoner's concentration on `summon_source` (same place that drops effects:
  `end_concentration`). Reason codes on `SummonEnded.b`: 0 duration, 1 concentration, 2 dismissed, 3 0 HP, 4 summoner died
  (only where SRD says so; Find Familiar does not end on the caster's death, Q10).
- Polymorph: concentration end reverts the target (`Reverted.b = 1`).
- Conjure Elemental with `hostile_on_break`: on concentration loss the creature stays, changes team to hostile, and acts on its
  own (Q5).

### 2.6 Determinism, ids and replay

- **Ids**: sim assigns summon ids at request time from a monotonic per-Combat counter starting above the highest create-time id
  (proposed: `max(create ids) + 1`, then +1 per summon, never reused). The ids travel in the logged request (`summon_ids`), so
  replay rebuilds the same roster without re-deriving them. Rules refuses an id already present.
- **Roster growth**: `EncounterState::combatants` / `order` grow by append; nothing is erased mid-step (an ended summon is marked
  `gone` and removed at the step boundary) so pointers / spans taken inside a step stay valid. Iteration order everywhere stays
  "combatants vector order" = creation order, which is deterministic.
- **RNG**: initiative for summons draws from the encounter rng inside the casting step, after the spell's own rolls and before
  any reaction window of later effects (fixed position: documented ordering rule). Form building draws nothing.
- **Hooks are pure**: `build_summon` / `build_form` must be functions of (content, archetype, id); replay calls them again.
- **Replay**: `replay_step` re-executes the logged request; since ids and archetype indices are in the request, the roster after
  each step is identical. Existing logs carry none of the new fields, so they replay unchanged.

### 2.7 Event plan (event stream v10)

Appended rules5e `EventKind`s (after `ZoneEnded` = 47; numbers proposed):

| kind | payload |
|---|---|
| `Summoned` (48) | actor = summoner, target = new creature, a = source spell / feature id, b = archetype index, c = initiative total (or -1 = none) |
| `SummonEnded` (49) | actor = summoner, target = creature, a = source id, b = reason (2.5) |
| `Transformed` (50) | actor = creature, target = 0, a = source id, b = archetype index, c = FormKind |
| `Reverted` (51) | actor = creature, a = source id, b = reason (0 duration, 1 concentration, 2 0 HP, 3 chose, 4 died), c = overflow damage carried |
| `Commanded` (52) | actor = summoner, target = creature (0 = all), a = command |

`event_stream_version` returns 10 only for streams that carry one of them (SIM_EVENTS.md versioning policy), so no existing digest
moves. Sim adds a `SimEvent::Kind::TokenSpawned` / `TokenRemoved` for tiles (EC, 3.x).

### 2.8 Hashing plan (tags reserved here, claimed in SIM_EVENTS.md by the implementation PR)

Taken on main or by open PRs at the time of writing: Combat::hash up to 0xAD on main, **0xAE** (#149, gaze); log_hash up to
0xB9 on main, **0xBA** (#147, Font of Magic), **0xBB** (#149, gaze). #146 / #148 claim none.

| tag (reserved) | where | content | folded when |
|---|---|---|---|
| **0xAF** | `Combat::hash` | `summons_digest`: per summoned combatant (creation order): id, summoner, source, group, gone flag; per transformed combatant: id, FormKind, source, base hp / hp_max (the saved block's HP) | any summon or transform has happened |
| **0xBC** | `log_hash` | summon inputs: `summon_ids` (count + ids), `summon_archetypes` (count + bytes), then `CommandSummons` `command` byte | any non-empty / non-zero |
| **0xBD** | `log_hash` | transform input: `form_archetype` byte (1 + index; 0 = none) | non-zero |

`summon_tiles` / placement are sim facts (not folded, like zone membership) unless EC decides otherwise (3.x). Combatant records
of summons are covered by the existing per-slot fold once sim has a slot for them (EC: decide whether summons get slots, 3.2).

### 2.9 Rules / sim split

| concern | rules | sim |
|---|---|---|
| validity of a summon (count, choice, slot level, concentration) | yes | — |
| building the summon's `Combatant` from content | — (hook) | yes (`build_summon`) |
| ids | validates uniqueness | assigns (counter) |
| initiative roll + order insertion / removal | yes | mirrors for its slots |
| placement (tiles, "unoccupied space within range") | — | yes |
| commands, legal_actions of summons | yes | AI policy for summons |
| form stats (build), gear meld | — (hook) | yes (`build_form`, content) |
| HP overflow, revert, kept vs replaced fields | yes | — |
| token spawn / removal events | — | yes |
| rest / long-lived summons (familiar, steed, Animate Dead control) | rest helpers ignore summons | campaign roster keeps them (11.4a) |

### 2.10 Proposed PR split

1. **Rules PR 1 — transforms** (no roster growth): `FormStats`, `Combatant::base` / `form`, Wild Shape (feature, uses), Polymorph
   (spell, Wis save, concentration), shapechanger forms, revert + overflow in `damage_creature`, events 50 / 51, 0xBD, tests.
2. **Rules PR 2 — summons**: roster growth, `build_summon` hook, initiative insertion / removal, concentration link,
   `CommandSummons`, Find Familiar, Conjure Animals, Animate Dead, events 48 / 49 / 52, 0xAF / 0xBC, tests.
3. **Rules PR 3 — Spiritual Weapon** (non-creature summon) and Pact of the Chain's attack trade (small).
4. **Sim PRs (EC)** after each rules PR: hooks, ids, slots / tokens, folds, AI; **content PRs (T&C)** in step (schema keys
   below). Find Steed / mounted combat later with R-G17.

## 3. SIM CONTRACT section (Engine Core: stub, to complete)

### 3.1 Spawn and placement
*EC: how a summon gets a slot / token, where (`summon_tiles` validation: unoccupied, within range, footprint), and the
`TokenSpawned` / `TokenRemoved` events.*

### 3.2 Hashing of summoned slots
*EC: whether summons join the per-slot fold in `Combat::hash` (moves no golden: only combats with summons), and whether 0xAF
covers what slots do not.*

### 3.3 `build_summon` / `build_form` hooks
*EC: content lookup (archetype ids from the spell / feature), purity for replay, Combat::create untouched.*

### 3.4 AI and driver
*EC: commanding summons, uncommanded behaviour (Dodge), when to Wild Shape / revert.*

### 3.5 Campaign roster
*EC: long-lived summons across encounters (11.4a).*

## 4. Content / schema needs (Tools & Content)

- spell@1 effect kind `summon { creatures: [{archetype: <content id>, count}] | choose: {max_cr, count_by_cr: {...}},
  initiative: own | group | summoner_turn, control: verbal | bonus_action | telepathic, ends_at_zero_hp, hostile_on_break,
  concentration_link }` (closed arm).
- spell@1 effect kind `summon_weapon { attack: {dice, type, reach_ft}, move_ft, bonus_action_attack }` (Spiritual Weapon).
- spell@1 effect kind `polymorph { max_cr: "target_level" | n, save: {ability: wis}, unwilling_save, excludes_shapechangers }`.
- character@N feature kind `wild_shape { uses, recharge: short_rest, max_cr_by_level: [...], no_fly_below, no_swim_below,
  duration_hours: "half_level" }`.
- monster@4 trait `shapechanger { forms: [{id, name, size, ac, speed, attacks: [...], ...overrides}] }` (statistics other than
  size are the same unless listed).
- creature archetypes for familiars / steeds / conjured beasts: already monster@4 stat blocks (bat, cat, owl, wolf, warhorse,
  skeleton, zombie, ...); T&C confirms the SRD set ships in `srd51.core`.
- Validator: archetype ids resolve; CR limits; Find Familiar forms list.

## 5. Open questions (Director rulings; recommended answer in each)

- **Q1 Who chooses the creatures of Conjure Animals / the conjure family?** SRD: the DM. **Recommend:** the content lists allowed
  archetypes per CR band; the caster's request picks (AI: strongest by CR then id). Director may pin a per-encounter choice.
- **Q2 Animate Dead initiative: own per creature or shared?** SRD is silent. **Recommend:** one shared group per cast (like the
  conjure family), so a cast adds one slot of turns.
- **Q3 Spiritual Weapon as a combatant or a non-creature effect?** **Recommend:** non-creature (`ActiveSummonWeapon`): no HP, not
  targetable, attacks in the caster's bonus action; position is a sim token without a Combatant.
- **Q4 Uncommanded summons:** SRD conjure: "defends itself and otherwise takes no actions"; Animate Dead: "only defends itself".
  **Recommend:** Dodge on its turn and opportunity attacks / reactions allowed; no movement.
- **Q5 Conjure Elemental hostile on broken concentration:** **Recommend:** model it (team flips to the summoner's enemies, keeps
  its turn); level 5, so after PR 2.
- **Q6 Pact of the Chain attack trade:** **Recommend:** `Attack` request flag `familiar_attack` that spends one of the warlock's
  attacks and the familiar's reaction (PR 3).
- **Q7 Wild Shape "features it can physically use":** **Recommend:** keep all class / racial features unless content marks a
  feature `requires_humanoid_form`; spellcasting always off (SRD), concentration kept.
- **Q8 Temporary HP across a transform:** SRD is silent. **Recommend:** keep them on the creature (they absorb damage before the
  form's HP); not part of the saved base block.
- **Q9 Find Steed / mounted combat:** **Recommend:** out of R-G12 (with R-G17); Find Steed summons on its own initiative unmounted.
- **Q10 Summons when the summoner dies:** SRD: Find Familiar / Find Steed persist; concentration summons end with concentration
  (death ends it). **Recommend:** follow SRD per spell (`SummonEnded.b = 1` via concentration; no generic "summoner died" end).
- **Q11 Ids for summons:** **Recommend:** sim counter above the create-time maximum, carried in the logged request (2.6).
- **Q12 Massive damage on revert:** **Recommend:** instant death checks the overflow against the base form's hp_max (SRD death
  rules apply to the normal form after carrying over).
- **Q13 Polymorph on a shapechanger / an unwilling target at 0 HP:** **Recommend:** refuse on shapechangers (`excludes_shapechangers`);
  a 0-HP target can't be polymorphed (no effect).
- **Q14 Initiative position for a summon whose roll beats the current actor:** **Recommend:** first turn next round (2.4); no
  turn in the round it appeared after its slot has passed.

## 6. SRD coverage mapping

| `SRD_COMBAT_COVERAGE.md` row | today | after | PR |
|---|---|---|---|
| Wild Shape (Druid 2) | NEW | NEW (wiring) → DATA with sim | rules 1 |
| Shapechanger (werewolf, doppelganger) | NEW | NEW (wiring) | rules 1 |
| Polymorph (L4, not in tables) | — | NEW (wiring) | rules 1 |
| Find Familiar (L1) | NEW | NEW (wiring) | rules 2 |
| Pact Boon: Chain | NEW | NEW (wiring) | rules 2 + 3 |
| Conjure Animals (L3) | NEW | NEW (wiring) | rules 2 |
| Animate Dead (L3) | (not listed; add row) | NEW (wiring) | rules 2 |
| Spiritual Weapon (L2) | NEW | NEW (wiring) | rules 3 |
| Pact Boon: Blade | NEW | unchanged (bonded weapon = R-G17 / T&C-G2) | — |
| Find Steed (L2) | (not listed; add row) | NEW (summon only; mounted combat R-G17) | rules 2 |
