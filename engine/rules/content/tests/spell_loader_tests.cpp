#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include "rules5e_content/spell_loader.hpp"

using namespace rules5e;
using namespace rules5e_content;

namespace {
const std::filesystem::path kSpells = std::filesystem::path(ENGINE_SOURCE_ROOT) / "content/packs/sample.vertical_slice/spells";
}

TEST_CASE("sample pack spells load into a SpellBook") {
    auto r = load_spellbook(kSpells);
    REQUIRE(r.errors.empty());
    REQUIRE(r.book);
    CHECK(r.book->all().size() == 6);

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
                                 expect.find("is not a spell@1") == 0 || expect.find("'") == 0 || expect.find("until_start") == 0;
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
    j = fb; j["effects"].push_back(fb["effects"][0]); bad(j, "exactly one effect");
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
