#pragma once
// Spellcasting core (SRD 5.1 Chapter 10 "Spellcasting"). Pure data + functions; spell
// definitions come from content (spell@1, loaded by rules5e_content) or are built in code.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "rules5e/types.hpp"

namespace rules5e {

// Components (SRD "Components"), as flags. Rules only record them; sim decides whether they can
// be provided (hooks.can_provide_components).
inline constexpr uint8_t kComponentV = 1, kComponentS = 2, kComponentM = 4;

enum class CastingTime : uint8_t { Action, BonusAction, Reaction, Minute, Hour };   // Minute/Hour: not in encounters
enum class SpellDuration : uint8_t { Instantaneous, Rounds, Minutes, Hours, UntilStartOfCasterTurn, UntilDispelled, Special };
enum class AreaShape : uint8_t { None, Cone, Cube, Cylinder, Line, Sphere };
enum class DartDistribution : uint8_t { Any, Single, Distinct };
// What the spell does. Append-only.
enum class SpellEffectKind : uint8_t {
    SpellAttack,    // spell attack roll vs AC, damage on a hit (Fire Bolt)
    Save,           // saving throw vs spell save DC: damage (full / half) and/or a condition (Hold Person)
    Heal,           // hit points regained (Cure Wounds)
    AutoHit,        // darts that hit automatically (Magic Missile)
    AcBonus,        // reaction: AC bonus until the start of the caster's next turn (Shield)
    Counter,        // reaction: interrupts a spell being cast (Counterspell)
    ApplyCondition, // a condition on each target with no save (Invisibility); lasts with the spell
    ApplyModifier,  // row 12: a timed modifier (Bless, Bane, Haste, Slow, Shield of Faith, Faerie Fire, Hunter's Mark); SpellDef::modifier
    // appended (R-G15):
    HpPool,         // roll `dice` (scaled) as a hit-point pool; targets by lowest current HP get `condition` while the pool covers them (Sleep, Color Spray)
    Dispel,         // end spells on each target: slot level or lower automatically, higher ones on a check (Dispel Magic)
    Revive,         // a creature dead for at most `revive_within_rounds` returns with 1 HP (Revivify)
    EndCondition,   // ends one condition of `ends_conditions` on each target, the lowest Condition value present (Lesser Restoration)
    DelayedDamage,  // `dice` of `damage_type` when the effect's timer runs out (`expiry`; Acid Arrow: end of the target's next turn)
    SustainedDamage,// `dice` now, and again each time the caster sustains it with a bonus action (Heat Metal; concentration)
    // appended (R-G9, docs/proposals/FORCED_MOVEMENT.md):
    ForcedMove,     // push / pull each target (`forced`); mostly an `affected_only` rider (Thunderwave, Repelling Blast)
    Teleport,       // the caster teleports (`teleport`; Misty Step); the destination is sim's (ActionRequest::teleport_*)
    // appended (R-G11, docs/proposals/ZONES_AURAS.md): a persistent zone or aura (`zone`; primary effect only)
    Zone,
};
// R-G9 forced movement (push / pull) of an effect. mode 1 push (away from the source), 2 pull (toward it), 3 along a line
// (Gust of Wind: sim's direction; not in R-G9's first scope). feet: exact distance, or the maximum when up_to (the caster
// picks a multiple of 5 with ActionRequest::forced_feet). max_size: largest Size moved (-1 = no limit; over it: the push part
// only is skipped). provokes_oa: SRD never; true is a content load error in R-G9.
struct ForcedMoveSpec {
    uint8_t  mode = 1;
    uint16_t feet = 0;
    bool     up_to = false;
    int8_t   max_size = -1;
    bool     provokes_oa = false;
};
// R-G9 self-teleport (Misty Step 30, blink dog 40). Companions (Dimension Door) and the occupied-destination damage are deferred.
struct TeleportSpec {
    uint16_t max_feet = 0;
    bool     needs_sight = true;   // sim's destination check
};
enum class OnSave : uint8_t { None, Half };          // damage on a successful save
enum class RepeatSave : uint8_t { None, EndOfTurn, StartOfTurn, OnDamage };   // condition: target repeats the save

// One further effect of a multi-effect spell (SpellDef::extra_effects; effects 2..n of spell@1 `effects`).
// Mirrors the per-effect fields of SpellDef. Extra effects resolve in order over the same targets as the
// first effect and do not scale with slot level or caster level (scaling belongs to the first effect).
struct SpellEffect {
    SpellEffectKind kind = SpellEffectKind::Save;
    bool melee = false; uint8_t beams = 1;                    // SpellAttack
    DiceExpr dice{0, 0, 0}; DamageType damage_type = DamageType::Force;
    bool add_spellcasting_modifier = false; uint32_t excluded_types = 0;   // Heal
    Ability save_ability = Ability::Wis; OnSave on_save = OnSave::None;    // Save
    bool applies_condition = false; Condition condition = Condition::Paralyzed;   // Save / ApplyCondition
    RepeatSave repeat_save = RepeatSave::None;
    uint8_t darts = 0; DartDistribution distribution = DartDistribution::Any; bool simultaneous = false;   // AutoHit
    // Rider: resolve only for the targets the previous effect affected (spell attack: hit; save: failed;
    // heal: healed; auto-hit: struck; apply_condition: received it). false = every target.
    bool affected_only = false;
    // appended: timed condition (Save / ApplyCondition): ends after `duration_rounds` rounds or at the end of the
    // target's next turn. `expiry` holds an rules5e::Expiry value (encounter.hpp): 0 none, 1 rounds, 2 end of target's next turn.
    uint8_t expiry = 0;
    uint16_t duration_rounds = 0;      // expiry == 1
    ModifierSpec modifier{};           // appended (row 12 follow-up): kind ApplyModifier as a rider (Guiding Bolt, Ray of Frost, Vicious Mockery)
    ForcedMoveSpec forced{};           // appended (R-G9): kind ForcedMove
    TeleportSpec teleport{};           // appended (R-G9): kind Teleport
    // appended: Save / ApplyCondition with expiry 0 and no repeat save, for a condition that ends by its own rules (prone: the
    // creature stands up). Applied like Shove's prone: the condition only, no ActiveSpellEffect entry.
    bool condition_untracked = false;
};

// ---- R-G11 persistent zones and auras (docs/proposals/ZONES_AURAS.md §2.6) ----
// Point: fixed at a sim tile (ActionRequest::zone_x / zone_y, kept opaque in ActiveZone); Creature: an aura centred on the
// caster that moves with it (Spirit Guardians). Geometry and membership are sim's; rules keeps lifetime and triggers.
enum class ZoneAnchor : uint8_t { Point, Creature };
// Appear: once, when the zone is created, for each creature in it. Enter: the creature moves into it (own move, forced move,
// teleport; an aura / zone move onto it only with counts_aura_move_as_enter). StartTurn / EndTurn: the creature's turn starts /
// ends while it is in the zone. MoveWithin: every 5 ft the creature travels into or inside it (Spike Growth).
enum class ZoneTrigger : uint8_t { Appear, Enter, StartTurn, EndTurn, MoveWithin };
// Zone flags. Terrain and sense flags are data for sim (difficult-terrain overlay, halved-speed cost, Q5); obscurement and
// Silence are stored only until R-G13 / rules PR 2.
inline constexpr uint8_t kZoneDifficult = 1, kZoneLightObscured = 2, kZoneHeavyObscured = 4, kZoneMagicalDarkness = 8,
                         kZoneSilence = 16, kZoneHalveSpeed = 32, kZoneOpaque = 64;
// One trigger row. `effect` reuses SpellEffect for what happens: kind Save = a saving throw (save_ability, on_save) against the
// zone's DC, damage `dice` of `damage_type` (half on a success with OnSave::Half) and, on a failure, `condition` when
// applies_condition; any other kind = no save, the damage and condition always apply. Damage dice gain SpellDef::upcast_dice
// per slot level above the spell's level, like the spell's first effect. `while_in_zone`: the condition lasts while the
// creature stays in the zone (Web's restraint; it ends on leaving, on escape, or when the zone ends); false = an ordinary
// condition (Grease's prone).
struct ZoneTriggerRow {
    ZoneTrigger on = ZoneTrigger::Enter;
    uint8_t group = 1;                       // once-per-turn group (0 = no limit); Enter + StartTurn share group 1 (D2)
    bool own_turn_only = false;              // only during the creature's own turn (Web's "enters during its turn")
    bool completely_within = false;          // the whole footprint must be inside (zone_contains with completely = true)
    bool counts_aura_move_as_enter = false;  // D1: an aura / zone moving onto the creature counts as Enter (default no)
    bool while_in_zone = false;              // the condition lasts while in the zone (Web)
    SpellEffect effect{};
};
struct ZoneSpec {
    AreaShape shape = AreaShape::None;       // Sphere, Cylinder, Cube, Line (walls: rules PR 2)
    uint16_t size_ft = 0, height_ft = 0, length_ft = 0;
    ZoneAnchor anchor = ZoneAnchor::Point;
    uint8_t flags = 0;                       // kZone*
    uint8_t move_cost = 0;                   // 0 cannot move, 1 action (Moonbeam, D3), 2 bonus action
    uint16_t move_ft = 0;                    // Moonbeam 60
    bool chooses_excluded = false;           // Spirit Guardians: ActionRequest::zone_excluded are unaffected
    std::vector<ZoneTriggerRow> triggers{};
    bool needs_conscious_owner = false;      // feature auras (D7, later); no effect in PR 1
};

struct SpellDef {
    uint32_t id = 0;                     // spell_id_of(content_id)
    std::string content_id;              // "srd51.spell.fire_bolt"
    uint8_t level = 0;                   // 0 = cantrip
    CastingTime casting_time = CastingTime::Action;
    ReactionTrigger reaction_trigger = ReactionTrigger::BeingHit;   // Reaction spells only
    uint8_t components = 0;              // kComponentV | kComponentS | kComponentM
    bool concentration = false;
    uint16_t range_ft = 0;               // 0 = self; 5 = touch
    bool melee = false;                  // SpellAttack: melee spell attack (default ranged)
    uint8_t targets = 1;                 // Save / Heal: creatures affected at base level (ignored with an area)
    SpellEffectKind kind = SpellEffectKind::SpellAttack;
    DiceExpr dice{0, 0, 0};              // damage / healing / per-dart damage
    DamageType damage_type = DamageType::Force;
    bool add_spellcasting_modifier = false;  // Heal: + spellcasting ability modifier
    Ability save_ability = Ability::Wis; // Save
    OnSave on_save = OnSave::None;       // Save with damage
    bool applies_condition = false;      // Save: condition on a failed save
    Condition condition = Condition::Paralyzed;
    RepeatSave repeat_save = RepeatSave::None;
    uint8_t darts = 0;                   // AutoHit
    ModifierSpec modifier{};             // ApplyModifier (primary effect only): `source` and `concentration` are filled from the spell
    int8_t ac_bonus = 0;                 // AcBonus
    uint32_t blocks_spell = 0;           // AcBonus: also takes no damage from this spell (Shield vs Magic Missile)
    uint8_t counter_dc_base = 10;        // Counter: check DC = base + spell level when above the slot level
    // Scaling
    std::array<uint8_t, 3> cantrip_tiers{5, 11, 17};   // cantrip: grows at each caster level reached
    bool cantrip_scales = false;
    bool cantrip_adds_beams = false;     // cantrip growth adds a beam instead of the base dice
    uint8_t upcast_beams = 0;
    uint8_t upcast_dice = 0;             // + this many dice per slot level above `level`
    uint8_t upcast_darts = 0;            // AutoHit: + darts per slot level above
    uint8_t upcast_targets = 0;          // + targets per slot level above
    // appended (spell@1 review):
    uint8_t casting_amount = 0;          // Minute / Hour casting times
    std::vector<uint32_t> or_targeted_by;  // BeingHit reaction: also offered when targeted by these spells
    bool requires_sight = false;         // reaction: reactor must see the trigger actor (hooks.can_see)
    bool ritual = false;
    uint32_t material_cost_cp = 0; bool material_consumed = false;   // sim/inventory checks them
    SpellDuration duration = SpellDuration::Instantaneous; uint16_t duration_amount = 0;
    uint32_t target_types = 0;           // bitmask over CreatureType; 0 = any
    uint32_t excluded_types = 0;         // Heal: no effect on these (Cure Wounds: undead, construct)
    uint16_t max_spread_ft = 0;          // targets within this distance of each other (hooks.targets_within)
    uint8_t beams = 1;                   // SpellAttack: separate attack rolls
    DartDistribution distribution = DartDistribution::Any;   // AutoHit
    bool simultaneous = false;           // AutoHit: every target reacts before any damage
    AreaShape area = AreaShape::None; uint16_t area_size_ft = 0, area_width_ft = 0, area_height_ft = 0;
    std::vector<SpellEffect> extra_effects;   // appended: effects 2..n (see SpellEffect); empty = single-effect spell
    // appended (version-bump batch, row 5). Defaults preserve every existing spell.
    uint8_t expiry = 0;            // appended (timed effects): see SpellEffect::expiry
    uint16_t duration_rounds = 0;
    bool    is_spell = true;       // false: no SpellCast window (cannot be Counterspelled); emits ActionUsed instead of SpellCast
    bool    magical = true;        // SaveSource.magical and damage qualifier (breath weapons are NOT magical)
    int8_t  fixed_dc = 0, fixed_attack = 0;   // 0 = derive from the caster (spell_save_dc / spell_attack_bonus)
    uint8_t usage_kind = 0;        // 0 at-will, 1 recharge (ActionUse), 2 per-day/rest (FeatureUses row keyed by spell id), 3 pool
    uint8_t recharge_min = 0;      // usage_kind == 1: recharges on a d6 roll >= this
    uint32_t pool_id = 0; uint8_t pool_cost = 0;   // usage_kind == 3 (0 cost = 1)
    // appended (R-G15). HpPool: the condition ends early when the creature takes damage (Sleep); Dispel: check DC = base + the
    // spell's level for spells above the slot level; Revive: rounds since death allowed (Revivify: 1 minute = 10 rounds).
    bool ends_on_damage = false;
    uint8_t dispel_dc_base = 10;
    uint8_t revive_within_rounds = 10;
    uint32_t ends_conditions = 0;   // EndCondition: bitmask over Condition (Lesser Restoration: blinded, deafened, paralyzed, poisoned)
    // appended (R-G14): school flag for Sculpt Spells, and cast-time choices. Non-empty `damage_choices` (Chromatic Orb) /
    // `condition_choices` (Blindness/Deafness) require ActionRequest::damage_choice / condition_choice (1 + index), which replace
    // the primary effect's damage_type / condition for that cast.
    bool evocation = false;
    std::vector<DamageType> damage_choices;
    std::vector<Condition> condition_choices;
    // appended (Petrifying Gaze, primary Save with a condition): a failed save applies `condition` (Restrained) with the
    // repeat save; failing that repeat save turns it into `escalate_to` (Petrified) for good. escalate_margin > 0: failing the
    // first save by that much or more applies `escalate_to` at once (medusa: 5).
    bool escalates = false; Condition escalate_to = Condition::Petrified; uint8_t escalate_margin = 0;
    // appended (R-G9): primary-effect forced movement / teleport (extra effects carry their own).
    ForcedMoveSpec forced{};
    TeleportSpec teleport{};
    // appended (R-G11): kind Zone (primary effect only; an extra effect of kind Zone is refused at cast).
    ZoneSpec zone{};
    // appended (priced materials, CONTENT_CONVENTIONS §7): components.m.item, the equipment content id that provides a priced
    // material ("" = none: a priced material without an item is unsatisfiable). rules5e never reads it (sim's components hook does).
    std::string material_item;
    // appended: see SpellEffect::condition_untracked (primary effect).
    bool condition_untracked = false;
};

// R-G14 Metamagic options (Combatant::metamagic_known, ActionRequest::metamagic). One option per spell, except that Empowered
// may be combined with another (SRD 5.1). Costs in sorcery points: metamagic_cost.
inline constexpr uint8_t kMetaCareful = 1, kMetaDistant = 2, kMetaEmpowered = 4, kMetaExtended = 8, kMetaHeightened = 16,
                         kMetaQuickened = 32, kMetaSubtle = 64, kMetaTwinned = 128;
// Total sorcery points for `options` at `slot_level`: Careful / Distant / Empowered / Extended / Subtle 1, Quickened 2,
// Heightened 3, Twinned = the slot level (1 for a cantrip).
constexpr int metamagic_cost(uint8_t options, uint8_t slot_level) {
    int c = 0;
    if (options & kMetaCareful) c += 1;
    if (options & kMetaDistant) c += 1;
    if (options & kMetaEmpowered) c += 1;
    if (options & kMetaExtended) c += 1;
    if (options & kMetaSubtle) c += 1;
    if (options & kMetaQuickened) c += 2;
    if (options & kMetaHeightened) c += 3;
    if (options & kMetaTwinned) c += slot_level == 0 ? 1 : slot_level;
    return c;
}

// The spell as seen by one extra effect: `base` with that effect's fields, no scaling, no further effects.
SpellDef effect_view(const SpellDef& base, const SpellEffect& e);

// Owned by the encounter hooks (shared, immutable). Sorted by id.
class SpellBook {
public:
    // Returns false on a duplicate id (content id hash collision).
    bool add(SpellDef);
    // Removes a definition (a replaced archetype's synthetic spells). Returns false if absent.
    bool erase(uint32_t id);
    const SpellDef* find(uint32_t id) const;
    const std::vector<SpellDef>& all() const { return defs_; }
private:
    std::vector<SpellDef> defs_;
};

// FNV-1a 32-bit of the content id: the numeric spell_id used in requests, events, and logs.
constexpr uint32_t spell_id_of(std::string_view content_id) {
    uint32_t h = 2166136261u;
    for (char c : content_id) { h ^= uint8_t(c); h *= 16777619u; }
    return h;
}

// SRD: spell save DC = 8 + proficiency bonus + spellcasting ability modifier;
// spell attack bonus = proficiency bonus + spellcasting ability modifier.
int spell_save_dc(const Combatant&);
int spell_attack_bonus(const Combatant&);
// Per-definition forms: fixed_dc / fixed_attack when non-zero, else the caster-derived value above.
int spell_save_dc(const Combatant&, const SpellDef&);
int spell_attack_bonus(const Combatant&, const SpellDef&);
// Cantrip damage tier (0..3) for a caster level: number of entries in `tiers` reached.
int cantrip_tier(const SpellDef&, uint8_t caster_level);
// Damage/healing dice when cast with a slot of `slot_level` by a caster of `caster_level`.
DiceExpr scaled_dice(const SpellDef&, uint8_t slot_level, uint8_t caster_level);
int scaled_darts(const SpellDef&, uint8_t slot_level);
int scaled_targets(const SpellDef&, uint8_t slot_level);
int scaled_beams(const SpellDef&, uint8_t slot_level, uint8_t caster_level);
bool valid_target_type(const SpellDef&, const Combatant&);

// Slots: spell_slots[1..9] (long rest) and Pact Magic slots (short or long rest).
bool has_slot(const Combatant&, uint8_t level);         // level 0 (cantrip) is always true
bool spend_slot(Combatant&, uint8_t level);             // regular slot first, then a pact slot of that level
void recover_slots_short_rest(Combatant&);              // Pact Magic slots
void recover_slots_long_rest(Combatant&);               // all slots
bool knows_spell(const Combatant&, uint32_t spell_id);

} // namespace rules5e
