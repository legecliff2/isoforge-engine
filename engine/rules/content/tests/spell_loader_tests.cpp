#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include "rules5e_content/spell_loader.hpp"

using namespace rules5e;
using namespace rules5e_content;

namespace {
const std::filesystem::path kSpells = std::filesystem::path(ENGINE_SOURCE_ROOT) / "content/packs/srd51.core/spells";
}

TEST_CASE("sample pack spells load into a SpellBook") {
    auto r = load_spellbook(kSpells);
    REQUIRE(r.errors.empty());
    REQUIRE(r.book);
    // Expected: the spell@1 files the srd51.core manifest lists, so new spell batches need no edit.
    std::ifstream mf(kSpells.parent_path() / "manifest.json");
    const auto manifest = eng::assets::Json::parse(std::string(std::istreambuf_iterator<char>(mf), std::istreambuf_iterator<char>()));
    size_t listed = 0;
    for (const auto& c : manifest["contents"])
        listed += c.get<std::string>().rfind("spells/", 0) == 0 ? 1 : 0;
    CHECK(listed > 0);
    CHECK(r.book->all().size() == listed);

    const SpellDef* fb = r.book->find(spell_id_of("srd51.spell.fire_bolt"));
    REQUIRE(fb);
    CHECK(fb->level == 0);
    CHECK(fb->kind == SpellEffectKind::SpellAttack);
    CHECK(fb->dice == DiceExpr{1, 10, 0});
    CHECK(fb->damage_type == DamageType::Fire);
    CHECK(fb->range_ft == 120);
    CHECK(fb->components == (kComponentV | kComponentS));
    CHECK(fb->cantrip_scales);
    CHECK(scaled_dice(*fb, 0, 4).count == 1);
    CHECK(scaled_dice(*fb, 0, 5).count == 2);
    CHECK(scaled_dice(*fb, 0, 17).count == 4);

    CHECK_FALSE(fb->ignores_cover);
    const SpellDef* sfl = r.book->find(spell_id_of("srd51.spell.sacred_flame"));   // 7.2t: ignores_cover
    REQUIRE(sfl);
    CHECK(sfl->ignores_cover);

    const SpellDef* mm = r.book->find(spell_id_of("srd51.spell.magic_missile"));
    REQUIRE(mm);
    CHECK(mm->kind == SpellEffectKind::AutoHit);
    CHECK(scaled_darts(*mm, 1) == 3);
    CHECK(scaled_darts(*mm, 3) == 5);
    CHECK(mm->dice == DiceExpr{1, 4, 1});

    const SpellDef* cw = r.book->find(spell_id_of("srd51.spell.cure_wounds"));
    REQUIRE(cw);
    CHECK(cw->kind == SpellEffectKind::Heal);
    CHECK(cw->range_ft == 5);
    CHECK(cw->add_spellcasting_modifier);
    CHECK(scaled_dice(*cw, 3, 0).count == 3);

    const SpellDef* sh = r.book->find(spell_id_of("srd51.spell.shield"));
    REQUIRE(sh);
    CHECK(sh->casting_time == CastingTime::Reaction);
    CHECK(sh->reaction_trigger == ReactionTrigger::BeingHit);
    CHECK(sh->ac_bonus == 5);
    CHECK(sh->blocks_spell == mm->id);

    const SpellDef* hp = r.book->find(spell_id_of("srd51.spell.hold_person"));
    REQUIRE(hp);
    CHECK(hp->concentration);
    CHECK(hp->save_ability == Ability::Wis);
    CHECK(hp->applies_condition);
    CHECK(hp->condition == Condition::Paralyzed);
    CHECK(hp->repeat_save == RepeatSave::EndOfTurn);
    CHECK((hp->components & kComponentM) != 0);
    CHECK(scaled_targets(*hp, 4) == 3);

    const SpellDef* cs = r.book->find(spell_id_of("srd51.spell.counterspell"));
    REQUIRE(cs);
    CHECK(cs->kind == SpellEffectKind::Counter);
    CHECK(cs->reaction_trigger == ReactionTrigger::SpellCast);
    CHECK(cs->counter_dc_base == 10);
    CHECK(cs->components == kComponentS);
    CHECK(cs->requires_sight);
    CHECK(sh->or_targeted_by == std::vector<uint32_t>{mm->id});
    CHECK(sh->duration == SpellDuration::UntilStartOfCasterTurn);
    CHECK(mm->distribution == DartDistribution::Any);
    CHECK(mm->simultaneous);
    CHECK(mm->targets == 0);
    CHECK(hp->target_types == bit(CreatureType::Humanoid));
    CHECK(hp->max_spread_ft == 30);
    CHECK(hp->duration == SpellDuration::Minutes);
    CHECK(hp->duration_amount == 1);
    CHECK(cw->excluded_types == (bit(CreatureType::Undead) | bit(CreatureType::Construct)));
}

TEST_CASE("loader rejects every validator negative case (tests/validate/cases)") {
    // Same rules as tools/validate: each bad doc, loaded alone, must fail.
    const auto cases = std::filesystem::path(ENGINE_SOURCE_ROOT) / "tests/validate/cases";
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(cases))
        if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    REQUIRE(files.size() >= 10);
    const auto dir = std::filesystem::temp_directory_path() / "rules5e_spell_negative";
    for (const auto& f : files) {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        std::filesystem::copy_file(f, dir / f.filename());
        // R-G12: form ids resolve across the pack set (monster data): validator checks, not the spell loader's (alone, well formed).
        std::ifstream pre(std::filesystem::path(f).replace_extension(".expect"));
        std::string pre_expect;
        std::getline(pre, pre_expect);
        if (pre_expect.find("polymorph: unknown form") == 0 || pre_expect.find("polymorph: form '") == 0) continue;
        auto r = load_spellbook(dir);
        INFO(f.filename().string());
        CHECK_FALSE(r.book);
        CHECK_FALSE(r.errors.empty());
        // Cross-field rules use the validator's wording.
        std::ifstream in(std::filesystem::path(f).replace_extension(".expect"));
        std::string expect;
        std::getline(in, expect);
        const bool cross_field = expect.find("scaling.") == 0 || expect.find("effect") == 0 || expect.find("targets not") == 0 ||
                                 expect.find("ac_bonus") == 0 || expect.find("casting_time") == 0 || expect.find("disagree") == 0 ||
                                 expect.find("is not a spell@1") == 0 || expect.find("'") == 0 || expect.find("until_start") == 0 ||
                                 expect.find("zone") == 0 ||   // R-G11: the loader parses zones (CONTENT_CONVENTIONS.md section 4)
                                 expect.find("polymorph") == 0 ||   // R-G12 (section 9)
                                 expect.find("move_mark") == 0;   // 7.2zc
        const std::string first_error = r.errors.empty() ? std::string() : r.errors[0];
        INFO(first_error);
        if (cross_field && !r.errors.empty()) CHECK(r.errors[0].find(expect) != std::string::npos);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("loader: R6 rules match the validator") {
    auto fb = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.x","level":0,"name":"x",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":30},"components":{"v":true},
        "duration":{"kind":"instantaneous"},
        "effects":[{"kind":"spell_attack","attack":"ranged","damage":{"dice":"1d6","type":"fire"}}]})");
    REQUIRE(spell_from_json(fb).def);
    auto bad = [](eng::assets::Json j, const char* want) {
        auto r = spell_from_json(j);
        INFO(want);
        CHECK_FALSE(r.def);
        CHECK(r.error.find(want) != std::string::npos);
    };
    auto j = fb; j["effects"][0]["attack"] = "touch"; bad(j, "'touch' is not one of");
    j = fb; j["level"] = 1; j["scaling"] = {{"upcast", {{"darts", 1}}}}; bad(j, "scaling.upcast.darts not allowed on a spell_attack");
    j = fb; j["level"] = 1; j["scaling"] = {{"upcast", {{"targets", 1}}}}; bad(j, "scaling.upcast.targets not allowed on a spell_attack");
    j = fb; j["range"]["feet"] = 0; bad(j, "range invalid");
    j = fb; j["scaling"] = {{"cantrip", {{"levels", {11, 5}}, {"adds", "dice"}}}}; bad(j, "ascending");
    j = fb; j["effects"].push_back({{"kind", "apply_condition"}}); bad(j, "effects[1]: effect condition invalid");
    j = fb; j["effects"][0]["applies_to"] = "affected"; bad(j, "effects[0].applies_to needs a previous effect");
    j = fb; j["effects"] = eng::assets::Json::array({{{"kind", "ac_bonus"}, {"bonus", 5}}, fb["effects"][0]});
    j["casting_time"] = {{"kind", "reaction"}, {"trigger", {{"kind", "being_hit"}}}}; j["duration"] = {{"kind", "until_start_of_caster_turn"}};
    bad(j, "cannot be combined with other effects");
    auto hp = fb; hp["level"] = 2;
    hp["effects"] = eng::assets::Json::array({{{"kind", "save"}, {"ability", "wis"}, {"condition", "paralyzed"}}});
    REQUIRE(spell_from_json(hp).def);
    j = hp; j["scaling"] = {{"upcast", {{"dice", 1}}}}; bad(j, "scaling.upcast.dice needs effect dice or damage");
    j = hp; j["effects"][0]["on_success"] = "half"; bad(j, "on_success needs damage");
    j = hp; j["effects"][0].erase("condition"); j["effects"][0]["repeat_save"] = "end_of_turn";
    j["effects"][0]["damage"] = {{"dice", "1d6"}, {"type", "fire"}}; j["effects"][0]["on_success"] = "none";
    bad(j, "repeat_save needs condition");
    j = hp; j["effects"][0]["damage"] = {{"dice", "1d6"}, {"type", "fire"}}; bad(j, "'on_success' is a dependency of 'damage'");
    {   // consumed without cost_cp loads (spell@1 no longer requires cost_cp) and has no effect: no price, nothing to consume
        j = hp; j["components"]["m"] = {{"text", "holy water"}, {"consumed", true}};
        auto r = spell_from_json(j); INFO(r.error); REQUIRE(r.def);
        CHECK(r.def->material_consumed);
        CHECK(r.def->material_cost_cp == 0);
        CHECK(r.def->material_item.empty());
        const bool consumes = r.def->material_cost_cp > 0 && r.def->material_consumed;   // combat.cpp's consume condition: never true
        CHECK_FALSE(consumes);
    }
    j = fb; j["casting_time"] = {{"kind", "reaction"}, {"trigger", {{"kind", "being_hit"}}}}; bad(j, "disagree");
}

TEST_CASE("runtime spell id collision is a hard load error") {
    // FNV-1a 32 collision (found by search); both hash to 0xce11440b.
    static_assert(spell_id_of("srd51.spell.x562789") == spell_id_of("srd51.spell.x779192"));
    static_assert(spell_id_of("srd51.spell.x562789") == 0xce11440bu);
    SpellBook book;
    SpellDef a; a.content_id = "srd51.spell.x562789"; a.id = spell_id_of(a.content_id);
    SpellDef b; b.content_id = "srd51.spell.x779192"; b.id = spell_id_of(b.content_id);
    std::string e;
    CHECK(add_spell(book, a, e));
    CHECK_FALSE(add_spell(book, b, e));
    CHECK(e.find("collision") != std::string::npos);
    CHECK(e.find("0xce11440b") != std::string::npos);
    CHECK_FALSE(add_spell(book, a, e));
    CHECK(e.find("duplicate") != std::string::npos);

    // Through load_spellbook: the whole load fails (no book).
    auto dir = std::filesystem::temp_directory_path() / "rules5e_spell_collision";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    for (const char* id : {"srd51.spell.x562789", "srd51.spell.x779192"}) {
        std::ofstream(dir / (std::string(id) + ".json")) << R"({"$schema":"spell@1","id":")" << id
            << R"(","name":"X","source":{"ref":"t","license":"t"},"level":0,"school":"evocation",
               "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":30},"components":{"v":true},
               "duration":{"kind":"instantaneous"},
               "effects":[{"kind":"spell_attack","attack":"ranged","damage":{"dice":"1d6","type":"fire"}}]})";
    }
    auto r = load_spellbook(dir);
    CHECK_FALSE(r.book);
    REQUIRE(r.errors.size() == 1);
    CHECK(r.errors[0].find("collision") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("loader: several effects, a no-save apply_condition effect, riders") {
    auto doc = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.laugh","level":1,"name":"x",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":30},"components":{"v":true},
        "duration":{"kind":"minutes","amount":1,"concentration":true},
        "effects":[{"kind":"save","ability":"wis","condition":"prone","repeat_save":"on_damage"},
                   {"kind":"apply_condition","condition":"incapacitated","applies_to":"affected"},
                   {"kind":"save","ability":"con","damage":{"dice":"2d6","type":"thunder"},"on_success":"half"}]})");
    auto r = spell_from_json(doc);
    REQUIRE(r.def);
    const SpellDef& d = *r.def;
    CHECK(d.kind == SpellEffectKind::Save);
    CHECK(d.condition == Condition::Prone);
    REQUIRE(d.extra_effects.size() == 2);
    CHECK(d.extra_effects[0].kind == SpellEffectKind::ApplyCondition);
    CHECK(d.extra_effects[0].condition == Condition::Incapacitated);
    CHECK(d.extra_effects[0].affected_only);
    CHECK(d.extra_effects[1].kind == SpellEffectKind::Save);
    CHECK(d.extra_effects[1].save_ability == Ability::Con);
    CHECK(d.extra_effects[1].dice == DiceExpr{2, 6, 0});
    CHECK(d.extra_effects[1].on_save == OnSave::Half);
    CHECK_FALSE(d.extra_effects[1].affected_only);

    // apply_condition alone (Invisibility): targets and upcast targets are allowed
    auto inv = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.inv","level":2,"name":"x",
        "casting_time":{"kind":"action"},"range":{"kind":"touch"},"components":{"v":true},
        "duration":{"kind":"hours","amount":1,"concentration":true},"targets":{"count":1},
        "scaling":{"upcast":{"targets":1}},
        "effects":[{"kind":"apply_condition","condition":"invisible"}]})");
    auto ri = spell_from_json(inv);
    REQUIRE(ri.def);
    CHECK(ri.def->kind == SpellEffectKind::ApplyCondition);
    CHECK(ri.def->extra_effects.empty());
    CHECK(scaled_targets(*ri.def, 3) == 2);
    auto bad_c = inv; bad_c["effects"][0]["condition"] = "wet";
    CHECK_FALSE(spell_from_json(bad_c).def);
}

TEST_CASE("loader: R-G11 zone fixtures (tests/validate/fixtures/zones) map onto ZoneSpec") {
    const auto dir = std::filesystem::path(ENGINE_SOURCE_ROOT) / "tests/validate/fixtures/zones/spells";
    auto r = load_spellbook(dir);
    INFO((r.errors.empty() ? std::string() : r.errors[0]));
    REQUIRE(r.errors.empty());
    REQUIRE(r.book);
    CHECK(r.book->all().size() == 9);   // spells/ including Entangle (spells_pr2 stays a separate book)
    for (const SpellDef& d : r.book->all()) { CHECK(d.kind == SpellEffectKind::Zone); CHECK(d.extra_effects.empty()); }

    const SpellDef* mb = r.book->find(spell_id_of("test.zone.spell.moonbeam"));
    REQUIRE(mb);
    CHECK(mb->zone.shape == AreaShape::Cylinder); CHECK(mb->zone.size_ft == 5); CHECK(mb->zone.height_ft == 40);
    CHECK(mb->zone.anchor == ZoneAnchor::Point);
    CHECK(mb->zone.move_cost == 1); CHECK(mb->zone.move_ft == 60);
    CHECK(mb->upcast_dice == 1);
    CHECK(mb->expiry == 1); CHECK(mb->duration_rounds == 10);   // 1 minute: the zone's timer
    REQUIRE(mb->zone.triggers.size() == 2);
    CHECK(mb->zone.triggers[0].on == ZoneTrigger::Enter); CHECK(mb->zone.triggers[1].on == ZoneTrigger::StartTurn);
    CHECK(mb->zone.triggers[0].group == 1);
    const SpellEffect& e = mb->zone.triggers[0].effect;
    CHECK(e.kind == SpellEffectKind::Save); CHECK(e.save_ability == Ability::Con); CHECK(e.on_save == OnSave::Half);
    CHECK(e.dice == DiceExpr{2, 10, 0}); CHECK(e.damage_type == DamageType::Radiant); CHECK_FALSE(e.applies_condition);

    const SpellDef* sg = r.book->find(spell_id_of("test.zone.spell.spirit_guardians"));
    REQUIRE(sg);
    CHECK(sg->zone.anchor == ZoneAnchor::Creature); CHECK(sg->range_ft == 0);
    CHECK(sg->zone.flags == kZoneHalveSpeed); CHECK(sg->zone.chooses_excluded);
    CHECK(sg->zone.needs_conscious_owner);   // D7 field
    CHECK(sg->concentration); CHECK(sg->duration_rounds == 100);

    const SpellDef* si = r.book->find(spell_id_of("test.zone.spell.silence"));
    REQUIRE(si);
    CHECK(si->zone.flags == kZoneSilence); CHECK(si->zone.triggers.empty()); CHECK_FALSE(si->zone.needs_conscious_owner);

    const SpellDef* sp = r.book->find(spell_id_of("test.zone.spell.spike_growth"));
    REQUIRE(sp);
    REQUIRE(sp->zone.triggers.size() == 1);
    CHECK(sp->zone.triggers[0].on == ZoneTrigger::MoveWithin); CHECK(sp->zone.triggers[0].group == 0);
    CHECK(sp->zone.triggers[0].effect.kind == SpellEffectKind::ApplyCondition);   // damage, no save
    CHECK_FALSE(sp->zone.triggers[0].effect.applies_condition);
    CHECK((sp->zone.flags & kZoneDifficult) != 0);

    const SpellDef* web = r.book->find(spell_id_of("test.zone.spell.web"));
    REQUIRE(web);
    bool own = false, held = false;
    for (const auto& t : web->zone.triggers) {
        own = own || t.own_turn_only;
        held = held || (t.while_in_zone && t.effect.applies_condition && t.effect.condition == Condition::Restrained);
    }
    CHECK(own); CHECK(held);

    const SpellDef* gr = r.book->find(spell_id_of("test.zone.spell.grease"));
    REQUIRE(gr);
    CHECK_FALSE(gr->concentration); CHECK(gr->expiry == 1);
    CHECK(gr->zone.triggers.front().on == ZoneTrigger::Appear);

    // Entangle: SRD 20-ft square as a cube, Strength save or restrained, Web's while_in_zone escape.
    const SpellDef* en = r.book->find(spell_id_of("test.zone.spell.entangle"));
    REQUIRE(en);
    CHECK(en->level == 1); CHECK(en->concentration); CHECK(en->range_ft == 90);
    CHECK(en->expiry == 1); CHECK(en->duration_rounds == 10);   // 1 minute
    CHECK(en->zone.shape == AreaShape::Cube); CHECK(en->zone.size_ft == 20);
    CHECK(en->zone.anchor == ZoneAnchor::Point); CHECK(en->zone.flags == kZoneDifficult);
    REQUIRE(en->zone.triggers.size() == 1);
    const ZoneTriggerRow& er = en->zone.triggers[0];
    CHECK(er.on == ZoneTrigger::Appear); CHECK(er.while_in_zone); CHECK_FALSE(er.own_turn_only);
    CHECK(er.effect.kind == SpellEffectKind::Save); CHECK(er.effect.save_ability == Ability::Str);
    CHECK(er.effect.applies_condition); CHECK(er.effect.condition == Condition::Restrained);
}

TEST_CASE("loader: R-G11 zone rules match the validator") {
    auto zj = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.zz","level":2,"name":"x",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":120},"components":{"v":true},
        "duration":{"kind":"minutes","amount":1,"concentration":true},
        "effects":[{"kind":"zone","zone":{"shape":"sphere","size_ft":20,"anchor":"point","flags":["difficult"],
            "triggers":[{"on":"zone_enter","group":0,"effect":{"kind":"damage","damage":{"dice":"2d4","type":"piercing"}}}]}}]})");
    auto ok = spell_from_json(zj);
    INFO(ok.error);
    REQUIRE(ok.def);
    CHECK(ok.def->zone.triggers[0].group == 0);
    CHECK_FALSE(ok.def->zone.needs_conscious_owner);
    auto bad = [](eng::assets::Json j, const char* want) {
        auto r = spell_from_json(j);
        INFO(want);
        INFO(r.error);
        CHECK_FALSE(r.def);
        CHECK(r.error.find(want) != std::string::npos);
    };
    using J = eng::assets::Json;
    auto j = zj; j["area"] = {{"shape", "sphere"}, {"size_ft", 20}}; bad(j, "zone: area not allowed");
    j = zj; j["duration"] = {{"kind", "instantaneous"}}; bad(j, "zone: needs a lasting duration");
    j = zj; j["effects"][0]["zone"]["anchor"] = "creature"; bad(j, "zone: anchor creature (an aura on the caster) needs range self");
    j = zj; j["range"] = {{"kind", "self"}}; j["effects"][0]["zone"]["anchor"] = "creature";
    j["effects"][0]["zone"]["move"] = {{"cost", "action"}, {"feet", 60}}; bad(j, "zone: move needs anchor point");
    j = zj; j["effects"][0]["zone"]["flags"] = J::array({"light_obscured", "heavy_obscured"}); bad(j, "are exclusive");
    j = zj; j["effects"][0]["zone"].erase("flags"); j["effects"][0]["zone"].erase("triggers"); bad(j, "zone: needs triggers or flags");
    j = zj; j["effects"][0]["zone"]["triggers"][0]["effect"] = {{"kind", "heal"}}; bad(j, "is not a zone trigger effect");
    j = zj; j["effects"][0]["zone"]["triggers"][0]["on"] = "zone_start_turn"; j["effects"][0]["zone"]["triggers"][0]["own_turn_only"] = true;
    bad(j, "zone.triggers[0]: own_turn_only is only for zone_enter / zone_move_within");
    j = zj; j["effects"][0]["zone"]["triggers"][0]["on"] = "zone_end_turn"; j["effects"][0]["zone"]["triggers"][0]["counts_aura_move_as_enter"] = true;
    bad(j, "counts_aura_move_as_enter is only for zone_enter");
    j = zj; j["effects"][0]["zone"]["triggers"][0]["counts_aura_move_as_enter"] = true; bad(j, "on a zone that never moves");
    j = zj; j["effects"][0]["zone"]["triggers"][0]["while_in_zone"] = true; bad(j, "while_in_zone needs an effect condition");
    j = zj; j["effects"].push_back(zj["effects"][0]); bad(j, "zone must be the primary effect (effects[1] is a zone)");
    j = zj; j["effects"][0]["zone"]["shape"] = "cone"; bad(j, "zone.shape must be sphere, cylinder, cube or line");
    j = zj; j["effects"][0]["zone"]["size_ft"] = 7; bad(j, "zone.size_ft");
    j = zj; j["effects"][0]["zone"]["height_ft"] = 40; bad(j, "height_ft is required for (and only for) cylinders");
    j = zj; j["effects"][0]["zone"]["shape"] = "cylinder"; bad(j, "height_ft is required for (and only for) cylinders");
    j = zj; j["effects"][0]["zone"]["flags"] = 4; bad(j, "zone.flags");
    j = zj; j["effects"][0]["zone"]["flags"] = J::array({"difficult", "difficult"}); bad(j, "duplicate flag");
    j = zj; j["effects"][0]["zone"]["needs_conscious_owner"] = 1; bad(j, "needs_conscious_owner must be a boolean");
    j = zj; j["effects"][0]["zone"]["move"] = {{"cost", "reaction"}, {"feet", 60}}; bad(j, "zone.move.cost");
    j = zj; j["effects"][0]["zone"]["triggers"][0]["group"] = 256; bad(j, "group invalid");
    j = zj; j["effects"][0]["zone"]["triggers"][0]["on"] = "enter"; bad(j, ".on must be");
    j = zj; j["effects"][0]["zone"]["upcast_size"] = 20; bad(j, "zone: unknown key 'upcast_size'");
    j = zj; j["effects"][0]["zone"]["triggers"][0]["regions"] = "zone"; bad(j, "zone.triggers[0]: unknown key 'regions'");
    j = zj; j["targets"] = {{"count", 1}}; bad(j, "targets not allowed on a zone effect");
    j = zj; j["scaling"] = {{"upcast", {{"dice", 1}}}};
    REQUIRE(spell_from_json(j).def);
    CHECK(spell_from_json(j).def->upcast_dice == 1);
    j = zj; j["effects"][0]["zone"]["triggers"][0]["effect"] = {{"kind", "apply_condition"}, {"condition", "prone"}};
    j["scaling"] = {{"upcast", {{"dice", 1}}}}; bad(j, "scaling.upcast.dice needs effect dice or damage");
    j = zj; j["effects"][0]["zone"]["needs_conscious_owner"] = true;
    auto nc = spell_from_json(j);
    REQUIRE(nc.def);
    CHECK(nc.def->zone.needs_conscious_owner);
    j = zj; j["effects"][0]["zone"]["triggers"][0]["effect"] = {{"kind", "save"}, {"ability", "dex"}, {"condition", "restrained"}};
    j["effects"][0]["zone"]["triggers"][0]["while_in_zone"] = true;
    auto w = spell_from_json(j);
    REQUIRE(w.def);
    CHECK(w.def->zone.triggers[0].effect.kind == SpellEffectKind::Save);
    CHECK(w.def->zone.triggers[0].effect.applies_condition);
    CHECK(w.def->zone.triggers[0].while_in_zone);
}

TEST_CASE("loader: R-G11 rules PR 2 zone / trigger keys (CONTENT_CONVENTIONS.md 4.6)") {
    using J = eng::assets::Json;
    const J base = J::parse(R"({"$schema":"spell@1","id":"srd51.spell.zk","level":2,"name":"x",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":120},"components":{"v":true},
        "duration":{"kind":"minutes","amount":1,"concentration":true},
        "effects":[{"kind":"zone","zone":{"shape":"sphere","size_ft":20,"anchor":"point","flags":["heavy_obscured"]}}]})");
    auto bad = [](const J& j, const char* want) {
        auto r = spell_from_json(j);
        INFO(want);
        INFO(r.error);
        CHECK_FALSE(r.def);
        CHECK(r.error.find(want) != std::string::npos);
    };
    // Fog Cloud: upcast_size_ft
    J fog = base; fog["effects"][0]["zone"]["upcast_size_ft"] = 20;
    auto rf = spell_from_json(fog);
    INFO(rf.error);
    REQUIRE(rf.def);
    CHECK(rf.def->zone.upcast_size_ft == 20);
    J j = base; j["effects"][0]["zone"]["upcast_size_ft"] = 21; bad(j, "zone.upcast_size_ft invalid");

    // Wall of Fire: line + length_ft, ring_size_ft, sided, side_ft, region
    J wall = base;
    wall["effects"][0]["zone"] = J::parse(R"({"shape":"line","length_ft":60,"height_ft":20,"ring_size_ft":20,"sided":true,
        "side_ft":10,"anchor":"point","triggers":[
          {"on":"zone_appear","region":"zone_or_side","effect":{"kind":"save","ability":"dex","damage":{"dice":"5d8","type":"fire"},"on_success":"half"}},
          {"on":"zone_end_turn","region":"zone_or_side","effect":{"kind":"damage","damage":{"dice":"5d8","type":"fire"}}},
          {"on":"zone_enter","region":"zone","effect":{"kind":"damage","damage":{"dice":"5d8","type":"fire"}}}]})");
    auto rw = spell_from_json(wall);
    INFO(rw.error);
    REQUIRE(rw.def);
    const ZoneSpec& wz = rw.def->zone;
    CHECK(wz.shape == AreaShape::Line); CHECK(wz.length_ft == 60); CHECK(wz.height_ft == 20); CHECK(wz.size_ft == 0);
    CHECK(wz.ring_size_ft == 20); CHECK(wz.sided); CHECK(wz.side_ft == 10);
    REQUIRE(wz.triggers.size() == 3);
    CHECK(wz.triggers[0].region == 1); CHECK(wz.triggers[1].region == 1); CHECK(wz.triggers[2].region == 0);
    j = wall; j["effects"][0]["zone"]["size_ft"] = 20; bad(j, "zone.size_ft not allowed on a line");
    j = wall; j["effects"][0]["zone"].erase("length_ft"); bad(j, "zone.length_ft is required for (and only for) lines");
    j = base; j["effects"][0]["zone"]["length_ft"] = 60; bad(j, "zone.length_ft is required for (and only for) lines");
    j = wall; j["effects"][0]["zone"].erase("height_ft"); bad(j, "zone.height_ft is required for (and only for) cylinders and lines");
    j = wall; j["effects"][0]["zone"]["upcast_size_ft"] = 20; bad(j, "zone: upcast_size_ft not allowed on a line");
    j = base; j["effects"][0]["zone"]["ring_size_ft"] = 20; bad(j, "zone: ring_size_ft needs shape line");
    j = base; j["effects"][0]["zone"]["sided"] = true; j["effects"][0]["zone"]["side_ft"] = 10; bad(j, "zone: sided needs shape line");
    j = wall; j["effects"][0]["zone"]["sided"] = "yes"; bad(j, "zone.sided must be a boolean");
    j = wall; j["effects"][0]["zone"].erase("side_ft"); bad(j, "zone: side_ft is required for (and only for) sided zones");
    j = wall; j["effects"][0]["zone"]["side_ft"] = 3; bad(j, "zone.side_ft invalid");
    j = wall; j["effects"][0]["zone"]["triggers"][0]["region"] = "side"; bad(j, "zone.triggers[0].region must be zone or zone_or_side");
    j = wall; j["effects"][0]["zone"]["triggers"][2]["region"] = "zone_or_side";
    bad(j, "zone.triggers[2]: region zone_or_side is only for zone_appear / zone_start_turn / zone_end_turn");
    j = wall; j["effects"][0]["zone"]["sided"] = false; j["effects"][0]["zone"].erase("side_ft");
    bad(j, "zone.triggers[0]: region zone_or_side needs a sided zone");

    // Stinking Cloud: loses_action, auto_success_*
    J sc = base;
    sc["effects"][0]["zone"]["triggers"] = J::parse(R"([{"on":"zone_start_turn","loses_action":true,
        "auto_success_damage":["poison"],"auto_success_conditions":["poisoned"],"auto_success_no_breath":true,
        "effect":{"kind":"save","ability":"con","condition":"poisoned"}}])");
    auto rs = spell_from_json(sc);
    INFO(rs.error);
    REQUIRE(rs.def);
    const ZoneTriggerRow& sr = rs.def->zone.triggers[0];
    CHECK(sr.loses_action); CHECK(sr.auto_success_no_breath);
    CHECK(sr.auto_success_damage == bit(DamageType::Poison)); CHECK(sr.auto_success_conditions == bit(Condition::Poisoned));
    j = sc; j["effects"][0]["zone"]["triggers"][0]["loses_action"] = 1; bad(j, "zone.triggers[0]: flags must be booleans");
    j = sc; j["effects"][0]["zone"]["triggers"][0]["auto_success_damage"] = J::array({"poison", "poison"});
    bad(j, "zone.triggers[0].auto_success_damage must be a non-empty array of unique damage types");
    j = sc; j["effects"][0]["zone"]["triggers"][0]["auto_success_conditions"] = J::array();
    bad(j, "zone.triggers[0].auto_success_conditions must be a non-empty array of unique conditions");
    j = sc; j["effects"][0]["zone"]["triggers"][0]["effect"] = {{"kind", "apply_condition"}, {"condition", "poisoned"}};
    bad(j, "zone.triggers[0]: auto_success / shapechanger_disadvantage need effect kind save");

    // Moonbeam / Spirit Guardians: shapechanger_disadvantage, evil_damage_type
    J mb = base;
    mb["effects"][0]["zone"]["triggers"] = J::parse(R"([{"on":"zone_enter","shapechanger_disadvantage":true,"evil_damage_type":"necrotic",
        "effect":{"kind":"save","ability":"con","damage":{"dice":"2d10","type":"radiant"},"on_success":"half"}}])");
    auto rm = spell_from_json(mb);
    INFO(rm.error);
    REQUIRE(rm.def);
    CHECK(rm.def->zone.triggers[0].shapechanger_disadvantage);
    CHECK(rm.def->zone.triggers[0].has_evil_damage_type); CHECK(rm.def->zone.triggers[0].evil_damage_type == DamageType::Necrotic);
    j = mb; j["effects"][0]["zone"]["triggers"][0]["evil_damage_type"] = "unholy"; bad(j, "zone.triggers[0].evil_damage_type invalid");
    j = mb; j["effects"][0]["zone"]["triggers"][0]["shapechanger_disadvantage"] = false;
    j["effects"][0]["zone"]["triggers"][0]["effect"] = {{"kind", "save"}, {"ability", "con"}, {"condition", "blinded"}};
    bad(j, "zone.triggers[0]: evil_damage_type needs effect damage");

    // D7 auras: aura_modifier, aura_bonus_ability, aura_immunity, aura_allies_only (no flags / triggers needed)
    J au = base; au["range"] = {{"kind", "self"}};
    au["effects"][0]["zone"] = J::parse(R"({"shape":"sphere","size_ft":10,"anchor":"creature","needs_conscious_owner":true,
        "aura_modifier":{"stat":"save","bonus":0,"ability":"wis"},"aura_bonus_ability":"cha","aura_immunity":["frightened"],"aura_allies_only":true})");
    auto ra = spell_from_json(au);
    INFO(ra.error);
    REQUIRE(ra.def);
    const ZoneSpec& az = ra.def->zone;
    CHECK(az.aura_modifier.stat == ModStat::Save); CHECK(az.aura_modifier.has_ability); CHECK(az.aura_modifier.ability == Ability::Wis); CHECK(az.aura_modifier.id == spell_id_of("srd51.spell.zk#aura"));
    CHECK(az.has_aura_bonus_ability); CHECK(az.aura_bonus_ability == Ability::Cha);
    CHECK(az.aura_immunity == bit(Condition::Frightened)); CHECK(az.aura_allies_only);
    j = au; j["effects"][0]["zone"]["aura_modifier"] = {{"stat", "ac"}, {"bonus", 1}}; bad(j, "zone.aura_modifier stat must be save");
    j = au; j["effects"][0]["zone"]["aura_modifier"] = {{"stat", "save"}, {"duration", 1}}; bad(j, "zone.aura_modifier must be {stat, bonus, ability}");
    j = au; j["effects"][0]["zone"]["aura_modifier"] = {{"stat", "save"}, {"bonus", 99}}; bad(j, "zone.aura_modifier bonus invalid (-30..30)");
    j = au; j["effects"][0]["zone"]["aura_modifier"] = {{"stat", "save"}, {"ability", "luck"}}; bad(j, "zone.aura_modifier ability invalid");
    j = au; j["effects"][0]["zone"]["aura_bonus_ability"] = "luck"; bad(j, "zone.aura_bonus_ability invalid");
    j = au; j["effects"][0]["zone"].erase("aura_modifier"); bad(j, "zone: aura_bonus_ability needs aura_modifier");
    j = au; j["effects"][0]["zone"]["aura_immunity"] = J::array({"scared"});
    bad(j, "zone.aura_immunity must be a non-empty array of unique conditions");
    j = au; j["effects"][0]["zone"]["aura_allies_only"] = "yes"; bad(j, "zone.aura_allies_only must be a boolean");
    j = base; j["effects"][0]["zone"]["aura_allies_only"] = true; bad(j, "zone: aura fields need anchor creature");

    // Silence needs no new key: the flag alone
    j = base; j["effects"][0]["zone"]["flags"] = J::array({"silence"});
    REQUIRE(spell_from_json(j).def);
    // Refusal: unknown keys stay refused at both levels
    j = base; j["effects"][0]["zone"]["no_breath"] = true; bad(j, "zone: unknown key 'no_breath'");
    j = sc; j["effects"][0]["zone"]["triggers"][0]["evil"] = true; bad(j, "zone.triggers[0]: unknown key 'evil'");
}

static std::string tests_read_wall(const std::filesystem::path& p) {
    std::ifstream in(p);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

TEST_CASE("loader: R-G11 PR 2 fixtures (tests/validate/fixtures/zones/spells_pr2) and T&C edge cases") {
    const auto dir = std::filesystem::path(ENGINE_SOURCE_ROOT) / "tests/validate/fixtures/zones/spells_pr2";
    auto r = load_spellbook(dir);
    INFO((r.errors.empty() ? std::string() : r.errors[0]));
    REQUIRE(r.errors.empty());
    REQUIRE(r.book);
    CHECK(r.book->all().size() == 3);

    const SpellDef* sc = r.book->find(spell_id_of("test.zone.spell.stinking_cloud"));
    REQUIRE(sc);
    REQUIRE(sc->zone.triggers.size() == 1);
    const ZoneTriggerRow& scr = sc->zone.triggers[0];
    CHECK(scr.loses_action); CHECK(scr.auto_success_no_breath); CHECK(scr.completely_within);
    CHECK(scr.auto_success_damage == bit(DamageType::Poison)); CHECK(scr.auto_success_conditions == bit(Condition::Poisoned));

    const SpellDef* wf = r.book->find(spell_id_of("test.zone.spell.wall_of_fire"));
    REQUIRE(wf);
    CHECK(wf->zone.shape == AreaShape::Line); CHECK(wf->zone.length_ft == 60); CHECK(wf->zone.height_ft == 20);
    CHECK(wf->zone.ring_size_ft == 20); CHECK(wf->zone.sided); CHECK(wf->zone.side_ft == 10);
    REQUIRE(wf->zone.triggers.size() == 3);
    CHECK(wf->zone.triggers[0].region == 0); CHECK(wf->zone.triggers[1].region == 1); CHECK(wf->zone.triggers[2].region == 0);

    const SpellDef* wa = r.book->find(spell_id_of("test.zone.spell.warding_aura"));
    REQUIRE(wa);
    CHECK(wa->zone.anchor == ZoneAnchor::Creature); CHECK(wa->zone.triggers.empty());
    CHECK(wa->zone.aura_modifier.stat == ModStat::Save); CHECK(wa->zone.aura_modifier.bonus == 1);
    CHECK(wa->zone.has_aura_bonus_ability); CHECK(wa->zone.aura_bonus_ability == Ability::Cha);
    CHECK(wa->zone.aura_immunity == bit(Condition::Frightened)); CHECK(wa->zone.aura_allies_only);

    using J = eng::assets::Json;
    const J base = J::parse(R"({"$schema":"spell@1","id":"srd51.spell.ze","level":2,"name":"x",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":120},"components":{"v":true},
        "duration":{"kind":"minutes","amount":1,"concentration":true},
        "effects":[{"kind":"zone","zone":{"shape":"sphere","size_ft":20,"anchor":"point",
            "triggers":[{"on":"zone_enter","effect":{"kind":"damage","damage":{"dice":"2d4","type":"fire"}}}]}}]})");
    // (a) explicit false on the save-only keys passes on a non-save row
    J j = base;
    j["effects"][0]["zone"]["triggers"][0]["auto_success_no_breath"] = false;
    j["effects"][0]["zone"]["triggers"][0]["shapechanger_disadvantage"] = false;
    CHECK(spell_from_json(j).def);
    // (b) evil_damage_type needs a damage key on the row's effect
    j = base; j["effects"][0]["zone"]["triggers"][0]["evil_damage_type"] = "necrotic";
    CHECK(spell_from_json(j).def);
    j["effects"][0]["zone"]["triggers"][0]["effect"] = {{"kind", "apply_condition"}, {"condition", "prone"}};
    CHECK(spell_from_json(j).error.find("evil_damage_type needs effect damage") != std::string::npos);
    // (c) sided:false with side_ft is an error
    J w = J::parse(tests_read_wall(dir / "wall_of_fire.json"));
    w["effects"][0]["zone"]["sided"] = false;
    w["effects"][0]["zone"]["triggers"][1].erase("region");
    CHECK(spell_from_json(w).error.find("zone: side_ft is required for (and only for) sided zones") != std::string::npos);
    // (d) feet are 5..65530, multiples of 5
    for (int ft : {0, 3, 65535, 65540}) {
        j = base; j["effects"][0]["zone"]["size_ft"] = ft;
        INFO(ft);
        CHECK(spell_from_json(j).error.find("zone.size_ft invalid (5..65530, a multiple of 5)") != std::string::npos);
    }
    j = base; j["effects"][0]["zone"]["size_ft"] = 65530; CHECK(spell_from_json(j).def);
    j = base; j["effects"][0]["zone"]["upcast_size_ft"] = J::parse("20.0"); CHECK_FALSE(spell_from_json(j).def);   // floats are rejected
}

TEST_CASE("loader: R-G12 polymorph (spell@1, CONTENT_CONVENTIONS.md section 9)") {
    // The SRD Polymorph shape of T&C #163 (its negative cases with one target and concentration).
    auto pm = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.polymorph","name":"Polymorph","level":4,
        "school":"transmutation","source":{"ref":"SRD 5.1","license":"CC-BY-4.0"},
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":60},
        "components":{"v":true,"s":true,"m":{"text":"a caterpillar cocoon"}},
        "duration":{"kind":"hours","amount":1,"concentration":true},
        "effects":[{"kind":"polymorph","forms":["srd51.monster.wolf","srd51.monster.ape","srd51.monster.giant_eagle"]}]})");
    auto ok = spell_from_json(pm);
    REQUIRE(ok.def);
    CHECK(ok.def->kind == SpellEffectKind::Polymorph);
    CHECK(ok.def->concentration);
    CHECK(ok.def->polymorph_forms == std::vector<std::string>{"srd51.monster.wolf", "srd51.monster.ape", "srd51.monster.giant_eagle"});
    CHECK(ok.def->polymorph_max_cr8 == 0xFFFF);   // target_level
    CHECK(ok.def->polymorph_save == Ability::Wis);
    auto j = pm; j["targets"] = {{"count", 1}}; j["effects"][0]["max_cr"] = "1/2"; j["effects"][0]["save"] = {{"ability", "cha"}};
    auto ok2 = spell_from_json(j);
    REQUIRE(ok2.def);
    CHECK(ok2.def->polymorph_max_cr8 == 4); CHECK(ok2.def->polymorph_save == Ability::Cha);
    j = pm; j["effects"][0]["max_cr"] = "4"; REQUIRE(spell_from_json(j).def); CHECK(spell_from_json(j).def->polymorph_max_cr8 == 32);
    auto bad = [](eng::assets::Json x, const std::string& want) {
        auto r = spell_from_json(x);
        INFO(want);
        CHECK_FALSE(r.def);
        CHECK(r.error.find(want) != std::string::npos);
    };
    j = pm; j["targets"] = {{"count", 2}}; bad(j, "polymorph: one target");
    j = pm; j["area"] = {{"shape", "sphere"}, {"size_ft", 10}}; bad(j, "polymorph: one target");
    j = pm; j["duration"] = {{"kind", "hours"}, {"amount", 1}}; bad(j, "polymorph: needs concentration");
    j = pm; j["effects"][0]["forms"].push_back("srd51.monster.wolf"); bad(j, "polymorph: duplicate form 'srd51.monster.wolf'");
    j = pm; j["effects"] = eng::assets::Json::array({{{"kind", "save"}, {"ability", "wis"}, {"condition", "paralyzed"}}, pm["effects"][0]});
    bad(j, "polymorph: primary effect only");
    j = pm; j["effects"][0]["forms"] = eng::assets::Json::array(); bad(j, "polymorph: forms must list");
    j = pm; j["effects"][0]["max_cr"] = "1/3"; bad(j, "polymorph: max_cr");
    j = pm; j["effects"][0]["save"] = {{"ability", "luck"}}; bad(j, "polymorph: save");
    j = pm; j["effects"][0]["shape"] = 1; bad(j, "polymorph effect: unknown key 'shape'");
}

TEST_CASE("loader 7.2u: rider keys (durations 3 / 4, no_reactions / no_healing, subject_is_caster / owner_creature_type)") {
    auto j = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.chill_touch_t","level":0,"name":"x",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":120},"components":{"v":true,"s":true},
        "duration":{"kind":"rounds","amount":1},
        "effects":[{"kind":"spell_attack","attack":"ranged","damage":{"dice":"1d8","type":"necrotic"}},
          {"kind":"apply_modifier","applies_to":"affected","modifier":{"stat":"no_healing","applies_to":"target",
            "duration":{"kind":"until_start_of_caster_turn"}}},
          {"kind":"apply_modifier","applies_to":"affected","modifier":{"stat":"attack_roll","mode":"disadvantage","applies_to":"target",
            "duration":{"kind":"until_end_of_caster_next_turn"},
            "when":[{"kind":"owner_creature_type","creature_types":["undead"]},{"kind":"subject_is_caster"}]}},
          {"kind":"apply_modifier","applies_to":"affected","modifier":{"stat":"no_reactions","applies_to":"target",
            "duration":{"kind":"until_start_of_target_turn"}}}]})");
    auto r = spell_from_json(j);
    INFO(r.error);
    REQUIRE(r.def);
    REQUIRE(r.def->extra_effects.size() == 3);
    CHECK(r.def->extra_effects[0].modifier.stat == ModStat::NoHealing);
    CHECK(r.def->extra_effects[0].modifier.until_caster_turn);
    const ModifierSpec& d = r.def->extra_effects[1].modifier;
    CHECK(d.expiry == 3);
    REQUIRE(d.when.size() == 2);
    CHECK(d.when[0].kind == PredicateKind::OwnerCreatureType);
    CHECK(d.when[0].mask == bit(CreatureType::Undead));
    CHECK(d.when[1].kind == PredicateKind::SubjectIsCaster);
    CHECK(r.def->extra_effects[2].modifier.stat == ModStat::NoReactions);
    CHECK(r.def->extra_effects[2].modifier.expiry == 4);
}

TEST_CASE("loader: 7.2v control keys on a save effect (control, control_excludes_creature_types, control_min_int, destroy_undead_max_cr8)") {
    auto base = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"test.spell.command","name":"Command","level":1,
        "school":"enchantment","source":{"ref":"SRD 5.1","license":"CC-BY-4.0"},
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":60},"components":{"v":true},
        "duration":{"kind":"rounds","amount":1},
        "effects":[{"kind":"save","ability":"wis","control":"command","control_excludes_creature_types":["undead"]}]})");
    auto c = spell_from_json(base);
    REQUIRE(c.def);
    CHECK(c.def->control == ControlKind::Command);
    CHECK(c.def->control_excluded == bit(CreatureType::Undead));
    CHECK_FALSE(c.def->applies_condition);

    auto j = base; j["effects"][0] = {{"kind", "save"}, {"ability", "wis"}, {"condition", "prone"}, {"control", "laughter"},
                                      {"control_min_int", 4}, {"repeat_save", "end_of_turn_or_damage_advantage"}};
    auto l = spell_from_json(j); REQUIRE(l.def);
    CHECK(l.def->control == ControlKind::Laughter); CHECK(l.def->control_min_int == 4);
    CHECK(l.def->repeat_save == RepeatSave::EndOfTurnOrDamageAdv);
    j["effects"][0]["control"] = "fear"; j["effects"][0]["condition"] = "frightened"; j["effects"][0]["repeat_save"] = "end_of_turn_unseen";
    j["effects"][0].erase("control_min_int");
    auto f = spell_from_json(j); REQUIRE(f.def);
    CHECK(f.def->control == ControlKind::Fear); CHECK(f.def->repeat_save == RepeatSave::EndOfTurnUnseen);
    j = base; j["effects"][0] = {{"kind", "save"}, {"ability", "wis"}, {"control", "turned"}, {"destroy_undead_max_cr8", 8}};
    auto t = spell_from_json(j); REQUIRE(t.def);
    CHECK(t.def->control == ControlKind::Turned); CHECK(t.def->destroy_cr8_max == 8);

    auto bad = [](eng::assets::Json x, const std::string& want) {
        auto r = spell_from_json(x); INFO(want); CHECK_FALSE(r.def); CHECK(r.error.find(want) != std::string::npos);
    };
    j = base; j["effects"][0]["control"] = "charm"; bad(j, "effect control invalid");
    j = base; j["effects"][0]["control_excludes_creature_types"] = {"ghost"}; bad(j, "control_excludes_creature_types invalid");
    j = base; j["effects"][0]["control_min_int"] = 0; bad(j, "control_min_int invalid");
    j = base; j["effects"][0]["destroy_undead_max_cr8"] = 12; bad(j, "destroy_undead_max_cr8 invalid");
    j = base; j["effects"][0].erase("control"); bad(j, "needs control");
    j = base; j["effects"][0].erase("control"); j["effects"][0].erase("control_excludes_creature_types");
    bad(j, "save effect needs damage, condition or control");
    j = base; j["effects"] = eng::assets::Json::array({{{"kind", "save"}, {"ability", "wis"}, {"condition", "prone"}}, base["effects"][0]});
    bad(j, "control is only allowed on the first effect");
}

TEST_CASE("loader 7.2zc: ends_on, scope target marked, ability chosen, move_mark") {
    auto inv = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.invisibility","level":2,"name":"Invisibility",
        "casting_time":{"kind":"action"},"range":{"kind":"touch"},"components":{"v":true,"s":true},
        "duration":{"kind":"hours","amount":1,"concentration":true},
        "effects":[{"kind":"apply_condition","condition":"invisible","ends_on":["attack","cast"]}]})");
    auto i = spell_from_json(inv); REQUIRE(i.def);
    CHECK(i.def->ends_on == (kEndsOnAttack | kEndsOnCast));
    auto hex = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.hex","level":1,"name":"Hex",
        "casting_time":{"kind":"bonus_action"},"range":{"kind":"ranged","feet":90},"components":{"v":true,"s":true,"m":{"text":"eye"}},
        "duration":{"kind":"hours","amount":1,"concentration":true},"move_mark":{"action":"bonus_action"},
        "effects":[{"kind":"apply_modifier","scope":{"target":"marked"},
                    "modifier":{"stat":"damage","die":"1d6","damage_type":"necrotic","applies_to":"self","duration":{"kind":"concentration"}}},
                   {"kind":"apply_modifier","scope":{"target":"marked"},
                    "modifier":{"stat":"ability_check","mode":"disadvantage","ability":"chosen","applies_to":"target","duration":{"kind":"concentration"}}}]})");
    auto h = spell_from_json(hex); REQUIRE(h.def);
    CHECK(h.def->move_mark == 1);
    CHECK(h.def->modifier.mark_scope == 1);
    CHECK(h.def->modifier.stat == ModStat::Damage);
    REQUIRE(h.def->extra_effects.size() == 1);
    CHECK(h.def->extra_effects[0].modifier.mark_scope == 1);
    CHECK(h.def->extra_effects[0].modifier.chosen_ability);
    CHECK_FALSE(h.def->extra_effects[0].modifier.has_ability);

    auto bad = [](eng::assets::Json x, const std::string& want) {
        auto r = spell_from_json(x); INFO(want); INFO(r.error); CHECK_FALSE(r.def); CHECK(r.error.find(want) != std::string::npos);
    };
    auto j = inv; j["effects"][0]["ends_on"] = {"attack", "attack"}; bad(j, "effect ends_on items must be unique");
    j = inv; j["effects"][0]["ends_on"] = {"move"}; bad(j, "effect ends_on must be a non-empty list of attack, cast");
    j = inv; j["effects"][0]["ends_on"] = eng::assets::Json::array(); bad(j, "effect ends_on must be a non-empty list of attack, cast");
    j = inv; j["duration"] = {{"kind", "instantaneous"}}; bad(j, "effect ends_on needs concentration or a rounds, minutes or hours duration");
    j = inv; j["duration"] = {{"kind", "minutes"}, {"amount", 1}}; CHECK(spell_from_json(j).def);   // a duration without concentration
    j = hex; j["effects"][0]["scope"] = {{"target", "self"}}; bad(j, "effect scope must be {target: marked}");
    j = hex; j["effects"][0]["modifier"]["applies_to"] = "target"; bad(j, "effect scope target marked: a damage modifier needs applies_to self");
    j = hex; j["effects"][0]["modifier"] = {{"stat", "attack_roll"}, {"bonus", 1}, {"applies_to", "self"}, {"duration", {{"kind", "concentration"}}}};
    bad(j, "effect scope target marked: damage or ability_check modifiers only");
    j = hex; j["effects"][1].erase("scope"); bad(j, "effect modifier ability chosen needs scope target marked");
    j = hex; j["effects"][1]["modifier"]["stat"] = "save"; bad(j, "effect modifier ability chosen: ability_check modifiers only");
    j = hex; j["effects"][1]["modifier"]["applies_to"] = "allies_within_30"; bad(j, "effect scope target marked: applies_to self or target");
    j = hex; j["duration"].erase("concentration"); bad(j, "move_mark needs a concentration spell");
    j = hex; j["move_mark"] = {{"action", "action"}}; bad(j, "move_mark must be {action: bonus_action}");
    j = hex; j["effects"] = eng::assets::Json::array({inv["effects"][0]}); j["effects"][0].erase("ends_on");
    bad(j, "move_mark needs an apply_modifier with scope target marked");
}

TEST_CASE("loader 7.2zd: warded + ward, ends_on harmful_cast, scope from_types + applies_to attackers, condition_immunity_from, max_hp_bonus") {
    auto sanct = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.sanctuary","level":1,"name":"Sanctuary",
        "casting_time":{"kind":"bonus_action"},"range":{"kind":"ranged","feet":30},"components":{"v":true,"s":true},
        "duration":{"kind":"rounds","amount":10},
        "effects":[{"kind":"apply_condition","condition":"warded","ward":{"save":"wis","on_fail":"retarget_or_lose"},
                    "ends_on":["attack","harmful_cast"]}]})");
    auto s = spell_from_json(sanct); REQUIRE(s.def);
    CHECK(s.def->condition == Condition::Warded);
    CHECK(s.def->has_ward); CHECK(s.def->ward_save == Ability::Wis);
    CHECK(s.def->ends_on == (kEndsOnAttack | kEndsOnHarmfulCast));
    auto pfeg = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.protection_from_evil_and_good","level":1,
        "name":"Protection from Evil and Good","casting_time":{"kind":"action"},"range":{"kind":"touch"},"components":{"v":true,"s":true},
        "duration":{"kind":"rounds","amount":100,"concentration":true},
        "effects":[{"kind":"apply_modifier","scope":{"from_types":["aberration","celestial","elemental","fey","fiend","undead"]},
                    "modifier":{"stat":"attack_roll","mode":"disadvantage","applies_to":"attackers","duration":{"kind":"concentration"}}},
                   {"kind":"condition_immunity_from","conditions":["charmed","frightened"],
                    "types":["aberration","celestial","elemental","fey","fiend","undead"]}]})");
    auto p = spell_from_json(pfeg); REQUIRE(p.def);
    CHECK(p.def->modifier.applies_to == ModAppliesTo::Attackers);
    CHECK(p.def->modifier.from_types == (bit(CreatureType::Aberration) | bit(CreatureType::Celestial) | bit(CreatureType::Elemental) |
                                         bit(CreatureType::Fey) | bit(CreatureType::Fiend) | bit(CreatureType::Undead)));
    REQUIRE(p.def->extra_effects.size() == 1);
    CHECK(p.def->extra_effects[0].kind == SpellEffectKind::ConditionImmunityFrom);
    CHECK(p.def->extra_effects[0].immune_conditions == (bit(Condition::Charmed) | bit(Condition::Frightened)));
    auto aid = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.aid","level":2,"name":"Aid",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":30},"components":{"v":true,"s":true,"m":{"text":"strip"}},
        "duration":{"kind":"rounds","amount":4800},"targets":{"count":3},
        "effects":[{"kind":"max_hp_bonus","flat":5,"per_slot_above":5}]})");
    auto a = spell_from_json(aid); INFO(a.error); REQUIRE(a.def);
    CHECK(a.def->kind == SpellEffectKind::MaxHpBonus);
    CHECK(a.def->hp_bonus_flat == 5); CHECK(a.def->hp_bonus_per_slot == 5); CHECK(a.def->targets == 3);

    auto bad = [](eng::assets::Json x, const std::string& want) {
        auto r = spell_from_json(x); INFO(want); INFO(r.error); CHECK_FALSE(r.def); CHECK(r.error == want);   // exact, no suffix
    };
    auto j = sanct; j["effects"][0].erase("ward"); bad(j, "effect condition warded needs ward");
    j = sanct; j["effects"][0]["condition"] = "invisible"; bad(j, "effect ward needs condition warded");
    j = sanct; j["effects"][0]["ward"] = {{"save", "wis"}}; bad(j, "effect ward must be {save: <ability>, on_fail: retarget_or_lose}");
    j = sanct; j["effects"][0]["ends_on"] = {"harmful_cast", "harmful_cast"}; bad(j, "effect ends_on items must be unique");
    j = sanct; j["effects"][0]["ends_on"] = {"spell"}; bad(j, "effect ends_on must be a non-empty list of attack, cast, harmful_cast, damage, shake_awake");
    j = sanct; j["effects"][0].erase("ends_on"); j["duration"] = {{"kind", "instantaneous"}};
    bad(j, "effect ward needs concentration or a rounds, minutes or hours duration");
    j = pfeg; j["effects"][0]["modifier"]["stat"] = "save"; bad(j, "effect modifier applies_to attackers: attack_roll modifiers only");
    j = pfeg; j["effects"][0]["modifier"]["applies_to"] = "target"; bad(j, "effect scope from_types needs applies_to attackers");
    j = pfeg; j["effects"][0]["scope"]["from_types"] = {"fiend", "fiend"}; bad(j, "effect scope from_types items must be unique");
    j = pfeg; j["effects"][0]["scope"]["from_types"] = {"devil"}; bad(j, "effect scope from_types must be a non-empty list of creature types");
    j = pfeg; j["effects"][0]["scope"] = {{"weapon", "wielded"}}; bad(j, "effect scope must be {target: marked} and/or {from_types: [creature types]}");
    j = pfeg; j["effects"][1]["conditions"] = {"charmed", "stunned"}; bad(j, "effects[1]: effect conditions must be a non-empty list of charmed, frightened");
    j = pfeg; j["effects"][1]["conditions"] = {"charmed", "charmed"}; bad(j, "effects[1]: effect conditions items must be unique");
    j = pfeg; j["effects"][1]["types"] = eng::assets::Json::array(); bad(j, "effects[1]: effect types must be a non-empty list of creature types");
    j = pfeg; j["effects"][1]["types"] = {"undead", "undead"}; bad(j, "effects[1]: effect types items must be unique");
    j = pfeg; j["duration"] = {{"kind", "instantaneous"}}; j["effects"].erase(0);
    bad(j, "effect condition_immunity_from needs concentration or a rounds, minutes or hours duration");
    j = aid; j["effects"][0]["flat"] = 51; bad(j, "effect flat must be an integer 0..50");
    j = aid; j["effects"][0]["per_slot_above"] = -1; bad(j, "effect per_slot_above must be an integer 0..50");
    j = aid; j["effects"][0]["flat"] = 0; j["effects"][0]["per_slot_above"] = 0; bad(j, "effect max_hp_bonus needs flat or per_slot_above above 0");
    j = aid; j["duration"] = {{"kind", "instantaneous"}}; bad(j, "effect max_hp_bonus needs concentration or a rounds, minutes or hours duration");
}

TEST_CASE("loader 7.2ze: modifier sign + applies_to rolls, apply_modifier save gate, save condition chosen + choices") {
    auto bless = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.bless","level":1,"name":"Bless",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":30},"components":{"v":true,"s":true,"m":{"text":"holy water"}},
        "duration":{"kind":"rounds","amount":10,"concentration":true},"targets":{"count":3},
        "effects":[{"kind":"apply_modifier","modifier":{"stat":"attack_roll","die":"1d4","sign":"plus","applies_to":["attack","save"],
                    "duration":{"kind":"concentration"}}}],"scaling":{"upcast":{"targets":1}}})");
    auto b = spell_from_json(bless); INFO(b.error); REQUIRE(b.def);
    CHECK(b.def->modifier.die_mult == 1); CHECK(b.def->modifier.also_rolls == kRollSave);
    CHECK(b.def->modifier.applies_to == ModAppliesTo::Target);
    CHECK(b.def->targets == 3); CHECK(b.def->upcast_targets == 1); CHECK_FALSE(b.def->has_modifier_save);
    auto bane = bless;
    bane["id"] = "srd51.spell.bane"; bane["effects"][0]["modifier"]["sign"] = "minus";
    bane["effects"][0]["save"] = {{"ability", "cha"}, {"dc", "caster"}};
    auto n = spell_from_json(bane); INFO(n.error); REQUIRE(n.def);
    CHECK(n.def->modifier.die_mult == -1); CHECK(n.def->has_modifier_save); CHECK(n.def->modifier_save == Ability::Cha);
    auto blind = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.blindness_deafness","level":2,
        "name":"Blindness/Deafness","casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":30},"components":{"v":true},
        "duration":{"kind":"rounds","amount":10},
        "effects":[{"kind":"save","ability":"con","condition":"chosen","choices":["blinded","deafened"],"repeat_save":"end_of_turn"}],
        "scaling":{"upcast":{"targets":1}}})");
    auto d = spell_from_json(blind); INFO(d.error); REQUIRE(d.def);
    CHECK(d.def->condition_choices == std::vector<Condition>{Condition::Blinded, Condition::Deafened});
    CHECK(d.def->applies_condition); CHECK(d.def->repeat_save == RepeatSave::EndOfTurn); CHECK_FALSE(d.def->concentration);

    auto bad = [](eng::assets::Json x, const std::string& want) {
        auto r = spell_from_json(x); INFO(want); INFO(r.error); CHECK_FALSE(r.def); CHECK(r.error == want);   // exact, no suffix
    };
    auto j = bless; j["effects"][0]["modifier"]["sign"] = "neg"; bad(j, "effect modifier sign must be plus or minus");
    j = bless; j["effects"][0]["modifier"].erase("die"); bad(j, "effect modifier sign needs die");
    j = bless; j["effects"][0]["modifier"]["die"] = "-1d4"; bad(j, "effect modifier sign and a negative die together");
    j = bless; j["effects"][0]["modifier"]["applies_to"] = eng::assets::Json::array(); bad(j, "effect modifier applies_to rolls must be a non-empty list of attack, save");
    j = bless; j["effects"][0]["modifier"]["applies_to"] = {"attack", "check"}; bad(j, "effect modifier applies_to rolls must be a non-empty list of attack, save");
    j = bless; j["effects"][0]["modifier"]["applies_to"] = {"attack", "attack"}; bad(j, "effect modifier applies_to rolls items must be unique");
    j = bless; j["effects"][0]["modifier"]["applies_to"] = {"save"}; bad(j, "effect modifier applies_to rolls must include the modifier's own stat");
    j = bless; j["effects"][0]["modifier"]["stat"] = "ac"; j["effects"][0]["modifier"].erase("die"); j["effects"][0]["modifier"].erase("sign");
    j["effects"][0]["modifier"]["bonus"] = 1; bad(j, "effect modifier applies_to rolls: attack_roll or save modifiers only");
    j = bane; j["effects"][0]["save"] = {{"ability", "cha"}, {"dc", 13}}; bad(j, "effect save must be {ability: <ability>, dc: caster}");
    j = bane; j["effects"][0]["modifier"]["applies_to"] = "self"; bad(j, "effect save needs a modifier on the targets (applies_to target or rolls)");
    j = blind; j["effects"][0].erase("choices"); bad(j, "effect condition chosen needs choices");
    j = blind; j["effects"][0]["condition"] = "blinded"; bad(j, "effect choices needs condition chosen");
    j = blind; j["effects"][0]["choices"] = {"blinded"}; bad(j, "effect choices must be a list of 2..4 conditions");
    j = blind; j["effects"][0]["choices"] = {"blinded", "warded"}; bad(j, "effect choices must be a list of 2..4 conditions");
    j = blind; j["effects"][0]["choices"] = {"blinded", "blinded"}; bad(j, "effect choices items must be unique");
}

TEST_CASE("loader 7.2zg: haste / slow kinds expand into a marker row plus AC, Dex save and speed rows") {
    auto haste = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.haste","level":3,"name":"Haste",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":30},"components":{"v":true,"s":true,"m":{"text":"licorice root"}},
        "duration":{"kind":"minutes","amount":1,"concentration":true},"effects":[{"kind":"haste"}]})");
    auto h = spell_from_json(haste); INFO(h.error); REQUIRE(h.def);
    CHECK(h.def->kind == SpellEffectKind::ApplyModifier);
    CHECK(h.def->modifier.stat == ModStat::Haste); CHECK_FALSE(h.def->has_modifier_save);
    REQUIRE(h.def->extra_effects.size() == 3);
    CHECK(h.def->extra_effects[0].modifier.stat == ModStat::Ac); CHECK(h.def->extra_effects[0].modifier.bonus == 2);
    CHECK(h.def->extra_effects[1].modifier.stat == ModStat::Save); CHECK(h.def->extra_effects[1].modifier.ability == Ability::Dex);
    CHECK(h.def->extra_effects[1].modifier.mode == RollMode::Advantage);
    CHECK(h.def->extra_effects[2].modifier.stat == ModStat::Speed); CHECK(h.def->extra_effects[2].modifier.speed_num == 2);
    for (const auto& x : h.def->extra_effects) { CHECK(x.affected_only); CHECK(x.modifier.concentration); CHECK(x.modifier.id != h.def->modifier.id); }
    auto slow = haste; slow["id"] = "srd51.spell.slow"; slow["name"] = "Slow";
    slow["effects"] = eng::assets::Json::parse(R"([{"kind":"slow","ability":"wis"}])");
    auto w = spell_from_json(slow); INFO(w.error); REQUIRE(w.def);
    CHECK(w.def->modifier.stat == ModStat::Slow); CHECK(w.def->has_modifier_save); CHECK(w.def->modifier_save == Ability::Wis);
    CHECK(w.def->modifier.repeat_save == 1); CHECK(w.def->modifier.repeat_ability == Ability::Wis);
    REQUIRE(w.def->extra_effects.size() == 3);
    CHECK(w.def->extra_effects[0].modifier.bonus == -2); CHECK(w.def->extra_effects[1].modifier.bonus == -2);
    CHECK(w.def->extra_effects[2].modifier.speed_den == 2);
    auto bad = [](eng::assets::Json x, const std::string& want) {
        auto r = spell_from_json(x); INFO(want); INFO(r.error); CHECK_FALSE(r.def); CHECK(r.error == want);
    };
    auto j = haste; j["duration"]["concentration"] = false; bad(j, "effect haste needs concentration");
    j = slow; j["duration"]["concentration"] = false; bad(j, "effect slow needs concentration");
    j = slow; j["effects"][0].erase("ability"); bad(j, "effect slow needs ability");
    j = haste; j["effects"][0]["ability"] = "wis"; bad(j, "effect haste takes no ability");
}

TEST_CASE("loader 7.2zg: summon_weapon (Spiritual Weapon) maps onto SpellEffectKind::SummonWeapon") {
    auto sw = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.spiritual_weapon","level":2,"name":"Spiritual Weapon",
        "casting_time":{"kind":"bonus_action"},"range":{"kind":"ranged","feet":60},"components":{"v":true,"s":true},
        "duration":{"kind":"minutes","amount":1},
        "effects":[{"kind":"summon_weapon","damage":{"dice":"1d8","type":"force"},"add_spellcasting_modifier":true,
                    "upcast_every":2,"reach_ft":5,"move_ft":20}],"scaling":{"upcast":{"dice":1}}})");
    auto r = spell_from_json(sw); INFO(r.error); REQUIRE(r.def);
    CHECK(r.def->kind == SpellEffectKind::SummonWeapon); CHECK(r.def->melee); CHECK(r.def->add_spellcasting_modifier);
    CHECK(r.def->dice.count == 1); CHECK(r.def->dice.sides == 8); CHECK(r.def->damage_type == DamageType::Force);
    CHECK(r.def->upcast_dice == 1); CHECK(r.def->summon_weapon.upcast_every == 2);
    CHECK(r.def->summon_weapon.reach_ft == 5); CHECK(r.def->summon_weapon.move_ft == 20);
    CHECK(r.def->casting_time == CastingTime::BonusAction); CHECK_FALSE(r.def->concentration);
    auto bad = [](eng::assets::Json x, const std::string& want) {
        auto q = spell_from_json(x); INFO(want); INFO(q.error); CHECK_FALSE(q.def); CHECK(q.error == want);
    };
    auto j = sw; j["effects"][0].erase("damage"); bad(j, "effect damage invalid");
    j = sw; j["effects"][0]["upcast_every"] = 0; bad(j, "effect upcast_every must be an integer 1..9");
    j = sw; j["effects"][0]["reach_ft"] = 7; bad(j, "effect reach_ft must be a positive multiple of 5");
    j = sw; j["effects"][0]["move_ft"] = 0; bad(j, "effect move_ft must be a positive multiple of 5");
    j = sw; j["duration"] = {{"kind", "instantaneous"}}; bad(j, "effect summon_weapon needs concentration or a rounds, minutes or hours duration");
    j = sw; j["effects"] = eng::assets::Json::array({{{"kind", "heal"}, {"dice", "1d4"}}, sw["effects"][0]});
    j.erase("scaling");
    bad(j, "effects[1]: summon_weapon is only allowed as the first effect");
}

TEST_CASE("loader 7.2zg: temp_hp (False Life dice + per_slot_above; Heroism ability + per_turn + condition_immunity)") {
    auto fl = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.false_life","level":1,"name":"False Life",
        "casting_time":{"kind":"action"},"range":{"kind":"self"},"components":{"v":true,"s":true,"m":{"text":"alcohol"}},
        "duration":{"kind":"hours","amount":1},"effects":[{"kind":"temp_hp","dice":"1d4+4","per_slot_above":5}]})");
    auto r = spell_from_json(fl); INFO(r.error); REQUIRE(r.def);
    CHECK(r.def->kind == SpellEffectKind::TempHp);
    CHECK(r.def->temp_hp.dice.count == 1); CHECK(r.def->temp_hp.dice.sides == 4); CHECK(r.def->temp_hp.dice.bonus == 4);
    CHECK(r.def->temp_hp.per_slot_above == 5); CHECK_FALSE(r.def->temp_hp.ability); CHECK_FALSE(r.def->temp_hp.per_turn);
    auto he = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.heroism","level":1,"name":"Heroism",
        "casting_time":{"kind":"action"},"range":{"kind":"touch"},"components":{"v":true,"s":true},
        "duration":{"kind":"minutes","amount":1,"concentration":true},
        "effects":[{"kind":"temp_hp","ability":"spellcasting","per_turn":true,"condition_immunity":["frightened"]}],
        "scaling":{"upcast":{"targets":1}}})");
    auto h = spell_from_json(he); INFO(h.error); REQUIRE(h.def);
    CHECK(h.def->temp_hp.ability); CHECK(h.def->temp_hp.per_turn); CHECK(h.def->temp_hp.immune_conditions == bit(Condition::Frightened));
    auto bad = [](eng::assets::Json x, const std::string& want) {
        auto q = spell_from_json(x); INFO(want); INFO(q.error); CHECK_FALSE(q.def); CHECK(q.error == want);
    };
    auto j = fl; j["effects"][0]["ability"] = "spellcasting"; bad(j, "effect temp_hp needs exactly one of dice or ability");
    j = fl; j["effects"][0].erase("dice"); bad(j, "effect temp_hp needs exactly one of dice or ability");
    j = fl; j["effects"][0]["dice"] = "xd4"; bad(j, "effect dice invalid");
    j = fl; j["effects"][0]["per_slot_above"] = 0; bad(j, "effect per_slot_above must be an integer 1..50");
    j = he; j["effects"][0]["ability"] = "wis"; bad(j, "effect ability must be spellcasting");
    j = he; j["effects"][0]["per_turn"] = 1; bad(j, "effect per_turn must be a boolean");
    j = he; j["effects"][0]["condition_immunity"] = eng::assets::Json::array(); bad(j, "effect condition_immunity must be a non-empty list of conditions");
    j = he; j["effects"][0]["condition_immunity"] = {"frightened", "frightened"}; bad(j, "effect condition_immunity items must be unique");
    j = he; j["duration"] = {{"kind", "instantaneous"}};
    bad(j, "effect temp_hp per_turn or condition_immunity needs concentration or a rounds, minutes or hours duration");
}

TEST_CASE("loader 7.2zg: spell_attack on_miss half (Acid Arrow)") {
    auto aa = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.acid_arrow","level":2,"name":"Acid Arrow",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":90},"components":{"v":true,"s":true,"m":{"text":"rhubarb"}},
        "duration":{"kind":"instantaneous"},
        "effects":[{"kind":"spell_attack","attack":"ranged","damage":{"dice":"4d4","type":"acid"},"on_miss":"half"},
                   {"kind":"delayed_damage","applies_to":"affected","damage":{"dice":"2d4","type":"acid"},
                    "expiry":{"kind":"until_end_of_target_next_turn"}}],"scaling":{"upcast":{"dice":1}}})");
    auto r = spell_from_json(aa); INFO(r.error); REQUIRE(r.def);
    CHECK(r.def->miss_half); REQUIRE(r.def->extra_effects.size() == 1);
    CHECK(r.def->extra_effects[0].kind == SpellEffectKind::DelayedDamage); CHECK(r.def->extra_effects[0].affected_only);
    CHECK(r.def->upcast_dice == 1); CHECK(r.def->extra_effects[0].upcast_dice == 1);   // SRD: both gain 1d4 per slot above 2nd
    auto j = aa; j["effects"][0]["on_miss"] = "none";
    auto q = spell_from_json(j); CHECK_FALSE(q.def); CHECK(q.error == "effect on_miss must be half");
    j = aa; j["effects"] = eng::assets::Json::array({{{"kind", "heal"}, {"dice", "1d4"}},
        {{"kind", "spell_attack"}, {"attack", "ranged"}, {"damage", {{"dice", "4d4"}, {"type", "acid"}}}, {"on_miss", "half"}}});
    j.erase("scaling");
    q = spell_from_json(j); CHECK_FALSE(q.def); CHECK(q.error == "effects[1]: on_miss is only allowed on the first effect");
}

TEST_CASE("loader 7.2zg: casting_time.trigger damaged (Hellish Rebuke)") {
    auto hr = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.hellish_rebuke","level":1,"name":"Hellish Rebuke",
        "casting_time":{"kind":"reaction","trigger":{"kind":"damaged","requires_sight":true}},"range":{"kind":"ranged","feet":60},
        "components":{"v":true,"s":true},"duration":{"kind":"instantaneous"},
        "effects":[{"kind":"save","ability":"dex","damage":{"dice":"2d10","type":"fire"},"on_success":"half"}],
        "scaling":{"upcast":{"dice":1}}})");
    auto r = spell_from_json(hr); INFO(r.error); REQUIRE(r.def);
    CHECK(r.def->casting_time == CastingTime::Reaction); CHECK(r.def->reaction_trigger == ReactionTrigger::Damaged);
    CHECK(r.def->requires_sight); CHECK(r.def->range_ft == 60);
    auto j = hr; j["casting_time"]["trigger"].erase("requires_sight");
    auto q = spell_from_json(j); CHECK(q.error == "casting_time.trigger.kind damaged needs requires_sight");
    j = hr; j["range"] = {{"kind", "touch"}};
    q = spell_from_json(j); CHECK(q.error == "casting_time.trigger.kind damaged needs a ranged range");
    j = hr; j["effects"] = eng::assets::Json::array({{{"kind", "heal"}, {"dice", "1d4"}}}); j.erase("scaling");
    q = spell_from_json(j); CHECK(q.error == "casting_time.trigger.kind damaged needs a save effect");
    j = hr; j["casting_time"]["trigger"]["kind"] = "hurt";
    q = spell_from_json(j); CHECK(q.error == "casting_time.trigger.kind must be being_hit, spell_cast or damaged");
}

TEST_CASE("loader 7.2zg: apply_modifier damage_type_choices (Protection from Energy)") {
    auto pe = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.protection_from_energy","level":3,"name":"Protection from Energy",
        "casting_time":{"kind":"action"},"range":{"kind":"touch"},"components":{"v":true,"s":true},
        "duration":{"kind":"hours","amount":1,"concentration":true},
        "effects":[{"kind":"apply_modifier","modifier":{"stat":"resistance","damage_type":"acid","duration":{"kind":"concentration"}},
                    "damage_type_choices":["acid","cold","fire","lightning","thunder"]}]})");
    auto r = spell_from_json(pe); INFO(r.error); REQUIRE(r.def);
    REQUIRE(r.def->damage_choices.size() == 5);
    CHECK(r.def->damage_choices[2] == DamageType::Fire); CHECK(r.def->modifier.stat == ModStat::Resistance);
    auto j = pe; j["effects"][0]["damage_type_choices"] = {"cold", "fire"};
    auto q = spell_from_json(j); CHECK(q.error == "effect damage_type_choices must include the modifier damage_type");
    j = pe; j["effects"][0]["damage_type_choices"] = {"acid", "acid"};
    q = spell_from_json(j); CHECK(q.error == "effect damage_type_choices must be 2..13 distinct damage types");
    j = pe; j["effects"][0]["modifier"] = {{"stat", "ac"}, {"bonus", 1}, {"duration", {{"kind", "concentration"}}}};
    q = spell_from_json(j); CHECK(q.error == "effect damage_type_choices needs a resistance modifier");
    j = pe; j["effects"] = eng::assets::Json::array({{{"kind", "heal"}, {"dice", "1d4"}}, pe["effects"][0]});
    j["range"] = {{"kind", "touch"}};
    q = spell_from_json(j); CHECK(q.error == "effects[1]: damage_type_choices is only allowed on the first effect");
}

TEST_CASE("loader 7.2zg: exempt_senses, one key for Blur (apply_modifier) and Mirror Image (duplicates)") {
    auto blur = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.blur","level":2,"name":"Blur",
        "casting_time":{"kind":"action"},"range":{"kind":"self"},"components":{"v":true},
        "duration":{"kind":"minutes","amount":1,"concentration":true},
        "effects":[{"kind":"apply_modifier","modifier":{"stat":"attacks_against","mode":"disadvantage","duration":{"kind":"concentration"}},
                    "exempt_senses":["blindsight","truesight"]}]})");
    auto r = spell_from_json(blur); INFO(r.error); REQUIRE(r.def);
    CHECK(r.def->modifier.stat == ModStat::AttacksAgainst);
    CHECK(r.def->modifier.exempt_senses == (kSenseBlindsight | kSenseTruesight));
    auto j = blur; j["effects"][0]["exempt_senses"] = {"blindsight", "blindsight"};
    auto q = spell_from_json(j);
    CHECK(q.error == "effect exempt_senses must be distinct senses (blindsight, tremorsense, truesight), at least one");
    j = blur; j["effects"][0]["exempt_senses"] = {"smell"};
    q = spell_from_json(j);
    CHECK(q.error == "effect exempt_senses must be distinct senses (blindsight, tremorsense, truesight), at least one");
    j = blur; j["effects"][0]["modifier"] = {{"stat", "ac"}, {"bonus", 1}, {"duration", {{"kind", "concentration"}}}};
    q = spell_from_json(j); CHECK(q.error == "effect exempt_senses needs an attacks_against modifier");
    auto mi = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.mirror_image","level":2,"name":"Mirror Image",
        "casting_time":{"kind":"action"},"range":{"kind":"self"},"components":{"v":true,"s":true},"duration":{"kind":"minutes","amount":1},
        "effects":[{"kind":"duplicates","count":3,"thresholds":[11,8,6],"ac":{"base":10,"ability":"dex"}}]})");
    auto a = spell_from_json(mi); INFO(a.error); REQUIRE(a.def);
    CHECK(a.def->duplicates.exempt_senses == (kSenseBlindsight | kSenseTruesight));   // default (7.2zf behaviour)
    mi["effects"][0]["exempt_senses"] = {"tremorsense"};
    a = spell_from_json(mi); INFO(a.error); REQUIRE(a.def);
    CHECK(a.def->duplicates.exempt_senses == kSenseTremorsense);
    mi["effects"][0]["exempt_senses"] = eng::assets::Json::array();
    a = spell_from_json(mi);
    CHECK(a.error == "effect exempt_senses must be distinct senses (blindsight, tremorsense, truesight), at least one");
}

TEST_CASE("loader 7.2zg: modifier stat outlined (Faerie Fire), Dex save gate") {
    auto ff = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.faerie_fire","level":1,"name":"Faerie Fire",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":60},"components":{"v":true},
        "area":{"shape":"cube","size_ft":20},"duration":{"kind":"minutes","amount":1,"concentration":true},
        "effects":[{"kind":"apply_modifier","modifier":{"stat":"outlined","duration":{"kind":"concentration"}},
                    "save":{"ability":"dex","dc":"caster"}}]})");
    auto r = spell_from_json(ff); INFO(r.error); REQUIRE(r.def);
    CHECK(r.def->modifier.stat == ModStat::Outlined); CHECK(r.def->has_modifier_save); CHECK(r.def->modifier_save == Ability::Dex);
    auto j = ff; j["effects"][0]["modifier"]["stat"] = "outline";
    auto q = spell_from_json(j); CHECK(q.error == "effect modifier stat invalid");
}

TEST_CASE("loader 7.2zg: ends_on damage / shake_awake on save and apply_condition effects (Hypnotic Pattern)") {
    auto hp = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.hypnotic_pattern","level":3,"name":"Hypnotic Pattern",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":120},"components":{"s":true,"m":{"text":"a glowing stick of incense"}},
        "area":{"shape":"cube","size_ft":30},"duration":{"kind":"minutes","amount":1,"concentration":true},
        "effects":[{"kind":"save","ability":"wis","condition":"charmed","ends_on":["damage","shake_awake"]},
                   {"kind":"apply_condition","applies_to":"affected","condition":"incapacitated","ends_on":["damage","shake_awake"]}]})");
    auto r = spell_from_json(hp); INFO(r.error); REQUIRE(r.def);
    CHECK(r.def->ends_on == (kEndsOnDamage | kEndsOnShakeAwake)); REQUIRE(r.def->extra_effects.size() == 1);
    CHECK(r.def->extra_effects[0].ends_on == (kEndsOnDamage | kEndsOnShakeAwake));
    auto j = hp; j["effects"][0]["ends_on"] = {"damage", "damage"};
    auto q = spell_from_json(j); CHECK(q.error == "effect ends_on items must be unique");
    j = hp; j["effects"][0]["ends_on"] = {"sneeze"};
    q = spell_from_json(j); CHECK(q.error == "effect ends_on must be a non-empty list of attack, cast, harmful_cast, damage, shake_awake");
    j = hp; j["effects"][0].erase("condition"); j["effects"][0]["damage"] = {{"dice", "1d6"}, {"type", "fire"}}; j["effects"][0]["on_success"] = "half";
    q = spell_from_json(j); CHECK(q.error == "effect ends_on on a save effect needs a condition");
    j = hp; j["duration"] = {{"kind", "instantaneous"}};
    q = spell_from_json(j); CHECK(q.error == "effect ends_on needs concentration or a rounds, minutes or hours duration");
}

TEST_CASE("loader 7.2zh: weapon_smite (Branding Smite), summon_weapon held (Flame Blade), sustained_damage held_object") {
    auto sm = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"test.spell.zh_branding_smite","level":2,"name":"Branding Smite",
        "casting_time":{"kind":"bonus_action"},"range":{"kind":"self"},"components":{"v":true},
        "duration":{"kind":"minutes","amount":1,"concentration":true},
        "effects":[{"kind":"weapon_smite","damage":{"dice":"2d6","type":"radiant"},"reveal_invisible":true,"light":{"dim_ft":5}}],
        "scaling":{"upcast":{"dice":1}}})");
    auto r = spell_from_json(sm); INFO(r.error); REQUIRE(r.def);
    CHECK(r.def->kind == SpellEffectKind::WeaponSmite);
    CHECK(r.def->dice == DiceExpr{2, 6, 0}); CHECK(r.def->damage_type == DamageType::Radiant);
    CHECK(r.def->weapon_smite.reveal_invisible); CHECK(r.def->weapon_smite.dim_ft == 5);
    CHECK(r.def->upcast_dice == 1); CHECK(r.def->concentration);
    CHECK(r.def->expiry == 1); CHECK(r.def->duration_rounds == 10);
    CHECK(r.def->casting_time == CastingTime::BonusAction);
    auto bad = [](eng::assets::Json x, const std::string& want) {
        auto q = spell_from_json(x); INFO(want); INFO(q.error); CHECK_FALSE(q.def); CHECK(q.error == want);
    };
    auto j = sm; j["duration"] = {{"kind", "instantaneous"}};
    bad(j, "weapon_smite: needs concentration or a rounds, minutes or hours duration");
    j = sm; j["effects"][0]["light"]["dim_ft"] = 6;
    bad(j, "effect weapon_smite light.dim_ft must be a positive multiple of 5");
    j = sm; j["effects"][0]["reveal_invisible"] = "yes"; bad(j, "effect reveal_invisible must be a boolean");
    j = sm; j["effects"][0]["damage"] = {{"dice", "xd6"}, {"type", "radiant"}}; bad(j, "effect damage invalid");
    j = sm; j["effects"] = eng::assets::Json::array({{{"kind", "heal"}, {"dice", "1d4"}}, sm["effects"][0]}); j.erase("scaling");
    bad(j, "effects[1]: weapon_smite is only allowed as the first effect");
    j = sm; j["duration"] = {{"kind", "minutes"}, {"amount", 1}}; j["effects"][0].erase("reveal_invisible"); j["effects"][0].erase("light");
    auto timed = spell_from_json(j); INFO(timed.error); REQUIRE(timed.def);
    CHECK_FALSE(timed.def->concentration); CHECK_FALSE(timed.def->weapon_smite.reveal_invisible); CHECK(timed.def->weapon_smite.dim_ft == 0);

    auto fb = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"test.spell.zh_flame_blade","level":2,"name":"Flame Blade",
        "casting_time":{"kind":"bonus_action"},"range":{"kind":"self"},"components":{"v":true,"s":true,"m":{"text":"sumac leaf"}},
        "duration":{"kind":"minutes","amount":10,"concentration":true},
        "effects":[{"kind":"summon_weapon","damage":{"dice":"3d6","type":"fire"},"upcast_every":2,"reach_ft":5,"held":true,
                    "light":{"bright_ft":10,"dim_ft":10}}],"scaling":{"upcast":{"dice":1}}})");
    auto blade = spell_from_json(fb); INFO(blade.error); REQUIRE(blade.def);
    CHECK(blade.def->kind == SpellEffectKind::SummonWeapon); CHECK(blade.def->summon_weapon.held);
    CHECK(blade.def->summon_weapon.move_ft == 0); CHECK_FALSE(blade.def->add_spellcasting_modifier);
    CHECK(blade.def->summon_weapon.upcast_every == 2); CHECK(blade.def->upcast_dice == 1);
    CHECK(blade.def->summon_weapon.light_bright_ft == 10); CHECK(blade.def->summon_weapon.light_dim_ft == 10);
    CHECK(blade.def->dice == DiceExpr{3, 6, 0}); CHECK(blade.def->damage_type == DamageType::Fire);
    j = fb; j["effects"][0]["move_ft"] = 20; bad(j, "summon_weapon: held needs no move_ft");
    j = fb; j["effects"][0]["add_spellcasting_modifier"] = true; bad(j, "summon_weapon: held takes no add_spellcasting_modifier");
    j = fb; j["effects"][0]["light"]["bright_ft"] = 6; bad(j, "effect summon_weapon light.bright_ft and dim_ft must be positive multiples of 5");
    j = fb; j["effects"][0]["held"] = "yes"; bad(j, "effect summon_weapon held must be a boolean");
    j = fb; j["effects"][0].erase("held"); j["effects"][0].erase("light");
    auto floating = spell_from_json(j); INFO(floating.error); REQUIRE(floating.def);
    CHECK_FALSE(floating.def->summon_weapon.held); CHECK(floating.def->summon_weapon.move_ft == 20);

    const char* shape = "effect held_object must be {save: con, on_fail: drop, while_held: {disadvantage: [attack_rolls, ability_checks], until: start_of_caster_next_turn}}";
    auto hm = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"test.spell.zh_heat_metal","level":2,"name":"Heat Metal",
        "casting_time":{"kind":"action"},"range":{"kind":"ranged","feet":60},"components":{"v":true,"s":true,"m":{"text":"a piece of iron"}},
        "duration":{"kind":"minutes","amount":1,"concentration":true},
        "effects":[{"kind":"sustained_damage","damage":{"dice":"2d8","type":"fire"},
                    "held_object":{"save":"con","on_fail":"drop","while_held":{"disadvantage":["attack_rolls","ability_checks"],
                    "until":"start_of_caster_next_turn"}}}],"scaling":{"upcast":{"dice":1}}})");
    auto heat = spell_from_json(hm); INFO(heat.error); REQUIRE(heat.def);
    CHECK(heat.def->kind == SpellEffectKind::SustainedDamage); CHECK(heat.def->held_object);
    CHECK(heat.def->dice == DiceExpr{2, 8, 0}); CHECK(heat.def->upcast_dice == 1);
    CHECK(heat.def->expiry == 1); CHECK(heat.def->duration_rounds == 10);
    j = hm; j["effects"][0]["held_object"]["while_held"]["disadvantage"] = {"ability_checks", "attack_rolls"};
    auto flipped = spell_from_json(j); INFO(flipped.error); REQUIRE(flipped.def); CHECK(flipped.def->held_object);
    j = hm; j["effects"][0]["held_object"]["save"] = "dex"; bad(j, shape);
    j = hm; j["duration"] = {{"kind", "until_dispelled"}, {"concentration", true}};
    bad(j, "sustained_damage needs a rounds, minutes or hours duration");
    j = hm; j["effects"][0]["kind"] = "delayed_damage"; j["effects"][0]["expiry"] = {{"kind", "until_end_of_target_next_turn"}};
    bad(j, "effect held_object is only for sustained_damage");
}

TEST_CASE("loader 7.2zi: stabilize, chosen-target advantage, bonus Dash, poison ward, hit_only halve, beacon, warding bond") {
    const auto root = std::filesystem::path(ENGINE_SOURCE_ROOT) / "tests/validate/fixtures/spell_batch_zi";
    auto load = [&](const char* name) {
        std::ifstream in(root / name);
        REQUIRE(in);
        const auto j = eng::assets::Json::parse(std::string(std::istreambuf_iterator<char>(in), {}));
        auto r = spell_from_json(j);
        INFO(name); INFO(r.error);
        REQUIRE(r.def);
        return r.def;
    };
    const auto spare = load("spare_the_dying.json");
    CHECK(spare->kind == SpellEffectKind::Stabilize);
    CHECK(spare->level == 0); CHECK(spare->range_ft == 5); CHECK_FALSE(spare->concentration);
    CHECK(spare->excluded_types == (bit(CreatureType::Undead) | bit(CreatureType::Construct)));
    CHECK(spare->components == (kComponentV | kComponentS));

    const auto strike = load("true_strike.json");
    CHECK(strike->kind == SpellEffectKind::ApplyModifier);
    CHECK(strike->concentration); CHECK(strike->duration == SpellDuration::Rounds); CHECK(strike->duration_amount == 1);
    CHECK(strike->range_ft == 30); CHECK(strike->components == kComponentS);
    CHECK(strike->modifier.stat == ModStat::AttackRoll);
    CHECK(strike->modifier.mode == RollMode::Advantage); CHECK(strike->modifier.consume == 1);
    CHECK(strike->modifier.applies_to == ModAppliesTo::Self);
    CHECK(strike->modifier.mark_scope == 1); CHECK(strike->modifier.on_next_turn == 1);
    CHECK(strike->modifier.expiry == 3); CHECK(strike->modifier.rounds == 0);

    const auto retreat = load("expeditious_retreat.json");
    CHECK(retreat->casting_time == CastingTime::BonusAction);
    CHECK(retreat->concentration); CHECK(retreat->range_ft == 0);
    CHECK(retreat->duration == SpellDuration::Minutes); CHECK(retreat->duration_amount == 10);
    CHECK(retreat->modifier.stat == ModStat::BonusDash);
    CHECK(retreat->modifier.applies_to == ModAppliesTo::Self);
    CHECK(retreat->modifier.expiry == 1); CHECK(retreat->modifier.rounds == 600);
    CHECK(retreat->dash_on_cast);

    const auto poison = load("protection_from_poison.json");
    CHECK(poison->kind == SpellEffectKind::EndCondition);
    CHECK_FALSE(poison->concentration);
    CHECK(poison->duration == SpellDuration::Hours); CHECK(poison->duration_amount == 1);
    CHECK(poison->ends_conditions == bit(Condition::Poisoned));
    REQUIRE(poison->extra_effects.size() == 2);
    CHECK(poison->extra_effects[0].modifier.stat == ModStat::Resistance);
    CHECK(poison->extra_effects[0].modifier.has_damage_type);
    CHECK(poison->extra_effects[0].modifier.damage_type == DamageType::Poison);
    CHECK(poison->extra_effects[0].modifier.rounds == 600);
    CHECK_FALSE(poison->extra_effects[0].affected_only);
    CHECK(poison->extra_effects[1].modifier.stat == ModStat::Save);
    CHECK(poison->extra_effects[1].modifier.mode == RollMode::Advantage);
    CHECK_FALSE(poison->extra_effects[1].modifier.has_ability);
    CHECK(poison->extra_effects[1].modifier.save_vs_conditions == bit(Condition::Poisoned));

    const auto ray = load("ray_of_enfeeblement.json");
    CHECK(ray->kind == SpellEffectKind::SpellAttack); CHECK(ray->hit_only); CHECK_FALSE(ray->melee);
    CHECK(ray->concentration); CHECK(ray->range_ft == 60);
    CHECK(ray->dice == DiceExpr{});
    REQUIRE(ray->extra_effects.size() == 1);
    CHECK(ray->extra_effects[0].affected_only);
    CHECK(ray->extra_effects[0].modifier.stat == ModStat::HalveWeaponDamage);
    CHECK(ray->extra_effects[0].modifier.scope_ability == 1);
    CHECK_FALSE(ray->extra_effects[0].modifier.has_ability);
    CHECK(ray->extra_effects[0].modifier.repeat_save == 1);
    CHECK(ray->extra_effects[0].modifier.repeat_ability == Ability::Con);
    CHECK(ray->extra_effects[0].modifier.rounds == 10);

    const auto beacon = load("beacon_of_hope.json");
    CHECK(beacon->targets == 255); CHECK(beacon->concentration); CHECK(beacon->range_ft == 30);
    CHECK(beacon->modifier.stat == ModStat::Save);
    CHECK(beacon->modifier.has_ability); CHECK(beacon->modifier.ability == Ability::Wis);
    CHECK(beacon->modifier.mode == RollMode::Advantage);
    CHECK(beacon->modifier.death_save_adv == 1); CHECK(beacon->modifier.heal_maximum == 1);
    CHECK(beacon->modifier.rounds == 10);

    const auto bond = load("warding_bond.json");
    CHECK_FALSE(bond->concentration);
    CHECK(bond->duration == SpellDuration::Hours); CHECK(bond->duration_amount == 1);
    CHECK(bond->range_ft == 5);
    CHECK((bond->components & kComponentM) != 0);
    CHECK(bond->modifier.stat == ModStat::Ac); CHECK(bond->modifier.bonus == 1); CHECK(bond->modifier.rounds == 600);
    REQUIRE(bond->extra_effects.size() == 3);
    CHECK(bond->extra_effects[0].modifier.stat == ModStat::Save);
    CHECK(bond->extra_effects[0].modifier.bonus == 1); CHECK_FALSE(bond->extra_effects[0].modifier.has_ability);
    CHECK(bond->extra_effects[1].modifier.stat == ModStat::Resistance);
    CHECK(bond->extra_effects[1].modifier.resist_all == 1);
    CHECK_FALSE(bond->extra_effects[1].modifier.has_damage_type);
    CHECK(bond->extra_effects[2].modifier.stat == ModStat::ShareDamage);
    CHECK(bond->extra_effects[2].modifier.end_beyond_ft == 60);
    CHECK(bond->extra_effects[2].modifier.end_caster_at_zero == 1);

    auto bad = [](eng::assets::Json x, const std::string& want) {
        auto q = spell_from_json(x); INFO(want); INFO(q.error); CHECK_FALSE(q.def); CHECK(q.error == want);
    };
    auto doc = [&](const char* name) {
        std::ifstream in(root / name);
        REQUIRE(in);
        return eng::assets::Json::parse(std::string(std::istreambuf_iterator<char>(in), {}));
    };
    auto j = doc("ray_of_enfeeblement.json");
    j["effects"][0]["damage"] = {{"dice", "1d8"}, {"type", "necrotic"}};
    bad(j, "effect hit_only and damage together");
    j = doc("spare_the_dying.json");
    j["effects"] = eng::assets::Json::array({{{"kind", "heal"}, {"dice", "1d4"}}, j["effects"][0]});
    bad(j, "effects[1]: stabilize is only allowed as the first effect");
    j = doc("true_strike.json");
    j["effects"][0]["modifier"]["stat"] = "bonus_dash";
    j["effects"][0]["modifier"].erase("mode"); j["effects"][0]["modifier"].erase("consume");
    j["effects"][0]["modifier"].erase("applies_to"); j["effects"][0].erase("scope");
    bad(j, "effect on_next_turn needs an attack_roll modifier");
    j = doc("expeditious_retreat.json");
    j["effects"][0]["modifier"].erase("applies_to");
    bad(j, "effect dash_on_cast needs a bonus_dash modifier on self");
    j = doc("expeditious_retreat.json");
    j["effects"][0]["dash_on_cast"] = false;
    bad(j, "effect dash_on_cast must be true");
    j = doc("ray_of_enfeeblement.json");
    j["effects"] = eng::assets::Json::array({j["effects"][1]});
    j["effects"][0]["modifier"]["ability"] = "dex";
    bad(j, "effect halve_weapon_damage needs ability str");
    j = doc("warding_bond.json");
    j["effects"] = eng::assets::Json::array({j["effects"][2]});
    j["effects"][0]["modifier"]["damage_type"] = "poison";
    bad(j, "effect modifier all_damage and damage_type together");
    j = doc("warding_bond.json");
    j["effects"] = eng::assets::Json::array({j["effects"][3]});
    j["effects"][0]["end_beyond_ft"] = 7;
    bad(j, "effect end_beyond_ft must be a positive multiple of 5");
    j = doc("protection_from_poison.json");
    j["effects"] = eng::assets::Json::array({j["effects"][2]});
    j["effects"][0]["modifier"]["stat"] = "attack_roll";
    j["effects"][0]["modifier"].erase("applies_to");
    bad(j, "effect save_vs_conditions needs a save modifier with a mode");
}
