#pragma once
// Spellcasting core (SRD 5.1 Chapter 10 "Spellcasting"). Pure data + functions; spell
// definitions come from content (spell@1, loaded by rules5e_content) or are built in code.
#include <array>
#include <cstdint>
#include <utility>
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
    ApplyModifier,  // row 12: a timed modifier (Bless, Bane, Haste, Slow, Shield of Faith, Faerie Fire); SpellDef::modifier
    // appended (R-G15):
    HpPool,         // roll `dice` (scaled) as a hit-point pool; targets by lowest current HP get `condition` while the pool covers them (Sleep, Color Spray)
    Dispel,         // end spells on each target: slot level or lower automatically, higher ones on a check (Dispel Magic)
    Revive,         // a creature dead for at most `revive_within_rounds` returns with 1 HP (Revivify)
    EndCondition,   // ends one condition of `ends_conditions` on each target, the lowest Condition value present, or the cast-time
                    // ActionRequest::condition_choice (1 + index into ends_condition_list, JSON order) (Lesser Restoration)
    DelayedDamage,  // `dice` of `damage_type` when the effect's timer runs out (`expiry`; Acid Arrow: end of the target's next turn)
    SustainedDamage,// `dice` now, and again each time the caster sustains it with a bonus action (Heat Metal; concentration)
    // appended (R-G9, docs/proposals/FORCED_MOVEMENT.md):
    ForcedMove,     // push / pull each target (`forced`); mostly an `affected_only` rider (Thunderwave, Repelling Blast)
    Teleport,       // the caster teleports (`teleport`; Misty Step); the destination is sim's (ActionRequest::teleport_*)
    // appended (R-G11, docs/proposals/ZONES_AURAS.md): a persistent zone or aura (`zone`; primary effect only)
    Zone,
    // appended (R-G12 rules PR 1, docs/proposals/SUMMONS_TRANSFORMS.md): one creature becomes a beast (Polymorph): unwilling
    // (another team) makes a Wisdom save; the form is ActionRequest::form_archetype through EncounterHooks::build_form; no effect
    // on a shapechanger or a creature with 0 hit points (D13: refused). Ends at 0 HP (overflow carries), death or concentration.
    Polymorph,
    // appended (R-G12 rules PR 2): creatures appear (Conjure Animals, Find Familiar, Animate Dead); `summon`. Cast on the caster;
    // the creatures are ActionRequest::summon_archetypes through EncounterHooks::build_summon, with ids from the encounter's counter.
    Summon,
    // appended (R-G12 rules PR 3, D3): Spiritual Weapon: a non-creature token (`ActiveSummonWeapon`) whose attack is a melee spell
    // attack (dice + the spellcasting modifier when add_spellcasting_modifier) on the cast and as a later bonus action.
    SummonWeapon,
    // appended (7.2zd): immunity of each target to `immune_conditions` from creatures of `immune_from_types` while the spell
    // lasts (Protection from Evil and Good: charmed, frightened); no condition of its own.
    ConditionImmunityFrom,
    // appended (7.2zd Aid): each target's hit point maximum and current hit points rise by hp_bonus_flat + hp_bonus_per_slot
    // per slot level above the spell's; when the effect ends the maximum drops back and current HP clamps to it.
    MaxHpBonus,
    // appended (7.2zf Shillelagh, Magic Weapon): imbues the caster's weapon (`imbue`); see WeaponImbueSpec.
    WeaponImbue,
    // appended (7.2zf Mirror Image): illusory duplicates on the caster (`duplicates`); see DuplicatesSpec.
    Duplicates,
    // appended (7.2zg False Life, Heroism): temporary hit points on each target (`temp_hp`); see TempHpSpec.
    TempHp,
    // appended (7.2zh Branding Smite): a charge on the caster until their next weapon-attack hit (`weapon_smite`); see WeaponSmiteSpec.
    WeaponSmite,
    // appended (7.2zi Spare the Dying): stabilize one dying living creature within touch. No hook. Undead and constructs
    // are excluded by excluded_types (the same mask heal uses).
    Stabilize,
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
enum class RepeatSave : uint8_t { None, EndOfTurn, StartOfTurn, OnDamage,
                                  // appended (7.2v control conditions): EndOfTurnUnseen = at the end of the target's turn, only
                                  // while it can't see the caster (Fear; EncounterHooks::can_see, no hook = seen, no save);
                                  // EndOfTurnOrDamageAdv = at the end of each turn, and each time it takes damage with advantage
                                  // (Hideous Laughter).
                                  EndOfTurnUnseen, EndOfTurnOrDamageAdv };
// appended (7.2v): a control effect restricts what its target may do (rules: the legal actions; sim: the path).
//   Turned (Turn Undead): Move, Dash, Dodge, StandUp, escape (Attack special 4); no reactions; ends on damage.
//   Fear: while frightened by it, Move, Dash, Dodge, StandUp (sim moves it away from the caster).
//   Laughter (Hideous Laughter): prone and incapacitated; can't stand up.
//   Command: on the target's next turn, by ActionRequest::command_word (CommandWord).
enum class ControlKind : uint8_t { None, Turned, Fear, Laughter, Command };
enum class CommandWord : uint8_t { None, Approach, Drop, Flee, Grovel, Halt };   // ActionRequest::command_word (log_hash 0xC4)

// One further effect of a multi-effect spell (SpellDef::extra_effects; effects 2..n of spell@1 `effects`).
// Mirrors the per-effect fields of SpellDef. Extra effects resolve in order over the same targets as the
// first effect and do not scale with slot level or caster level (scaling belongs to the first effect), except a
// DelayedDamage rider's upcast_dice (Acid Arrow's later damage).
// 7.2zf: spell@1 `weapon_imbue`. The imbued weapon is the caster's first weapon attack (not a spell attack) whose Attack::id is
// in `weapons` (the full item id or its last segment), or the first weapon attack when `weapons` is empty. While it lasts:
// spellcasting_ability: melee attacks with it use the spellcasting modifier instead of Strength (to-hit and damage move by the
// difference; the authored Attack is assumed Strength-based); die > 0: its damage die becomes a d<die>; magical: AttackQualifier
// magic; bonus_base / bonus_at_slot: + the bonus to attack and damage rolls ([slot level, bonus] pairs, slots ascending).
// requires_nonmagical: refused on an already magical weapon. ends_on_release: ends when the holder lets go (equipment play).
struct WeaponImbueSpec {
    std::vector<std::string> weapons{};
    bool spellcasting_ability = false;
    uint8_t die = 0;
    bool magical = false;
    int8_t bonus_base = 0;
    std::vector<std::pair<uint8_t, int8_t>> bonus_at_slot{};
    bool requires_nonmagical = false;
    bool ends_on_release = false;
};
// 7.2zf: spell@1 `duplicates` (Mirror Image). thresholds[remaining - 1] is the d20 an attack needs to target a duplicate
// (1 left: 11, 2 left: 8, 3 left: 6); a duplicate's AC = ac_base + the caster's ac_ability modifier.
struct DuplicatesSpec {
    uint8_t count = 0;
    std::array<uint8_t, 3> thresholds{{11, 8, 6}};
    int8_t ac_base = 10;
    Ability ac_ability = Ability::Dex;
    uint8_t exempt_senses = kSenseBlindsight | kSenseTruesight;   // appended (7.2zg `exempt_senses`; the 7.2zf default)
};
// The bonus of an imbue cast at `slot` (0 when the spec has no magic bonus).
int imbue_bonus(const WeaponImbueSpec&, int slot);

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
    // appended (EC deferred monsters, flagged): ApplyCondition: the creature ends it with an action, a Strength check vs this DC
    // (special 4, like a zone's Web); the condition lasts until then (no timer). 0 = none.
    int8_t escape_dc = 0;
    // appended (EC spider follow-up, flagged): ApplyCondition with escape_dc: the restraint is also a destructible object (the
    // giant spider's web: AC 10, 5 HP, vulnerable to fire, immune to bludgeoning / poison / psychic). Attacking it is
    // ActionRequest::special 6; at 0 HP the effect (and its condition) ends. Masks are DamageType bits. object_hp 0 = none.
    int8_t object_ac = 0;
    int16_t object_hp = 0;
    uint32_t object_vulnerable = 0, object_immune = 0;
    // appended (#120 follow-up): + this many dice per slot level above the spell's level; set by the loader only on a
    // DelayedDamage extra effect (Acid Arrow), from the spell's scaling.upcast.dice. 0 = does not scale (every other effect).
    uint8_t upcast_dice = 0;
    // appended (grab and contain, 7.2w): see SpellDef::contain / contain_dice / contain_damage_type / condition_max_size.
    uint8_t contain = 0;
    DiceExpr contain_dice{0, 0, 0}; DamageType contain_damage_type = DamageType::Acid;
    int8_t condition_max_size = -1;
    int8_t regurgitate_dc = 0; uint8_t regurgitate_damage = 30;   // 7.2w: see SpellDef
    bool excludes_formless = false;                                // 7.2w: see SpellDef
    uint8_t ends_on = 0;                                           // 7.2zc: see SpellDef::ends_on
    bool hit_only = false;                                        // 7.2zi: spell_attack rider (primary uses SpellDef::hit_only)
    bool has_ward = false; Ability ward_save = Ability::Wis; uint8_t ward_on_fail = 1;   // 7.2zd: see SpellDef
    uint32_t immune_conditions = 0, immune_from_types = 0;                              // 7.2zd: see SpellDef
    int16_t hp_bonus_flat = 0, hp_bonus_per_slot = 0;                                   // 7.2zd: see SpellDef
};
// 7.2zc (Invisibility): apply_condition `ends_on` bits: the effect ends when its target makes an attack / casts a spell.
// kEndsOnHarmfulCast (7.2zd Sanctuary): a spell it cast affected a creature of another team.
inline constexpr uint8_t kEndsOnAttack = 1, kEndsOnCast = 2, kEndsOnHarmfulCast = 4;
// 7.2zg (Hypnotic Pattern; also on `save` effects with a condition): kEndsOnDamage: the effect ends when its target takes
// damage (ActiveSpellEffect::ends_on_damage, as Sleep); kEndsOnShakeAwake: any other creature within 5 ft may end it with its
// action (the existing Wake action, ActionRequest::special 5). While a kEndsOnShakeAwake effect lasts its target's speed is 0
// (modified_speed; SRD Hypnotic Pattern).
inline constexpr uint8_t kEndsOnDamage = 8, kEndsOnShakeAwake = 16;

// ---- R-G11 persistent zones and auras (docs/proposals/ZONES_AURAS.md §2.6) ----
// Point: fixed at a sim tile (ActionRequest::zone_x / zone_y, kept opaque in ActiveZone); Creature: an aura centred on the
// caster that moves with it (Spirit Guardians). Geometry and membership are sim's; rules keeps lifetime and triggers.
enum class ZoneAnchor : uint8_t { Point, Creature };
// Appear: once, when the zone is created, for each creature in it. Enter: the creature moves into it (own move, forced move,
// teleport; an aura / zone move onto it only with counts_aura_move_as_enter). StartTurn / EndTurn: the creature's turn starts /
// ends while it is in the zone. MoveWithin: every 5 ft the creature travels into or inside it (Spike Growth).
enum class ZoneTrigger : uint8_t { Appear, Enter, StartTurn, EndTurn, MoveWithin };
// Zone flags. Terrain and sense flags are data for sim (difficult-terrain overlay, halved-speed cost, Q5); obscurement,
// magical darkness and opaque are stored only until R-G13 (Q6). kZoneSilence is rules' (PR 2): a creature completely inside
// can't cast a spell with a verbal component, is immune to thunder damage, and is deafened while it stays in the zone.
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
    // ---- appended (R-G11 rules PR 2) ----
    // region: where a hook-answered row (Appear / StartTurn / EndTurn) finds the creature. 0 = in the zone; 1 = in the zone
    // or within its damaging side (Wall of Fire's "ends its turn within 10 feet of that side or inside the wall";
    // hooks.zone_side_contains). Rows from sim's membership facts (Enter, MoveWithin) always mean the zone itself.
    uint8_t region = 0;
    // A failed save (or the row's effect with no save) spends the creature's action for its current turn, only when it is
    // that creature's turn: TurnBudget::action_lost; movement and the bonus action remain (Stinking Cloud, D9).
    bool loses_action = false;
    // Automatic success, no roll (FeatureTriggered c = 2): the creature is immune to a damage type in auto_success_damage
    // (bitmask over DamageType) or a condition in auto_success_conditions (bitmask over Condition), or doesn't breathe
    // (Combatant::no_breath) when auto_success_no_breath (Stinking Cloud: poison, poisoned, no breath).
    uint32_t auto_success_damage = 0, auto_success_conditions = 0;
    bool auto_success_no_breath = false;
    // Moonbeam: a shapechanger (Combatant::shapechanger) makes the save with disadvantage. Reverting its form on a failure
    // is not modelled (rules has no shapechange state).
    bool shapechanger_disadvantage = false;
    // Spirit Guardians: the damage is evil_damage_type instead when the zone's owner is evil (Combatant::evil).
    bool has_evil_damage_type = false;
    DamageType evil_damage_type = DamageType::Necrotic;
};
// R-G12 rules PR 2 (SUMMONS_TRANSFORMS.md §2.2 A): how summoned creatures act. initiative: Own = each rolls (Find Familiar),
// Group = one roll for the cast, consecutive turns ordered by id (conjure family, Animate Dead: D2). control: None = acts on
// its own; Verbal (conjure: free command) / BonusAction (Animate Dead: the summoner's bonus action) = needs a command for its
// turn, else it only Dodges (D4); Telepathic (familiar) = acts on its own. vanishes_at_zero_hp: disappears at 0 HP
// (SummonEnded b = 3, no corpse) instead of dying. Concentration = the spell's.
enum class SummonInitiative : uint8_t { Own = 0, Group = 1 };
enum class SummonControl : uint8_t { None = 0, Verbal = 1, BonusAction = 2, Telepathic = 3 };
struct SummonSpec {
    uint8_t max_count = 1;   // creatures per cast (any count 1..max_count unless EncounterHooks::summon_choice_ok decides)
    SummonInitiative initiative = SummonInitiative::Own;
    SummonControl control = SummonControl::None;
    bool vanishes_at_zero_hp = true;
};
// R-G12 rules PR 3: Spiritual Weapon. upcast_every: slot levels per upcast_dice extra dice (SRD: 1d8 per two levels above 2nd);
// move_ft / reach_ft are sim's (the token's position), carried for content.
struct SummonWeaponSpec {
    uint8_t upcast_every = 2;
    uint8_t reach_ft = 5;
    uint16_t move_ft = 20;
    // appended (7.2zh Flame Blade): held in the caster's free hand. No token move, no spellcasting modifier on the damage,
    // later attacks spend the action (not the bonus action). light_*: content radii (0 = none); not hashed (spell content).
    bool held = false;
    uint16_t light_bright_ft = 0, light_dim_ft = 0;
};
// 7.2zg False Life / Heroism: amount = roll(dice, its flat included) + per_slot_above x (slot - spell level), plus the
// caster's spellcasting modifier when `ability`. per_turn: granted again at the start of each of the target's turns while the
// spell lasts (an ActiveModifier TempHp row, Heroism). immune_conditions: Condition bits the target is immune to while it lasts
// (Heroism: frightened), from any source. Integer only.
struct TempHpSpec {
    DiceExpr dice{0, 0, 0};
    int16_t per_slot_above = 0;
    bool ability = false;
    bool per_turn = false;
    uint32_t immune_conditions = 0;
};
// 7.2zh Branding Smite. The charge waits on the caster. The next weapon-attack hit (spell_id == 0, not a spell attack)
// deals `dice` of the effect's damage type (a crit doubles the dice) and, when reveal_invisible or dim_ft is set, leaves the
// brand on that target for the rest of the duration: visible if it was invisible, cannot become invisible, sheds dim light
// of dim_ft (rules stores the radius; lighting owns the source). dim_ft 0 and reveal false: the charge ends on the hit.
struct WeaponSmiteSpec {
    bool reveal_invisible = false;
    uint16_t dim_ft = 0;
};
struct ZoneSpec {
    AreaShape shape = AreaShape::None;       // Sphere, Cylinder, Cube, Line (walls)
    uint16_t size_ft = 0, height_ft = 0, length_ft = 0;
    ZoneAnchor anchor = ZoneAnchor::Point;
    uint8_t flags = 0;                       // kZone*
    uint8_t move_cost = 0;                   // 0 cannot move, 1 action (Moonbeam, D3), 2 bonus action
    uint16_t move_ft = 0;                    // Moonbeam 60
    bool chooses_excluded = false;           // Spirit Guardians: ActionRequest::zone_excluded are unaffected
    std::vector<ZoneTriggerRow> triggers{};
    bool needs_conscious_owner = false;      // the zone does nothing while its owner is dead, at 0 HP or unconscious (feature auras, D7)
    // ---- appended (R-G11 rules PR 2) ----
    uint16_t upcast_size_ft = 0;   // + feet of size_ft per slot level above the spell's level (Fog Cloud 20); ActiveZone::size_ft
    // Walls (Wall of Fire): shape Line, length_ft x height_ft, 1 tile thick; the start tile is ActionRequest::zone_x / zone_y,
    // the end tile zone_x2 / zone_y2 (has_zone_end). ring_size_ft > 0: the caster may make a ring this many feet across
    // instead (ActionRequest::zone_ring). sided: the caster picks the damaging side at the cast (ActionRequest::zone_side,
    // +1 / -1: the sign of the cross product for a line, +1 inside / -1 outside for a ring, ZONES_AURAS.md §3 Q4), side_ft deep.
    uint16_t ring_size_ft = 0;
    bool sided = false;
    uint16_t side_ft = 0;
    // D7 passive auras (fields only; the Paladin auras' content stays deferred). For creatures in the zone, and for its owner
    // (always inside its own creature aura), only the owner's team when aura_allies_only, and not while needs_conscious_owner
    // suppresses it:
    //   aura_modifier: stat Save only (others are ignored for now), id != 0 = present: a flat bonus to their saving throws
    //   (scope: has_ability / ability), `bonus`, or max(1, the owner's aura_bonus_ability modifier) when
    //   has_aura_bonus_ability (Aura of Protection: Cha). The same spell's auras don't stack (the highest counts).
    //   aura_immunity: bitmask over Condition they can't be given (Aura of Courage: frightened).
    ModifierSpec aura_modifier{};
    bool has_aura_bonus_ability = false;
    Ability aura_bonus_ability = Ability::Cha;
    uint32_t aura_immunity = 0;
    bool aura_allies_only = false;
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
    SummonSpec summon{};   // appended (R-G12 rules PR 2): kind Summon (primary effect only)
    SummonWeaponSpec summon_weapon{};   // appended (R-G12 rules PR 3): kind SummonWeapon (primary effect only)
    WeaponSmiteSpec weapon_smite{};     // appended (7.2zh): kind WeaponSmite (primary effect only)
    // appended (7.2zh Heat Metal): sustained_damage may carry the held-object rider. The object is a worn or held
    // weapon, armor, or shield. Absent material falls back to the category rule (any weapon, armor, or shield counts
    // as metal); only an explicit wood, leather, hide, cloth, or rope removes it as a target. See VERSION_BUMP_BATCH 7.2zh.
    bool held_object = false;
    // appended (priced materials, CONTENT_CONVENTIONS §7): components.m.item, the equipment content id that provides a priced
    // material ("" = none: a priced material without an item is unsatisfiable). rules5e never reads it (sim's components hook does).
    std::string material_item;
    // appended: see SpellEffect::condition_untracked (primary effect).
    bool condition_untracked = false;
    int8_t escape_dc = 0;   // appended (EC deferred monsters): see SpellEffect::escape_dc
    // appended (EC spider follow-up, flagged): see SpellEffect::object_*.
    int8_t object_ac = 0;
    int16_t object_hp = 0;
    uint32_t object_vulnerable = 0, object_immune = 0;
    // appended (EC spider follow-up, flagged): a non-spell ranged SpellAttack (attack_effect) with a long range: targets out
    // to long_range_ft are legal, beyond range_ft (or with a hostile within 5 ft, ActionRequest facts) at disadvantage. 0 = none.
    uint16_t long_range_ft = 0;
    // appended (EC spider follow-up, flagged): Save with damage (giant spider bite): when this effect's damage drops a living
    // target to 0 HP, it is stable and gains zero_hp_condition for zero_hp_rounds (one ActiveSpellEffect, timed, so it outlasts
    // regained HP) and zero_hp_while for as long as that effect lasts. 0 = off.
    uint16_t zero_hp_rounds = 0;
    Condition zero_hp_condition = Condition::Poisoned, zero_hp_while = Condition::Paralyzed;
    // appended (#138): EndCondition's `ends_conditions` in spell@1 JSON list order; ActionRequest::condition_choice is 1 + an
    // index into it (stable across engine versions, unlike Condition enum order). Filled by the loader; empty = mask order.
    std::vector<Condition> ends_condition_list;
    // appended (R-G12, CONTENT_CONVENTIONS §9): kind Polymorph. polymorph_forms = spell@1 `forms[]` (beast monster content ids;
    // ActionRequest::form_archetype = 1 + index); polymorph_max_cr8 = fixed `max_cr` as CR*8 (0xFFFF = target_level, the
    // default: sim's build_form checks it at cast); polymorph_save = the unwilling target's save (default Wis).
    std::vector<std::string> polymorph_forms;
    uint16_t polymorph_max_cr8 = 0xFFFF;
    Ability polymorph_save = Ability::Wis;
    // appended (cover, 7.2t): the target gains no benefit from half / three-quarters cover (Sacred Flame). Total cover still
    // refuses targeting.
    bool ignores_cover = false;
    // appended (control conditions, 7.2v): kind Save. control = ControlKind of the effect a failed save leaves (Turned and
    // Command carry no SRD condition: the effect has no_condition). control_excluded: creature types the spell has no effect
    // on (no save; Command: undead). control_min_int: a target with Intelligence at most this is unaffected (Hideous Laughter
    // 4; 0 = none). destroy_cr8_max: Destroy Undead, an undead with Combatant::cr8 at most this (CR * 8; CR 1/2 = 4) that
    // fails the save is destroyed instead (0 = off; the cleric 5 hook, wired by T&C).
    ControlKind control = ControlKind::None;
    uint32_t control_excluded = 0;
    uint8_t control_min_int = 0;
    uint16_t destroy_cr8_max = 0;
    // appended (grab and contain, 7.2w). contain: the condition puts the target inside the creature using this effect,
    // 1 Engulf (gelatinous cube: restrained, escape_dc), 2 Swallow (giant toad: restrained and blinded, total cover against
    // everything outside). The contained target takes contain_dice of contain_damage_type at the start of each of the
    // container's turns, and is freed when the container dies. condition_max_size: the condition (and, with contain, the
    // whole effect: no save) skips a target larger than this Size (Net, Engulf: Large; -1 = any).
    uint8_t contain = 0;
    DiceExpr contain_dice{0, 0, 0}; DamageType contain_damage_type = DamageType::Acid;
    int8_t condition_max_size = -1;
    // 7.2w, Swallow: regurgitate_dc > 0 (behir 14, purple worm 21; the giant toad has none): when the container takes at least
    // regurgitate_damage (30) in one turn from the swallowed creature, it makes a Con save vs regurgitate_dc at the end of
    // that turn; on a failure the creature exits prone (EncounterState::contain_exits, sim picks a tile within 10 ft).
    // excludes_formless: the condition skips a Combatant::formless target (the Net).
    int8_t regurgitate_dc = 0; uint8_t regurgitate_damage = 30;
    bool excludes_formless = false;
    // appended (7.2zc Invisibility): ApplyCondition: kEndsOn* bits. The effect on a creature ends once that creature has made an
    // attack (after the attack resolves) or cast a spell (after the spell resolves), whatever the target; the spell's
    // concentration ends with it when nothing of the spell is left. 0 = never early.
    uint8_t ends_on = 0;
    // appended (7.2zc generic mark): spell@1 `move_mark {action: bonus_action}`: 1 = the caster may move the mark of this
    // concentration spell's marked rows (ModifierSpec::mark_scope) with a bonus action once the marked creature has dropped to
    // 0 hit points (ActionRequest::move_mark). 0 = the mark never moves.
    uint8_t move_mark = 0;
    // appended (7.2zd Sanctuary): ApplyCondition `warded` with spell@1 `ward {save, on_fail: retarget_or_lose}`. A creature
    // targeting the warded creature with an attack, or (on another team) with a spell without an area, makes a `ward_save`
    // saving throw against the caster's spell save DC first (ActiveSpellEffect::dc / save_ability); on a failure it must
    // choose a new target (ActionRequest::ward_alternate, one of ward_alternate_targets) or lose the attack or spell.
    bool has_ward = false; Ability ward_save = Ability::Wis; uint8_t ward_on_fail = 1;   // 1 = retarget_or_lose
    // appended (7.2zd Protection from Evil and Good): ConditionImmunityFrom masks (over Condition / CreatureType).
    uint32_t immune_conditions = 0, immune_from_types = 0;
    // appended (7.2zd Aid): MaxHpBonus amounts (integers 0..50).
    int16_t hp_bonus_flat = 0, hp_bonus_per_slot = 0;
    // appended (7.2ze Bane): ApplyModifier (primary effect) gated per target by a saving throw against the caster's spell save
    // DC; a creature that succeeds is not affected (SavingThrowRolled, c bit 3 set).
    bool has_modifier_save = false; Ability modifier_save = Ability::Cha;
    // appended (7.2zf): kind WeaponImbue / Duplicates (primary effect only).
    WeaponImbueSpec imbue{};
    DuplicatesSpec duplicates{};
    // appended (7.2zg): kind TempHp (primary effect only).
    TempHpSpec temp_hp{};
    // appended (7.2zg Acid Arrow): kind SpellAttack, `on_miss: half`: a miss deals half the initial damage (dice rolled then,
    // halved, rounded down); riders that follow a hit (delayed_damage applies_to affected) do not apply.
    bool miss_half = false;
    // appended (7.2zi Ray of Enfeeblement): kind SpellAttack, `hit_only`: the attack roll resolves and a hit is a hit, but
    // no damage dice are rolled and no hit points are dealt. Riders (applies_to affected) still follow a hit.
    bool hit_only = false;
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
