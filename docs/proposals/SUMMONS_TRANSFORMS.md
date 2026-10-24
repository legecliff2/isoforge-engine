# Summons and transforms (R-G12): rules / sim co-design proposal

Status: **proposal, docs only; Director rulings D1..D15 recorded (§5).** The RULES section is written by Rules. The SIM CONTRACT
section is drafted by Engine Core (for review). Content / schema needs are listed for Tools & Content.
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
| Pact of the Chain (Warlock 3) | Find Familiar plus imp / pseudodragon / quasit / sprite; when the warlock takes the Attack action it can forgo one of its own attacks to let the familiar make one attack with its reaction | own | as Find Familiar | as Find Familiar | "Attack" action trade (Q6) |
| Find Steed (L2) | celestial / fey / fiend steed (warhorse, pony, camel, elk, mastiff); shares self-targeting spells | **acts on the rider's initiative when mounted** (mounted combat rules); otherwise own | telepathic | until dismissed or 0 HP | mounted combat (R-G17 adjacent; out of scope here, Q9) |
| Spiritual Weapon (L2) | a spectral weapon, not a creature: bonus action to create and attack (melee spell attack, 1d8 + mod force), bonus action on later turns to move 20 ft and attack | none (acts in the caster's turn) | caster's bonus action | 1 minute, **no concentration** | not a target; no HP; upcast +1d8 per 2 levels |
| Animate Dead (L3) | skeleton or zombie from a corpse; bonus action to command (all within 60 ft); 24 hours control, recast to keep | own (shared? the SRD says nothing: Q2) | **bonus action**, one command for any or all; without a command it only defends | 24 hours (control), no concentration | needs corpses (sim / content) |
| Conjure Minor Elementals / Woodland Beings (L4), Conjure Elemental (L5) | as Conjure Animals; the elemental turns hostile when concentration breaks | shared group | verbal | concentration | hostile-on-break (Q5) |
| Polymorph (L4) | a creature becomes a beast of CR <= its level/CR; new game statistics incl. HP; mental scores replaced; reverts at 0 HP with **excess damage carried over**; can't cast or speak | keeps its own turn / initiative | the creature itself (its own choices) | concentration, 1 hour; unwilling: Wis save | shapechangers are unaffected; gear melds |
| True Polymorph (L9) | as Polymorph, permanent after 1 hour | as Polymorph | as Polymorph | concentration, then permanent | outside our levels (not planned) |
| Wild Shape (Druid 2) | action (bonus action for a non-SRD druid subclass (out of scope); SRD Land stays an action); beast of max CR by level; keep mental scores, proficiencies, features; take beast physical scores, HP, Hit Dice; revert at 0 HP with overflow; uses 2 per short rest; lasts hours = half druid level | keeps its own | itself | bonus action to revert, 0 HP, unconscious, death, duration | can't cast; can concentrate on a spell cast before; gear: meld / wear / drop |
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
  base. Rules implements this inside `damage_creature` (one place), emitting the form's `DamageDealt` (a = only what the form absorbed), `Reverted` (c = overflow), then the carried `DamageDealt` (a = the overflow), so the two `a` values sum to the hit.
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
  the encounter rng (same stream, after the cast's own rolls) and inserts it into `order` **after every entry with a higher
  total, or an equal total and a Dex modifier at least as high** (as built, rules PR 2); it takes its first turn when its slot
  comes up. If its slot is earlier in the current round than the
  current actor, its first turn is next round (no extra turn this round).
- **Shared group** (Conjure Animals, the conjure family, Animate Dead per cast, D2): one roll for the group; one `InitiativeEntry`
  per creature with the same total and the first creature's Dex modifier (one slot), ordered by id; they act consecutively.
- **Summoner's turn** (Spiritual Weapon): no entry.
- **Rider's turn** (Find Steed when mounted): out of scope (Q9).
- Ended summons (as built, rules PR 2): a summon that ends (0 HP, dismissed, concentration) **stays in `order`**, marked `gone`
  (and dead); the turn loop skips it like any dead combatant. Nothing is removed, so ending never shifts `turn_index` or stamps
  (only insertion does, below).
- **Insertion before the current actor shifts `turn_index`.** A summon whose entry sorts before the current actor's slot moves
  the current actor one index later (per inserted entry). Rules adjusts `turn_index` in the same step so the **current actor's
  turn continues** (same budget, no new `TurnStarted`); the inserted creature first acts next round (D14). Ended summons stay in
  `order` (skipped), so nothing shifts it back. Engine Core changes `run_to_end`'s turn detector to key on the current actor and
  the count of `TurnStarted` events instead of `(round, turn_index)` (§3.4).
- **Decision: `cast_stamp` rewrite.** `cast_stamp` (`round << 16 | turn_index`, folded under `Combat::hash` 0xAC; read by Heat
  Metal's sustain check and the driver's `spell_kind_command`) would go stale on such a shift. When an insert or removal shifts
  `turn_index`, rules rewrites every stored `cast_stamp` (live effects) in the same step by the same shift, so "cast this turn"
  stays true. Combats without summons never shift, so the encoding and every golden stay unchanged. Rejected alternative: a
  monotonic turn counter (`turn_seq`) as the stamp, which would change 0xAC's bytes and move the Heat Metal goldens.

### 2.5 Concentration links

- `concentration_link` summons end with the summoner's concentration on `summon_source` (same place that drops effects:
  `end_concentration`). Reason codes on `SummonEnded.b`: 0 duration, 1 concentration, 2 dismissed, 3 0 HP, 4 summoner died, 5 encounter ended (§3.5)
  (only where SRD says so; Find Familiar does not end on the caster's death, Q10).
- Polymorph: concentration end reverts the target (`Reverted.b = 1`).
- Conjure Elemental with `hostile_on_break`: on concentration loss the creature stays, changes team to hostile, and acts on its
  own (Q5).

### 2.6 Determinism, ids and replay

- **Ids** (D11 as refined with Engine Core, §3.0; as built in rules PR 2): **rules** assigns summon ids from a monotonic counter
  (`EncounterState::next_summon_id`, seeded by sim at create) that starts above
  **every chunk spawn id** (combatant or not), not just combatants; +1 per summon, **never reused** (also after `SummonEnded`).
  The counter is folded in `Combat::hash` 0xAF. Ids travel in the logged request (`summon_ids`); a caller-supplied
  `summon_ids` is refused in live play (rules fills it into the logged request), and **replay rejects** logged ids that differ from what the counter
  would assign (desync). Rules refuses an id already present as a second check.
- **Turn order shifts**: see §2.4 (current actor's turn kept, `cast_stamp` rewritten).
- **Roster growth**: `EncounterState::combatants` / `order` grow by append; nothing is erased mid-step (an ended summon is marked
  `gone` and stays in `order`, skipped) so pointers / spans taken inside a step stay valid. Iteration order everywhere stays
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
| `Reverted` (51) | actor = creature, a = source id, b = reason (0 duration, 1 concentration, 2 0 HP, 3 chose, 4 died, 5 encounter ended), c = overflow damage carried |
| `Commanded` (52) | actor = summoner, target = creature (0 = all), a = command |

**Encounter end (D15, §3.5):** in the ending step rules emits, in order, `ConcentrationEnded` for each summon that was
concentrating (ascending summon id, with that concentration's own end events), then `SummonEnded{b = 5}` per summon in ascending id (a
transformed summon just ends, with no separate `Reverted`), then `Reverted{b = 5, c = 0}` per remaining transformed creature in
ascending id, then `EncounterEnded`, which stays the last event of the stream. **Settled (Director, #155):** SIM_EVENTS.md
ordering rule 7 is unchanged; rules reports the start of the teardown block with `StepResult::teardown_begin` (index into the
step's events of the first `SummonEnded{b = 5}`, or the first `Reverted{b = 5}` without summons, or none), and sim places each `TokenRemoved` right after its `SummonEnded` and every sim event of the ending
step before `EncounterEnded` (v10-gated; a documented rule-4 exception), per §3.5.

`event_stream_version` returns 10 only for streams that carry one of them (SIM_EVENTS.md versioning policy), so no existing digest
moves. Sim adds a `SimEvent::Kind::TokenSpawned` / `TokenRemoved` for tiles (EC, 3.x).

### 2.8 Hashing plan (tags reserved here, claimed in SIM_EVENTS.md by the implementation PR)

Taken on main or by open PRs at the time of writing: Combat::hash up to 0xAD on main, **0xAE** (#149, gaze); log_hash up to
0xB9 on main, **0xBA** (#147, Font of Magic), **0xBB** (#149, gaze). #146 / #148 claim none. **These reservations depend on
#147 and #149** landing with the claims they carry; if either changes its tags, this table moves with it.

| tag (reserved) | where | content | folded when |
|---|---|---|---|
| **0xAF** | `Combat::hash` | the summon id counter, then `summons_digest`: per summoned combatant (creation order): id, summoner, source, group, gone flag; per transformed combatant: id, FormKind, source, base hp / hp_max (the saved block's HP); then sim's per-summon-slot part (§3.2) | only when something has been summoned or transformed |
| **0xBC** | `log_hash` | summon inputs: `summon_ids` (count + ids), `summon_archetypes` (count + bytes), `summon_tiles` (count + x, y), then `CommandSummons` `command` byte and ids | any non-empty / non-zero |
| **0xBD** | `log_hash` | transform input: `form_archetype` byte (1 + index; 0 = none) | non-zero |

`summon_tiles` are **caller inputs** (or written back by sim's deterministic choice before logging), so they **are folded** in
0xBC (this reverses the earlier draft, which treated them as derived facts like zone membership; §3.1). Summon slots stay **out
of the untagged per-slot loop** of `Combat::hash` (which covers create-time slots only, byte-for-byte as today) and are folded
only inside 0xAF (§3.2).

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

## 3. SIM CONTRACT section (Engine Core: drafted, for review)

Code references are to `engine/core/sim` on #150's base. Every rule below is a no-op for a combat that never summons or
transforms: no new hash byte, no new event, no new log byte, so no golden moves (tested by re-running the golden suite
unchanged in each sim PR).

### 3.0 Ids (Q11, sim side)

- **Owner (ruling, R-G12 sim PR 1):** the summon id counter is rules' (`EncounterState`, folded inside 0xAF by
  `summons_digest`); sim has no counter of its own. Sim **seeds** it once at the end of `Combat::create` to `max(stable_id of
  **every** spawn in every chunk of the combat, combatant or not) + 1` (not only combatants: non-combatant spawns also own a
  `StableId` in the registry). Overflow (max = UINT32_MAX) makes `create` succeed but every summon is refused ("summon id
  space exhausted"). The seed alone never makes 0xAF fold: `has_summon_state` keys on "something was summoned or transformed".
- **As built (rules PR 2): rules fills `summon_ids`, not sim.** Sim seeds `EncounterState::next_summon_id` (the
  seed above; sim keeps no `next_summon_id_` of its own); rules assigns `count` consecutive ids from it, writes them into the logged
  request (`StepResult::logged->request.summon_ids`), refuses a caller-supplied non-empty list live (`summon_ids: assigned by
  rules (leave empty)`) and refuses replayed ids that differ from the counter (`summon_ids: do not match the summon counter`);
  the counter lives in rules' 0xAF. Sim needs no id check of its own. The original sim-side draft follows for history:
- (draft) At a summon request sim fills `summon_ids` itself: `count` consecutive ids from the counter, ascending; the counter
  advances only when the request completes (`StepStatus::Done`). Rejected or paused requests consume nothing; a request
  that pauses for a reaction window keeps the ids it was given and commits them on completion. Ids are **never reused**,
  including after `SummonEnded` (the counter only grows; the `gone` slot keeps its id).
- (draft; superseded by rules' checks above) A caller-supplied non-empty `summon_ids` on a live `submit` is an error ("summon ids are assigned by sim"), so the only
  source of ids is the counter.
- (draft; superseded by rules' checks above) **Replay** (`sim::replay`): before `replay_step`, sim checks that the logged `summon_ids` equal exactly the ids the counter
  would assign at that point (the rules counter, count = spec count). A mismatch is a desync error ("replay desync at
  entry i: summon ids"). Rules' own "id already present" check stays as the second line.
- Registry clash: if `reg.find(id)` already exists when a summon would be created (another combat or system shares the
  registry and took that StableId), the request is refused with an error and nothing is consumed. This is deterministic
  for a given registry; replay into a fresh registry is the supported path (same as `create`'s duplicate check).

### 3.1 Spawn and placement

- **Slot**: a summoned creature gets a normal `Slot{id, entity, archetype}` appended to `slots_` (creation order after all
  create-time slots), plus an entity `reg.create(id)` with `Health`, `Team` (the summoner's team id; `hostile_on_break` changes
  the rules team, sim mirrors it into `Team`), `TilePos`, `Footprint` when > 1, `CombatantLink`. Plus `Slot::summoned = true`
  and `Slot::gone` (set at `SummonEnded`). `Combat::create` is untouched; summon slots are only made in `complete()` (live and
  replay share that path).
- **Tiles (`summon_tiles`)**: the request carries one anchor per summon, or none. Caller-supplied anchors are validated in
  request order against the occupancy as it grows within the request: in bounds; every footprint tile (`footprint_tiles_at`,
  the archetype's side) walkable and not in `occupied_except(0)` nor in the footprints already placed for earlier summons
  of the same request; within the spell's range from the caster (nearest footprint tile to nearest footprint tile,
  Chebyshev, 5 ft tiles, as for range checks today); in the caster's line of sight to at least one footprint tile when the
  spell requires sight. Any failure rejects the whole request (no partial summon).
- **Deterministic choice** when `summon_tiles` is empty (AI, headless driver, Director tools): for each summon in id order,
  candidates are every anchor passing the checks above; pick minimum Chebyshev footprint distance to the caster, then
  **row-major** (lowest y, then lowest x), the same order as `footprint_tiles_at` / `teleport_dests`. No candidate for any
  summon rejects the request ("no unoccupied space"). Sim writes the chosen anchors back into the request before it is
  logged, so the log carries the tiles (like derived area targets) and replay never re-derives them.
- Dead creatures do not block (`occupied_except` already skips non-dead only); `gone` slots never occupy and are skipped
  by `pos` lookups used for targeting, pathing, areas, auras and the AI.
- **Removal**: at `SummonEnded`, the slot is marked `gone`, its entity is destroyed at the end of `complete()` (never mid-step,
  matching 2.6), the slot stays in `slots_` (index stability, hashing below).
- **Events** (SimEvent kinds appended, v10 like the rules kinds): `TokenSpawned = 4` (actor = id, tile = anchor,
  rules.a = footprint side, rules.b = summoner id) one per summon, **each right after its own `Summoned`** (rules PR 2 emits the
  `Summoned` events after `SpellResolved`, id order), before `SpellTarget`; `TokenRemoved = 5` (actor = id, tile = last anchor, rules.a = `SummonEnded.b` reason) after the
  request's rules events (and after its `TileMoved`s). A Large+ summon needs no separate `Footprint` event (TokenSpawned
  carries the side). `event_stream_version` returns 10 for either kind. Spiritual Weapon's token is `TokenSpawned` with
  actor = the weapon's pseudo id from the same counter (it never becomes a Combatant; `ActiveSummonWeapon::token_id`), moves
  as `TileMoved`, ends with `TokenRemoved`; it occupies nothing.
- `summon_tiles` are folded into `log_hash` under 0xBC (below), unlike zone membership: they are caller / AI inputs, not
  derived facts (a different tile is a different fight).

### 3.2 Hashing of summoned slots

- **Spiritual Weapon token (rules PR 3, as built):** its `TokenSpawned` (actor = token id from `Summoned.target`, tile = the
  cast's `summon_tiles[0]`) goes right after its `Summoned`, which rules emits inside the cast before `SpellResolved` (so before
  the weapon's first `AttackResolved`); a `SummonWeaponAttack` with a move tile gets its `TileMoved` before the attack's events.
  The token id is not a combatant. Rules stores the tile in the 0xAF weapon row and logs it (0xBC `summon_tiles`).
- **Tile contract (EC, #173):** (a) the cast carries **exactly one** spawn tile (`summon_tiles`), any in-bounds tile within 60 ft
  of the caster; the token occupies nothing. Rules refuses zero or several (`spiritual weapon: one tile`). (b)
  `SummonWeaponAttack`: an empty tile list = no move; otherwise one tile at most 20 ft from the stored tile; the attack's reach
  is 5 ft, measured from the tile after the move. (c) Sim validates live; on replay sim re-checks the logged tiles and a failure
  is a replay desync error. Rules trusts the log (it checks only the count).

- The existing per-slot fold in `Combat::hash` (id, pos, hp, dead per slot, untagged) runs over **create-time slots only**
  (`slots_[0, created_count_)`), byte-for-byte as today. Summon slots are folded only inside 0xAF, so their bytes are always
  behind a tag and a count (no ambiguity with later untagged sections).
- **0xAF** (`Combat::hash`, folded when `has_summon_state`: something was summoned or transformed this encounter; the
  seeded counter alone does not count): rules' `summons_digest` u64 (2.8; it holds the summon id counter, rules-owned, and
  the transform records; no separate sim counter is folded). With summons (PR 2) the sim part follows: count u32, then per summon slot (creation order) id u32,
  pos x/y i32, footprint byte, `gone` byte, team byte; then count u32 + per weapon token (token id, owner, pos). Sim and rules
  share the one tag: sim folds `h.byte(0xAF); h.u64(rules5e::summons_digest(state_)); <sim part>` in one place.
- **0xBC** (`log_hash`, folded when any of the fields is non-empty / non-zero): `summon_ids` (count u32 + u32 each),
  `summon_archetypes` (count u32 + byte each), `summon_tiles` (count u32 + i32 x, i32 y each), `command` byte, then the
  `CommandSummons` id list (count u32 + u32 each). **0xBD** as 2.8.
- Event order is not part of a hash section, but `event_stream_hash` and `Combat::hash` fold events in stream order. The v10
  ending-step reorder (3.5) therefore changes only v10 streams; pre-v10 streams hash byte-identically.
- 0xAC (`cast_stamp`) is unchanged in layout. In a combat with summons it folds the stamps after rules has rewritten them
  (3.4 ruling), so live play and replay agree. Combats without summons never rewrite, so their 0xAC bytes are as today.
- Required: 0xAF, 0xBC and 0xBD are added to the SIM_EVENTS.md registry by the first implementation PR (rules PR 1 for 0xBD;
  rules PR 2 for 0xAF / 0xBC), before or with the code.

### 3.3 `build_summon` / `build_form` hooks

- **As built (rules PR 2):** `build_summon(const EncounterState&, const Combatant& summoner, const SpellDef& def, uint8_t
  slot_level, uint8_t archetype_index) -> std::optional<Combatant>`; **no id parameter**: rules sets `id` (from its counter),
  `team`, `summoner`, `summon_source`, `summon_group` and the control fields on the returned record. Sim learns the id from
  `Summoned.target` (one per creature, id order) and registers the archetype rows for it then. Optional
  `summon_choice_ok(state, summoner, def, slot_level, archetypes) -> bool` is the D1 CR-band table. Draft text follows:
- (draft) Installed in `install_hooks()` (so live and replay get the same closures). `build_summon(state, spec, archetype_index, id)`
  resolves `spec.archetypes[index]` to the content id, `ContentLibrary::find(id)` (null = request rejected, "unknown summon
  archetype"; the validator makes this unreachable for shipped content), copies `Archetype::base`, sets `id`, `team` (the
  summoner's), `summoner`, `summon_source`, `summon_group`, and returns it. Sim then registers that archetype's features,
  pools and save-action rows for the new id with the same three loops as `create` (factored into
  `register_archetype_rows(slot)`; `create` calls it unchanged, order kept).
- `build_form(state, creature, archetype_index) -> FormStats` reads the monster@4 stat block (or the shapechanger trait's
  `forms[i]`) and fills `FormStats`; for Wild Shape sim keeps Int/Wis/Cha and proficiencies per 2.2 before returning (or rules
  merges them: rules owns kept-vs-replaced, sim only builds the beast block, recommended).
- Purity: both hooks read only `ContentLibrary` (immutable during a combat) and their arguments; no rng, no registry, no
  `slots_`. HP is the archetype's fixed `hp` (no rolled HP), which is how content already works.
- The `Archetype*` of a summon slot is the resolved archetype, so features (`find_active`), equipment (`equipment_`) and the
  AI see it like any creature. The SpellBook already holds every spell; no compile at summon time.

### 3.4 AI and driver

- **Initiative mirroring**: rules owns `order`; sim keeps no copy. Required change: the driver's turn detector in
  `run_to_end` keys on `(round, turn_index)`; an insertion before the current actor shifts `turn_index` mid-turn and would
  look like a new turn. Key it on `(round, current_actor, TurnStarted count)` instead (count of `TurnStarted` events in
  `events_`), which is identical today.
- **cast_stamp** (`round << 16 | turn_index`, used by Heat Metal's sustain and the driver's `spell_kind_command`): same
  hazard. **Director ruling (accepted):** the encoding is kept; when rules shifts `turn_index` (summon inserted into or
  inserted into `order`; as built, ended summons stay in `order`, so removal never shifts it), it rewrites the `cast_stamp` of every live effect in the same step so "cast this turn" stays true.
  This happens only in combats with summons, so no golden moves. Sim's driver keeps comparing against `round << 16 | turn_index`
  unchanged. 0xAC folds the rewritten stamps (3.2).
- **Policies**: `PolicyFor` is called with summon ids too; the default for a summon is `SummonPolicy` (built in):
  - control None / Telepathic / Verbal with `commanded` this turn (or Own-initiative familiars): `NearestEnemyPolicy`, except
    Find Familiar (`can_attack = false`): Help the summoner's nearest ally against its target if adjacent, else Dodge.
  - uncommanded (BonusAction / Verbal control, no command): `Dodge`, no movement, reactions per Q4 (`legal_actions` restricts).
  - surprised / gone: `EndTurn`.
- **Commanding** (summoner's turn, built-in tactic, before its own attack logic): if it controls any summon with
  `control = Verbal | Telepathic`, submit one free `CommandSummons{ids = empty (all), command = Act}` at turn start; for
  `BonusAction` control, spend the bonus action on it only when no better bonus action exists (the existing bonus-action
  priority: healing feature, then `bonus_attack_grant`, then command, then Heat Metal sustain). Commands are logged requests
  (0xBC), so replay needs no policy.
- **Summoning**: the AI casts a summon spell only from content priority lists (none by default, like conversions in #152);
  archetype choice per Q1 (highest CR, then lowest content-id byte order); tiles by 3.1's deterministic choice.
- **Transforms**: AI Wild Shapes by an engine heuristic (Director ruling (b), no content key; `driver.cpp`): at the start of its turn when the feature is usable (pool use left, action economy free), it is not concentrating at all, and either a visible living hostile is within 2 tiles or it has no spell slots left (pact slots included; no slots at all counts as spent), into the highest-CR allowed form (ties: the earlier forms[] entry);
  reverts by bonus action never (it lets 0 HP revert it); shapechangers stay in their spawn form. Polymorph as an ordinary
  save spell aimed at the highest-CR hostile.
- **Headless driver**: a summon's turn is driven like any creature; a `gone` combatant never becomes current (rules removes
  it from `order`); `max_commands` is unchanged.

### 3.5 End of encounter teardown (Q15, ruled) and campaign roster

**Director ruling (Q15):** when the encounter ends, Combat ends every summon and transform it owns, including concentration
ones, with the normal end events. Nothing is carried into another Combat for now.

- **Trigger:** the step that makes the encounter end (rules decides `EncounterEnded`), live or in replay. Teardown is part
  of that step, so replay reproduces it from the log alone. No log entry or request field is added.
- **Order (deterministic).** SIM_EVENTS.md ordering rule 7 is unchanged (Director): `EncounterEnded` is the last event of
  the stream.
  0. **Concentrating summons' `ConcentrationEnded`** (ascending summon id), before the block (rules PR 2).
  1. **Summons end** in ascending summon id. Each one emits `SummonEnded{actor = summoner, target = id, a = source, b = 5}`.
     Reason 5 means "encounter ended" (2.5). Spiritual Weapon rows (`ActiveSummonWeapon`) end in the same pass, by token id.
     A transformed summon just ends, with no separate `Reverted`.
  2. **`TokenRemoved`** for each of them, emitted inline: each one directly follows its `SummonEnded` (or its weapon row's
     end). This is a documented **exception** to ordering rule 4, under which sim events normally come after the step's rules
     events. It applies only to the teardown block.
  3. **Transforms revert** in ascending combatant id, for creatures that are not gone. Each one emits
     `Reverted{actor = id, a = source, b = 5, c = 0}`. Reason 5 means "encounter ended" (2.7). Nothing overflows, and stats
     come back from `base`.
  4. **`EncounterEnded`**, last.
- **How sim does it.** Rules emits the ending step as [the step's own events][teardown `SummonEnded`... then `Reverted`...]
  [`EncounterEnded`]. It reports where the teardown starts with `StepResult::teardown_begin` (an index into the step's
  events, or none). `Combat::complete` then builds the stream in this order:
  - (a) the step's rules events before `teardown_begin`;
  - (b) the step's ordinary sim events (`TileMoved`, drag `Moved`, `SpellTarget`), by the usual rules 4 and 5;
  - (c) the teardown rules events, each `SummonEnded` followed at once by its `TokenRemoved`, then the `Reverted`s;
  - (d) `EncounterEnded`.

  So every sim event of the step comes before `EncounterEnded`. When a step ends the encounter with no summon or transform
  alive, `teardown_begin` is none: (c) is empty, and the only change from today is that (b) is placed before
  `EncounterEnded`. **Version gate (Director ruling).** Today `complete()` appends `TileMoved` / `SpellTarget` after
  *all* of the step's rules events (combat.cpp ~L649-771), so before v10 a fight that ends on a spell or move step has sim
  events after `EncounterEnded` (the known exception under rule 7 in SIM_EVENTS.md). From v10 on, `complete` places **every**
  sim event of the ending step ((b) and the teardown `TokenRemoved`s) before `EncounterEnded`, whether or not there is a
  teardown. Streams below v10 keep today's order, so no golden moves and none is re-pinned.
  - **The gate.** It reads the stream as it will be after this step: `event_stream_version(events_ + this step's rules and
    sim events) >= 10`. That is true when the stream already has a v10 construct (any of kinds 48-52, `TokenSpawned`,
    `TokenRemoved`) or this step adds one. Each v10 construct only enters the stream through a summon or transform, so every
    pre-R-G12 stream evaluates below 10 and keeps its bytes. Moving events within the step does not change which constructs
    are present, so the gate gives the same answer before and after the reorder; it is not circular. Replay runs the same
    `complete` and so takes the same branch.
  - **Fallback** (if the gate turns out messy in code): reorder only when (c) is non-empty. Every stream with a teardown is
    v10 anyway, and every other stream keeps today's order.
  Live play and replay share `complete`, so both produce the same order. Sim tests lock it, and SIM_EVENTS.md
  gets the rule-4 exception in the implementation PR.
- Concentration links do not fire separately. Teardown ends the summons first, and the summoner's concentration is not
  ended by sim. So there is no duplicate `SummonEnded{b = 1}`. One change (rules PR 2): a **summon that is itself
  concentrating** has that concentration ended through `end_concentration` (`ConcentrationEnded`, with its modifiers / zones /
  Polymorph links) right before the `SummonEnded{b = 5}` block; other combatants' concentration at encounter end is as before.
- **Hash:** after teardown every summon slot is `gone` and every `form` is None. 0xAF still folds (the counter has moved),
  so the final `Combat::hash` records the teardown. Combats without summons or transforms emit nothing and fold nothing.
- `Combat::state()` after the end is therefore free of summons and forms. A caller that copies it into the rest roster
  (`sim/rest.hpp`) gets creatures in their base forms, and its summons are gone (the slot is marked, the entity is destroyed).
- **TODO (campaign layer, later; tracked with SRD_COMBAT_COVERAGE §11.4a):** carrying long-lived summons (Find Familiar,
  Find Steed, Animate Dead control) and lasting transforms between fights. This will be an optional carry-over input to
  `Combat::create`, a replay input like the surprised set, with ids allocated by a campaign-level counter. Until it exists,
  a familiar is re-summoned in each fight.

## 4. Content / schema needs (Tools & Content)

- spell@1 effect kind `summon { creatures: [{archetype: <content id>, count}] | choose: {max_cr, count_by_cr: {...}},
  initiative: own | group | summoner_turn, control: verbal | bonus_action | telepathic, ends_at_zero_hp, hostile_on_break,
  concentration_link }` (closed arm).
- spell@1 effect kind `summon_weapon { attack: {dice, type, reach_ft}, move_ft, bonus_action_attack }` (Spiritual Weapon).
- spell@1 effect kind `polymorph { max_cr: "target_level" | n, save: {ability: wis}, unwilling_save, excludes_shapechangers }`.
- character@N feature kind `wild_shape { uses, recharge: short_rest, max_cr_by_level: [...], no_fly_below, no_swim_below,
  duration_hours: "half_level" }`. (Draft. As merged in #163 / 7.2q and read by the sim loader: `wild_shape { pool, cost, class, forms,
  max_cr_by_level: [{level, max_cr}], no_fly_below, no_swim_below }`, no duration key; uses / recharge come from the pool.)
- monster@4 trait `shapechanger { forms: [{id, name, size, ac, speed, attacks: [...], ...overrides}] }` (statistics other than
  size are the same unless listed).
- creature archetypes for familiars / steeds / conjured beasts: already monster@4 stat blocks (bat, cat, owl, wolf, warhorse,
  skeleton, zombie, ...); T&C confirms the SRD set ships in `srd51.core`.
- Validator: archetype ids resolve; CR limits; Find Familiar forms list.

## 5. Open questions (all ruled by Director: D1..D15; every recommendation accepted, D15 as recommended)

- **Q1 Who chooses the creatures of Conjure Animals / the conjure family?** SRD: the DM. **Recommend:** the content lists allowed
  archetypes per CR band; the caster's request picks (AI: strongest by CR then id). Director may pin a per-encounter choice.
  - **Ruled (Director, D1):** Accepted. The caster's request picks both the CR band and the specific creature (an archetype from loaded content); the choice is in the logged request (`summon_archetypes`, log_hash 0xBC), so replay matches.
- **Q2 Animate Dead initiative: own per creature or shared?** SRD is silent. **Recommend:** one shared group per cast (like the
  conjure family), so a cast adds one slot of turns.
  - **Ruled (Director, D2):** Accepted as recommended.
- **Q3 Spiritual Weapon as a combatant or a non-creature effect?** **Recommend:** non-creature (`ActiveSummonWeapon`): no HP, not
  targetable, attacks in the caster's bonus action; position is a sim token without a Combatant.
  - **Ruled (Director, D3):** Accepted as recommended.
- **Q4 Uncommanded summons:** SRD conjure: "defends itself and otherwise takes no actions"; Animate Dead: "only defends itself".
  **Recommend:** Dodge on its turn and opportunity attacks / reactions allowed; no movement.
  - **Ruled (Director, D4):** Accepted: Dodge is the default. The SRD says these creatures "defend themselves", so AI retaliation (attacking a creature that attacked it) may be added later without a rules change.
- **Q5 Conjure Elemental hostile on broken concentration:** **Recommend:** model it (team flips to the summoner's enemies, keeps
  its turn); level 5, so after PR 2.
  - **Ruled (Director, D5):** Accepted as recommended.
- **Q6 Pact of the Chain attack trade:** **Recommend:** `Attack` request flag `familiar_attack` that spends one of the warlock's
  attacks and the familiar's reaction (PR 3).
  - **Ruled (Director, D6):** Accepted as recommended.
- **Q7 Wild Shape "features it can physically use":** **Recommend:** keep all class / racial features unless content marks a
  feature `requires_humanoid_form`; spellcasting always off (SRD), concentration kept.
  - **Ruled (Director, D7):** Accepted. SRD 5.1 Wild Shape: you retain the benefit of any features from your class, race, or other source and can use them "if the new form is physically capable of doing so"; content marks the exceptions (`requires_humanoid_form`).
- **Q8 Temporary HP across a transform:** SRD is silent. **Recommend:** keep them on the creature (they absorb damage before the
  form's HP); not part of the saved base block.
  - **Ruled (Director, D8):** Accepted as recommended.
- **Q9 Find Steed / mounted combat:** **Recommend:** out of R-G12 (with R-G17); Find Steed summons on its own initiative unmounted.
  - **Ruled (Director, D9):** Accepted as recommended.
- **Q10 Summons when the summoner dies:** SRD: Find Familiar / Find Steed persist; concentration summons end with concentration
  (death ends it). **Recommend:** follow SRD per spell (`SummonEnded.b = 1` via concentration; no generic "summoner died" end).
  - **Ruled (Director, D10):** Accepted as recommended.
- **Q11 Ids for summons:** **Recommend:** sim counter above the create-time maximum, carried in the logged request (2.6).
  - **Ruled (Director, D11):** Accepted, refined with Engine Core (§2.6, §3.0): the counter starts above every chunk spawn id, is
    folded in 0xAF, ids are never reused, live callers cannot supply `summon_ids`, and replay rejects mismatched ids.
- **Q12 Massive damage on revert:** **Recommend:** instant death checks the overflow against the base form's hp_max (SRD death
  rules apply to the normal form after carrying over).
  - **Ruled (Director, D12):** Accepted as recommended.
- **Q13 Polymorph on a shapechanger / an unwilling target at 0 HP:** **Recommend:** refuse on shapechangers (`excludes_shapechangers`);
  a 0-HP target can't be polymorphed (no effect).
  - **Ruled (Director, D13):** Accepted. SRD 5.1 Polymorph: the spell "has no effect on a shapechanger or a creature with 0 hit points"; rules refuses both targets (no effect).
- **Q14 Initiative position for a summon whose roll beats the current actor:** **Recommend:** first turn next round (2.4); no
  turn in the round it appeared after its slot has passed.
  - **Ruled (Director, D14):** Accepted as recommended.
- **Q15 Summons and transforms at the end of the encounter** (from Engine Core): do concentration summons end when the
  encounter ends? **Recommend:** yes for now: every summon and transform ends at encounter end (`Combat::complete`), with
  `SummonEnded` / `Reverted` with reason 5 ("encounter ended"); carrying them in the campaign roster can come later.
  - **Ruled (Director, D15):** yes. When the encounter ends, Combat ends every summon and transform it owns, concentration ones
    included, with the normal end events (`SummonEnded` / `Reverted`, reason 5 "encounter ended"; §3.5). Carry-over between fights
    is deferred to the campaign layer (`SRD_COMBAT_COVERAGE.md` §11.4a TODO).

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
