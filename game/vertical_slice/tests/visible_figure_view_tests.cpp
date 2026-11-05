// Session consumer for a visible figure. Body art stays the true form. Covered items are not drawn.
#include <doctest/doctest.h>

#include <vector>

#include "equip_visuals.hpp"
#include "rules5e/encounter.hpp"
#include "session.hpp"

using namespace eng;
using namespace slice;

TEST_CASE("visible figure view: shape, size and covered equipment, true-form body art") {
    rules5e::EncounterState st;
    rules5e::VisibleFigure fig;
    fig.instance = 4;
    fig.bearer = 2;
    fig.shape = rules5e::FigureShape::Humanoid;
    fig.size = rules5e::Size::Medium;
    fig.armor_def = 9;
    fig.covered_items = {10};
    st.figures.push_back(fig);
    st.figure_discerned.push_back({1, 2});

    ActorView hag;
    hag.id = 2;
    hag.name = "HAG";
    hag.kit = "standin.medium";
    hag.visual_kind = "standin";
    hag.standin_bin = "medium";
    EquipVisual sword;
    sword.item = 10;
    sword.slot = AttachSlot::MainHand;
    sword.kind = EquipKind::Weapon;
    sword.content_id = "srd51.item.longsword";
    EquipVisual bow;
    bow.item = 11;
    bow.slot = AttachSlot::Back;
    bow.kind = EquipKind::Weapon;
    hag.equipment = {sword, bow};

    apply_visible_figure(st, 1, hag);
    CHECK(hag.illusory_figure);
    CHECK(hag.figure_shape == uint8_t(rules5e::FigureShape::Humanoid));
    CHECK(hag.figure_size == uint8_t(rules5e::Size::Medium));
    CHECK(hag.covered_armor_def == 9);
    CHECK(hag.covered_equipment == std::vector<uint32_t>{10});
    CHECK(hag.illusion_discerned);
    CHECK(hag.kit == "standin.medium");
    CHECK(hag.visual_kind == "standin");
    CHECK(hag.standin_bin == "medium");
    CHECK(hag.name == "HAG");
    const auto layers = equip_layers(hag);
    REQUIRE(layers.size() == 2);
    CHECK(layers[0].part == EquipPart::Back);
    CHECK(layers[0].item->item == 11);
    CHECK(layers[1].part == EquipPart::Body);

    ActorView other = hag;
    apply_visible_figure(st, 3, other);
    CHECK(other.illusory_figure);
    CHECK_FALSE(other.illusion_discerned);
    CHECK(other.kit == "standin.medium");
}
