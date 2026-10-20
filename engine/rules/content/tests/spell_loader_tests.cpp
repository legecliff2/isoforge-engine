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
                                 expect.find("polymorph") == 0;   // R-G12 (section 9)
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
    j = hp; j["components"]["m"] = {{"text", "x"}, {"consumed", true}}; bad(j, "cost_cp");
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
    CHECK(r.book->all().size() == 8);
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
