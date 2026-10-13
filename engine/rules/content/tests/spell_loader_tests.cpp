#include <doctest/doctest.h>

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
}

TEST_CASE("loader rejects inconsistent spells") {
    auto base = eng::assets::Json::parse(R"({"$schema":"spell@1","id":"srd51.spell.x","level":1,
        "casting_time":{"kind":"action"},"range":{"kind":"self"},"components":{"v":true},
        "duration":{"kind":"instantaneous"},"effect":{"kind":"ac_bonus","bonus":5,"until":"start_of_caster_turn"}})");
    CHECK_FALSE(spell_from_json(base).def);   // ac_bonus must be a reaction
    base["casting_time"] = {{"kind", "reaction"}, {"trigger", "spell_cast"}};
    CHECK_FALSE(spell_from_json(base).def);   // wrong trigger
    base["casting_time"]["trigger"] = "being_hit";
    CHECK(spell_from_json(base).def);
    base["scaling"] = {{"cantrip", {{"levels", {5}}}}};
    CHECK_FALSE(spell_from_json(base).def);   // cantrip scaling on a leveled spell
    base.erase("scaling");
    base["components"] = eng::assets::Json::object();
    CHECK_FALSE(spell_from_json(base).def);
}
