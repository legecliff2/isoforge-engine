#!/usr/bin/env python3
"""srd51.core weapons / armor / shields against an independent data table (ctest: content_validate_equipment).

The tables below were generated once from 5e-bits/5e-database @ a6212be and cross-checked row by row against
the SRD weapon/armor tables (srd_content_inventory.md section 2); the pack files must keep matching them.
Units: cost in copper pieces, weight in ounces (SRD pounds x 16). Reach weapons are 10 ft; a thrown melee
weapon keeps reach 5 and gains range [thrown normal, long]; a pure ranged weapon has range and no reach;
Blowgun is flat 1 piercing; Net has no damage and 'special'.
"""
from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACK = ROOT / "content/packs/srd51.core"
ITEMS = PACK / "items"

# (local_id, weapon_class, reach, range, damage "dice type" | None, versatile "dice type" | None, properties, cost_cp, weight_oz)
WEAPONS = [
    ('battleaxe', 'martial', 5, None, '1d8 slashing', '1d10 slashing', ('versatile',), 1000, 64),
    ('blowgun', 'martial', None, (25, 100), '1 piercing', None, ('ammunition', 'loading'), 1000, 16),
    ('club', 'simple', 5, None, '1d4 bludgeoning', None, ('light',), 10, 32),
    ('crossbow_hand', 'martial', None, (30, 120), '1d6 piercing', None, ('ammunition', 'light', 'loading'), 7500, 48),
    ('crossbow_heavy', 'martial', None, (100, 400), '1d10 piercing', None, ('ammunition', 'heavy', 'loading', 'two_handed'), 5000, 288),
    ('crossbow_light', 'simple', None, (80, 320), '1d8 piercing', None, ('ammunition', 'loading', 'two_handed'), 2500, 80),
    ('dagger', 'simple', 5, (20, 60), '1d4 piercing', None, ('finesse', 'light', 'thrown'), 200, 16),
    ('dart', 'simple', None, (20, 60), '1d4 piercing', None, ('finesse', 'thrown'), 5, 4),
    ('flail', 'martial', 5, None, '1d8 bludgeoning', None, (), 1000, 32),
    ('glaive', 'martial', 10, None, '1d10 slashing', None, ('heavy', 'reach', 'two_handed'), 2000, 96),
    ('greataxe', 'martial', 5, None, '1d12 slashing', None, ('heavy', 'two_handed'), 3000, 112),
    ('greatclub', 'simple', 5, None, '1d8 bludgeoning', None, ('two_handed',), 20, 160),
    ('greatsword', 'martial', 5, None, '2d6 slashing', None, ('heavy', 'two_handed'), 5000, 96),
    ('halberd', 'martial', 10, None, '1d10 slashing', None, ('heavy', 'reach', 'two_handed'), 2000, 96),
    ('handaxe', 'simple', 5, (20, 60), '1d6 slashing', None, ('light', 'thrown'), 500, 32),
    ('javelin', 'simple', 5, (30, 120), '1d6 piercing', None, ('thrown',), 50, 32),
    ('lance', 'martial', 10, None, '1d12 piercing', None, ('lance', 'reach', 'special'), 1000, 96),
    ('light_hammer', 'simple', 5, (20, 60), '1d4 bludgeoning', None, ('light', 'thrown'), 200, 32),
    ('longbow', 'martial', None, (150, 600), '1d8 piercing', None, ('ammunition', 'heavy', 'two_handed'), 5000, 32),
    ('longsword', 'martial', 5, None, '1d8 slashing', '1d10 slashing', ('versatile',), 1500, 48),
    ('mace', 'simple', 5, None, '1d6 bludgeoning', None, (), 500, 64),
    ('maul', 'martial', 5, None, '2d6 bludgeoning', None, ('heavy', 'two_handed'), 1000, 160),
    ('morningstar', 'martial', 5, None, '1d8 piercing', None, (), 1500, 64),
    ('net', 'martial', None, (5, 15), None, None, ('special', 'thrown'), 100, 48),
    ('pike', 'martial', 10, None, '1d10 piercing', None, ('heavy', 'reach', 'two_handed'), 500, 288),
    ('quarterstaff', 'simple', 5, None, '1d6 bludgeoning', '1d8 bludgeoning', ('versatile',), 20, 64),
    ('rapier', 'martial', 5, None, '1d8 piercing', None, ('finesse',), 2500, 32),
    ('scimitar', 'martial', 5, None, '1d6 slashing', None, ('finesse', 'light'), 2500, 48),
    ('shortbow', 'simple', None, (80, 320), '1d6 piercing', None, ('ammunition', 'two_handed'), 2500, 32),
    ('shortsword', 'martial', 5, None, '1d6 piercing', None, ('finesse', 'light'), 1000, 32),
    ('sickle', 'simple', 5, None, '1d4 slashing', None, ('light',), 100, 32),
    ('sling', 'simple', None, (30, 120), '1d4 bludgeoning', None, ('ammunition',), 10, 0),
    ('spear', 'simple', 5, (20, 60), '1d6 piercing', '1d8 piercing', ('thrown', 'versatile'), 100, 48),
    ('trident', 'martial', 5, (20, 60), '1d6 piercing', '1d8 piercing', ('thrown', 'versatile'), 500, 64),
    ('war_pick', 'martial', 5, None, '1d8 piercing', None, (), 500, 32),
    ('warhammer', 'martial', 5, None, '1d8 bludgeoning', '1d10 bludgeoning', ('versatile',), 1500, 32),
    ('whip', 'martial', 10, None, '1d4 slashing', None, ('finesse', 'reach'), 200, 48),
]
# item material (Heat Metal, CONTENT_CONVENTIONS section 22). A weapon with a metal head or blade is manufactured
# metal, so the spear and the javelin are metal, with the pike, halberd, glaive, lance and the crossbows. All-wood or
# non-metal gear stays non-metal: club, greatclub, quarterstaff, shortbow, longbow, sling, blowgun, a dart only when it
# is all wood (this dart is metal), whip (leather), net (rope), and padded / leather / studded leather / hide armor.
# The generic SRD shield is metal (the SRD allows wood or metal; metal is the authoring default). Absent is not a
# value here: every srd51 weapon, armor and shield sets one.
MATERIAL = {
    'battleaxe': 'metal', 'blowgun': 'wood', 'club': 'wood', 'crossbow_hand': 'metal', 'crossbow_heavy': 'metal',
    'crossbow_light': 'metal', 'dagger': 'metal', 'dart': 'metal', 'flail': 'metal', 'glaive': 'metal',
    'greataxe': 'metal', 'greatclub': 'wood', 'greatsword': 'metal', 'halberd': 'metal', 'handaxe': 'metal',
    'javelin': 'metal', 'lance': 'metal', 'light_hammer': 'metal', 'longbow': 'wood', 'longsword': 'metal',
    'mace': 'metal', 'maul': 'metal', 'morningstar': 'metal', 'net': 'rope', 'pike': 'metal',
    'quarterstaff': 'wood', 'rapier': 'metal', 'scimitar': 'metal', 'shortbow': 'wood', 'shortsword': 'metal',
    'sickle': 'metal', 'sling': 'leather', 'spear': 'metal', 'trident': 'metal', 'war_pick': 'metal',
    'warhammer': 'metal', 'whip': 'leather',
    'breastplate': 'metal', 'chain_mail': 'metal', 'chain_shirt': 'metal', 'half_plate_armor': 'metal',
    'hide_armor': 'hide', 'leather_armor': 'leather', 'padded_armor': 'cloth', 'plate_armor': 'metal',
    'ring_mail': 'metal', 'scale_mail': 'metal', 'shield': 'metal', 'splint_armor': 'metal',
    'studded_leather_armor': 'leather',
}
# (local_id, armor_category | 'shield', base_ac (shield: ac_bonus), dex_bonus, dex_cap, str_min, stealth_disadvantage, cost_cp, weight_oz)
ARMOR = [
    ('breastplate', 'medium', 14, True, 2, 0, False, 40000, 320),
    ('chain_mail', 'heavy', 16, False, None, 13, True, 7500, 880),
    ('chain_shirt', 'medium', 13, True, 2, 0, False, 5000, 320),
    ('half_plate_armor', 'medium', 15, True, 2, 0, True, 75000, 640),
    ('hide_armor', 'medium', 12, True, 2, 0, False, 1000, 192),
    ('leather_armor', 'light', 11, True, None, 0, False, 1000, 160),
    ('padded_armor', 'light', 11, True, None, 0, True, 500, 128),
    ('plate_armor', 'heavy', 18, False, None, 15, True, 150000, 1040),
    ('ring_mail', 'heavy', 14, False, None, 0, True, 3000, 640),
    ('scale_mail', 'medium', 14, True, 2, 0, True, 5000, 720),
    ('shield', 'shield', 2, None, None, 0, False, 1000, 96),
    ('splint_armor', 'heavy', 17, False, None, 15, True, 20000, 960),
    ('studded_leather_armor', 'light', 12, True, None, 0, False, 4500, 208),
]

# (local_id, category, description must mention): valuables (item@1 category 'valuable'). The SRD equipment list at a6212be has
# no gems (no 'Diamond' in 5e-SRD-Equipment.json: categories weapon, armor, adventuring-gear, tools, mounts-and-vehicles), so
# the diamond is based on the SRD spell material text (Revivify, Raise Dead, ...) and carries no cost_cp / weight_oz: each
# character@4 equipment[] entry gives its own value_gp (docs/CONTENT_CONVENTIONS.md section 7).
VALUABLES = [
    ('diamond', 'valuable', 'Revivify'),
]

# Ammunition weapons are item@2 with ammo_kind (EQUIPMENT 2.5); ammunition is item@2 'ammunition', one document per piece.
# 5e-database a6212be sells by the bundle: arrows / bolts 1 gp per 20 (5 cp each), needles 1 gp per 50 (2 cp each), sling
# bullets 4 cp per 20 (0.2 cp: no integer cost_cp). No per-piece weight is a whole number of ounces, so weight_oz is omitted.
AMMO_KIND = {'blowgun': 'needle', 'crossbow_hand': 'bolt', 'crossbow_heavy': 'bolt', 'crossbow_light': 'bolt',
             'longbow': 'arrow', 'shortbow': 'arrow', 'sling': 'sling_bullet'}
AMMUNITION = [('arrow', 'arrow', 5), ('crossbow_bolt', 'bolt', 5), ('sling_bullet', 'sling_bullet', None), ('blowgun_needle', 'needle', 2)]

# SRD 5.1 ammunition containers (5e-database a6212be, adventuring gear; the SRD has no blowgun needle case: the Pouch holds
# "20 sling bullets or 50 blowgun needles"): (local_id, cost_cp, weight_oz, holds [(ammo_kind, capacity)], SRD sentence)
CONTAINERS = [
    ('quiver', 100, 16, [('arrow', 20)], "A quiver can hold up to 20 arrows."),
    ('case_crossbow_bolt', 100, 16, [('bolt', 20)], "This wooden case can hold up to twenty crossbow bolts."),
    ('pouch', 50, 16, [('sling_bullet', 20), ('needle', 50)], "A cloth or leather pouch can hold up to 20 sling bullets or 50 blowgun needles"),
]

# item@2 light gear (docs/proposals/EQUIPMENT.md 6.2), from 5e-database a6212be 5e-SRD-Equipment.json (adventuring-gear):
# (local_id, cost_cp, weight_oz, hands, light (bright_ft, dim_ft, shape, duration_rounds, fuel, states) | None, torch_attack)
# 1 hour = 600 rounds, 6 hours per flask = 3600. Descriptions are the SRD text verbatim (paragraphs joined by a blank line).
# Inert until equipment produces light (#244): the sim does not load item documents.
OIL = "srd51.item.oil_flask"
LIGHT_GEAR = [
    ('torch', 1, 16, 1, (20, 20, 'radius', 600, None, []), [{"flat": 1, "type": "fire"}]),
    ('candle', 1, 0, 1, (5, 5, 'radius', 600, None, []), None),
    ('lamp', 50, 16, 1, (15, 30, 'radius', 3600, OIL, []), None),
    ('lantern_hooded', 500, 32, 1, (30, 30, 'radius', 3600, OIL, [{"id": "hooded", "bright_ft": 0, "dim_ft": 5, "toggle": "action"}]), None),
    ('lantern_bullseye', 1000, 32, 1, (60, 60, 'cone', 3600, OIL, []), None),
    ('oil_flask', 10, 16, None, None, None),
    ('tinderbox', 50, 16, None, None, None),
]
SRD_DESC = {   # exact SRD wording the descriptions must contain
    'torch': "A torch burns for 1 hour, providing bright light in a 20-foot radius and dim light for an additional 20 feet.",
    'candle': "For 1 hour, a candle sheds bright light in a 5-foot radius and dim light for an additional 5 feet.",
    'lamp': "Once lit, it burns for 6 hours on a flask (1 pint) of oil.",
    'lantern_hooded': "As an action, you can lower the hood, reducing the light to dim light in a 5-foot radius.",
    'lantern_bullseye': "A bullseye lantern casts bright light in a 60-foot cone and dim light for an additional 60 feet.",
    'oil_flask': "Oil usually comes in a clay flask that holds 1 pint.",
    'tinderbox': "Using it to light a torch--or anything else with abundant, exposed fuel--takes an action.",
}


# SRD 5.1 utility adventuring gear (5e-database a6212be, standard-gear), item@2 'gear' with passive data only:
# (local_id, cost_cp, weight_oz, SRD sentence the description must contain | None when the SRD has no description).
# No mechanics: rope / chain hit points and burst DCs, crowbar advantage and manacle escape DCs are description text only.
UTILITY_GEAR = [
    ('backpack', 200, 80, None),
    ('bedroll', 100, 112, None),
    ('rope_hempen_50_feet', 100, 160, "has 2 hit points and can be burst with a DC 17 Strength check."),
    ('rope_silk_50_feet', 1000, 80, "has 2 hit points and can be burst with a DC 17 Strength check."),
    ('waterskin', 20, 80, None),
    ('rations_1_day', 50, 32, "Rations consist of dry foods suitable for extended travel"),
    ('crowbar', 200, 80, "Using a crowbar grants advantage to Strength checks"),
    ('pole_10_foot', 5, 112, None),
    ('piton', 5, 4, None),
    ('hammer', 100, 48, None),
    ('grappling_hook', 200, 64, None),
    ('mirror_steel', 500, 8, None),
    ('chain_10_feet', 500, 160, "A chain has 10 hit points."),
    ('manacles', 200, 96, "Escaping the manacles requires a successful DC 20 Dexterity check."),
]

# SRD 5.1 spellcasting foci, component pouch and spellbook (5e-database a6212be: arcane-foci, druidic-foci, holy-symbols,
# standard-gear), item@2 'gear', passive data only: (local_id, cost_cp, weight_oz, SRD sentence the description must contain).
# The SRD prices each focus form separately, so each is its own item, id prefixed by its focus family. The item@2 schema has no
# focus marker yet, so nothing here is a rules5e ItemKind::Focus (deferred); characters keep their descriptive strings.
FOCI = [
    ('component_pouch', 2500, 32, "A component pouch is a small, watertight leather belt pouch"),
    ('spellbook', 5000, 48, "a spellbook is a leather-bound tome with 100 blank vellum pages"),
    ('arcane_focus_crystal', 1000, 16, "An arcane focus is a special item"),
    ('arcane_focus_orb', 2000, 48, "An arcane focus is a special item"),
    ('arcane_focus_rod', 1000, 32, "An arcane focus is a special item"),
    ('arcane_focus_staff', 500, 64, "An arcane focus is a special item"),
    ('arcane_focus_wand', 1000, 16, "An arcane focus is a special item"),
    ('druidic_focus_sprig_of_mistletoe', 100, 0, "A druid can use such an object as a spellcasting focus."),
    ('druidic_focus_totem', 100, 0, "A druid can use such an object as a spellcasting focus."),
    ('druidic_focus_wooden_staff', 500, 64, "A druid can use such an object as a spellcasting focus."),
    ('druidic_focus_yew_wand', 1000, 16, "A druid can use such an object as a spellcasting focus."),
    ('holy_symbol_amulet', 500, 16, "the caster must hold it in hand, wear it visibly, or bear it on a shield."),
    ('holy_symbol_emblem', 500, 0, "the caster must hold it in hand, wear it visibly, or bear it on a shield."),
    ('holy_symbol_reliquary', 500, 32, "the caster must hold it in hand, wear it visibly, or bear it on a shield."),
]

FOCUS_FAMILY = {f[0]: f[0].split("_")[0] if f[0].startswith(("arcane_", "druidic_")) else "holy"
                for f in FOCI if f[0].startswith(("arcane_focus_", "druidic_focus_", "holy_symbol_"))}


# SRD 5.1 tools (5e-database a6212be, equipment_category 'tools'), item@2 'gear' with tool_kind = the SRD tool_category
# (docs/CONTENT_CONVENTIONS.md section 18; inert, no tool-proficiency checks): (local_id, tool_kind, cost_cp, weight_oz).
# SRD 5.1 has no disguise, forgery, herbalism or poisoner's kit (not in 5e-database's SRD equipment): none is authored.
TOOL_KINDS = ("artisans_tools", "gaming_set", "musical_instrument", "other_tools")
TOOLS = [
    ('alchemists_supplies', 'artisans_tools', 5000, 128),
    ('brewers_supplies', 'artisans_tools', 2000, 144),
    ('calligraphers_supplies', 'artisans_tools', 1000, 80),
    ('carpenters_tools', 'artisans_tools', 800, 96),
    ('cartographers_tools', 'artisans_tools', 1500, 96),
    ('cobblers_tools', 'artisans_tools', 500, 80),
    ('cooks_utensils', 'artisans_tools', 100, 128),
    ('glassblowers_tools', 'artisans_tools', 3000, 80),
    ('jewelers_tools', 'artisans_tools', 2500, 32),
    ('leatherworkers_tools', 'artisans_tools', 500, 80),
    ('masons_tools', 'artisans_tools', 1000, 128),
    ('painters_supplies', 'artisans_tools', 1000, 80),
    ('potters_tools', 'artisans_tools', 1000, 48),
    ('smiths_tools', 'artisans_tools', 2000, 128),
    ('tinkers_tools', 'artisans_tools', 5000, 160),
    ('weavers_tools', 'artisans_tools', 100, 80),
    ('woodcarvers_tools', 'artisans_tools', 100, 80),
]
TOOL_SENTENCE = {
    'artisans_tools': "Proficiency with a set of artisan's tools lets you add your proficiency bonus",
}


def load(lid):
    return json.loads((ITEMS / f"{lid}.json").read_text(encoding="utf-8"))


def dmg_text(entry):
    return f"{entry.get('dice', entry.get('flat'))} {entry['type']}"


class Srd51Equipment(unittest.TestCase):
    def test_counts(self):
        self.assertEqual(len(WEAPONS), 37)
        self.assertEqual(sum(1 for w in WEAPONS if w[1] == "simple"), 14)
        self.assertEqual(sum(1 for w in WEAPONS if w[1] == "martial"), 23)
        self.assertEqual(len(ARMOR), 13)
        self.assertEqual(sum(1 for a in ARMOR if a[1] == "light"), 3)
        self.assertEqual(sum(1 for a in ARMOR if a[1] == "medium"), 5)
        self.assertEqual(sum(1 for a in ARMOR if a[1] == "heavy"), 4)
        self.assertEqual(sum(1 for a in ARMOR if a[1] == "shield"), 1)
        self.assertEqual(len(VALUABLES), 1)
        files = sorted(p.stem for p in ITEMS.glob("*.json"))
        self.assertEqual(len(AMMUNITION), 4)
        self.assertEqual(len(LIGHT_GEAR), 7)
        self.assertEqual(len(CONTAINERS), 3)
        self.assertEqual(len(UTILITY_GEAR), 14)
        self.assertEqual(len(FOCI), 14)
        self.assertEqual(len(TOOLS), 17)
        self.assertEqual(files, sorted([w[0] for w in WEAPONS] + [a[0] for a in ARMOR] + [v[0] for v in VALUABLES]
                                       + [a[0] for a in AMMUNITION] + [g[0] for g in LIGHT_GEAR] + [c[0] for c in CONTAINERS]
                                       + [g[0] for g in UTILITY_GEAR] + [f[0] for f in FOCI]
                                       + [t[0] for t in TOOLS]),
                         "no extra or missing item files")

    def test_manifest_lists_every_item(self):
        man = json.loads((PACK / "manifest.json").read_text(encoding="utf-8"))
        listed = [c for c in man["contents"] if c.startswith("items/")]
        self.assertEqual(listed, sorted(f"items/{p.name}" for p in ITEMS.glob("*.json")))
        self.assertGreaterEqual(man["api_level"], 2, "item@1 documents need api_level >= 2 (VERSION_BUMP_BATCH 8.5)")

    def test_weapons(self):
        for lid, cls, reach, rng, dmg, vers, props, cost, oz in WEAPONS:
            with self.subTest(lid):
                d = load(lid)
                self.assertEqual((d["$schema"], d["id"], d["category"]),
                                 ("item@2" if lid in AMMO_KIND else "item@1", f"srd51.item.{lid}", "weapon"))
                self.assertEqual(d.get("ammo_kind"), AMMO_KIND.get(lid))
                self.assertEqual("ammunition" in props, lid in AMMO_KIND)
                self.assertEqual(d["weapon_class"], cls)
                self.assertEqual(d.get("reach"), reach)
                self.assertEqual(tuple(d["range"]) if "range" in d else None, rng)
                self.assertEqual(dmg_text(d["damage"][0]) if "damage" in d else None, dmg)
                self.assertEqual(dmg_text(d["versatile_damage"][0]) if "versatile_damage" in d else None, vers)
                self.assertEqual(tuple(d.get("properties", [])), props)
                self.assertEqual((d["cost_cp"], d["weight_oz"]), (cost, oz))
                self.assertEqual(d["material"], MATERIAL[lid])
                self.assertEqual(reach == 10, "reach" in props)
                self.assertEqual("versatile" in props, vers is not None)
                self.assertEqual("damage" in d, "special" not in props or lid == "lance")

    def test_armor_and_shield(self):
        for lid, cat, ac, dex, cap, strmin, stealth, cost, oz in ARMOR:
            with self.subTest(lid):
                d = load(lid)
                self.assertEqual((d["$schema"], d["id"]), ("item@1", f"srd51.item.{lid}"))
                self.assertEqual((d["cost_cp"], d["weight_oz"]), (cost, oz))
                self.assertEqual(d["material"], MATERIAL[lid])
                if cat == "shield":
                    self.assertEqual((d["category"], d["ac_bonus"]), ("shield", ac))
                    continue
                self.assertEqual(d["category"], "armor")
                self.assertEqual((d["armor_category"], d["base_ac"], d["dex_bonus"]), (cat, ac, dex))
                self.assertEqual(d.get("dex_cap"), cap)
                self.assertEqual(d.get("str_min", 0), strmin)
                self.assertEqual(d["stealth_disadvantage"], stealth)

    def test_valuables(self):
        for lid, cat, mention in VALUABLES:
            with self.subTest(lid):
                d = load(lid)
                self.assertEqual((d["$schema"], d["id"], d["category"]), ("item@1", f"srd51.item.{lid}", cat))
                self.assertEqual(d["source"], {"ref": "SRD 5.1", "license": "CC-BY-4.0"})
                self.assertNotIn("cost_cp", d)    # no list price: the equipment entry's value_gp is the value
                self.assertNotIn("weight_oz", d)  # the SRD gives none
                self.assertIn(mention, d["description"])
                self.assertEqual(set(d), {"$schema", "id", "name", "source", "category", "description"})

    def test_ammunition(self):
        for lid, kind, cost in AMMUNITION:
            with self.subTest(lid):
                d = load(lid)
                self.assertEqual((d["$schema"], d["id"], d["category"], d["ammo_kind"]), ("item@2", f"srd51.item.{lid}", "ammunition", kind))
                self.assertEqual(d.get("cost_cp"), cost)
                self.assertNotIn("weight_oz", d)
        self.assertEqual(set(AMMO_KIND.values()), {a[1] for a in AMMUNITION})

    def test_containers(self):
        for lid, cost, oz, holds, sentence in CONTAINERS:
            with self.subTest(lid):
                d = load(lid)
                self.assertEqual((d["$schema"], d["id"], d["category"]), ("item@2", f"srd51.item.{lid}", "gear"))
                self.assertEqual((d["cost_cp"], d["weight_oz"]), (cost, oz))
                self.assertEqual([(h["ammo_kind"], h["capacity"]) for h in d["container"]["holds"]], holds)
                self.assertIn(sentence, d["description"])
                self.assertNotIn("light", d)
        self.assertEqual({h[0] for c in CONTAINERS for h in c[3]}, {a[1] for a in AMMUNITION})   # every ammo kind has one

    def test_light_gear(self):
        for lid, cost, oz, hands, light, torch in LIGHT_GEAR:
            with self.subTest(lid):
                d = load(lid)
                self.assertEqual((d["$schema"], d["id"], d["category"]), ("item@2", f"srd51.item.{lid}", "gear"))
                self.assertEqual(d["source"], {"ref": "SRD 5.1", "license": "CC-BY-4.0"})
                self.assertEqual((d["cost_cp"], d["weight_oz"], d.get("hands")), (cost, oz, hands))
                self.assertIn(SRD_DESC[lid], d["description"])
                if light is None:
                    self.assertNotIn("light", d)
                else:
                    lt = d["light"]
                    self.assertEqual((lt["bright_ft"], lt["dim_ft"], lt["shape"], lt["duration_rounds"], lt.get("fuel"),
                                      lt.get("states", [])), light)
                    self.assertIs(lt["lit_by_default"], False)   # lighting takes an action (Tinderbox)
                    # Render preset key (#233 / #255 GlowPalette: 'srd51.core.light.<name>', last segment matched)
                    self.assertEqual(lt["preset"], f"srd51.core.light.{lid}")
                self.assertEqual(d["torch_attack"]["damage"] if "torch_attack" in d else None, torch)
                if torch:
                    self.assertIs(d["torch_attack"]["requires_lit"], True)
        self.assertIn(OIL, {f"srd51.item.{g[0]}" for g in LIGHT_GEAR})

    def test_utility_gear(self):
        for lid, cost, oz, sentence in UTILITY_GEAR:
            with self.subTest(lid):
                d = load(lid)
                self.assertEqual((d["$schema"], d["id"], d["category"]), ("item@2", f"srd51.item.{lid}", "gear"))
                self.assertEqual(d["source"], {"ref": "SRD 5.1", "license": "CC-BY-4.0"})
                self.assertEqual((d["cost_cp"], d["weight_oz"]), (cost, oz))
                if sentence is None:
                    self.assertNotIn("description", d)
                else:
                    self.assertIn(sentence, d["description"])
                self.assertEqual(set(d) - {"description"}, {"$schema", "id", "name", "source", "category", "cost_cp", "weight_oz"})

    def test_foci(self):
        for lid, cost, oz, sentence in FOCI:
            with self.subTest(lid):
                d = load(lid)
                self.assertEqual((d["$schema"], d["id"], d["category"]), ("item@2", f"srd51.item.{lid}", "gear"))
                self.assertEqual(d["source"], {"ref": "SRD 5.1", "license": "CC-BY-4.0"})
                self.assertEqual((d["cost_cp"], d["weight_oz"]), (cost, oz))
                self.assertIn(sentence, d["description"])
                self.assertEqual(d.get("focus"), FOCUS_FAMILY.get(lid))
                self.assertEqual(set(d) - {"focus"}, {"$schema", "id", "name", "source", "category", "cost_cp", "weight_oz", "description"})
        fams = [FOCUS_FAMILY[f[0]] for f in FOCI if f[0] in FOCUS_FAMILY]
        self.assertEqual((fams.count("arcane"), fams.count("druidic"), fams.count("holy")), (5, 4, 3))
        self.assertNotIn("component_pouch", FOCUS_FAMILY)   # a pouch is not a focus (it holds the materials)

    def test_focus_field_schema(self):
        # item@2 gear `focus` (docs/CONTENT_CONVENTIONS.md section 18): optional, closed enum, gear only.
        from jsonschema import Draft202012Validator
        from referencing import Registry, Resource
        schemas = ROOT / "schemas"
        res = [(d["$id"], Resource.from_contents(d)) for d in
               (json.loads(p.read_text(encoding="utf-8")) for p in schemas.rglob("*.schema.json")) if "$id" in d]
        v = Draft202012Validator(json.loads((schemas / "item/v2.schema.json").read_text(encoding="utf-8")),
                                 registry=Registry().with_resources(res))
        base = {"$schema": "item@2", "id": "test.item.orb", "name": "Orb", "category": "gear"}
        self.assertEqual(list(v.iter_errors(base)), [])
        for fam in ("arcane", "druidic", "holy"):
            self.assertEqual(list(v.iter_errors({**base, "focus": fam})), [], fam)
        for bad in ("Arcane", "divine", "", 1, None, ["arcane"]):
            self.assertTrue(list(v.iter_errors({**base, "focus": bad})), f"expected rejection: {bad!r}")
        shield = {"$schema": "item@2", "id": "test.item.s", "name": "S", "category": "shield", "ac_bonus": 2}
        self.assertTrue(list(v.iter_errors({**shield, "focus": "holy"})), "focus is a gear-only key")
        for lid, *_ in FOCI:
            self.assertEqual(list(v.iter_errors(load(lid))), [], lid)

    def test_tools(self):
        for lid, kind, cost, oz in TOOLS:
            with self.subTest(lid):
                d = load(lid)
                self.assertEqual((d["$schema"], d["id"], d["category"], d["tool_kind"]), ("item@2", f"srd51.item.{lid}", "gear", kind))
                self.assertEqual(d["source"], {"ref": "SRD 5.1", "license": "CC-BY-4.0"})
                self.assertEqual((d.get("cost_cp"), d.get("weight_oz")), (cost, oz))
                self.assertIn(TOOL_SENTENCE[kind], d["description"])
                self.assertLessEqual(set(d), {"$schema", "id", "name", "source", "category", "cost_cp", "weight_oz", "description", "tool_kind"})
        self.assertLessEqual({t[1] for t in TOOLS}, set(TOOL_KINDS))
        for p in ITEMS.glob("*.json"):   # tool_kind marks exactly the TOOLS rows
            self.assertEqual("tool_kind" in json.loads(p.read_text(encoding="utf-8")), p.stem in {t[0] for t in TOOLS}, p.stem)

    def test_tool_kind_schema(self):
        # item@2 gear `tool_kind` (docs/CONTENT_CONVENTIONS.md section 18): optional, closed enum, gear only.
        from jsonschema import Draft202012Validator
        from referencing import Registry, Resource
        schemas = ROOT / "schemas"
        res = [(d["$id"], Resource.from_contents(d)) for d in
               (json.loads(p.read_text(encoding="utf-8")) for p in schemas.rglob("*.schema.json")) if "$id" in d]
        v = Draft202012Validator(json.loads((schemas / "item/v2.schema.json").read_text(encoding="utf-8")),
                                 registry=Registry().with_resources(res))
        base = {"$schema": "item@2", "id": "test.item.kit", "name": "Kit", "category": "gear"}
        self.assertEqual(list(v.iter_errors(base)), [])
        for k in TOOL_KINDS:
            self.assertEqual(list(v.iter_errors({**base, "tool_kind": k})), [], k)
        for bad in ("Artisan's Tools", "tools", "kit", "", 1, None, ["gaming_set"]):
            self.assertTrue(list(v.iter_errors({**base, "tool_kind": bad})), f"expected rejection: {bad!r}")
        weapon = {"$schema": "item@2", "id": "test.item.w", "name": "W", "category": "weapon"}
        self.assertTrue(list(v.iter_errors({**weapon, "tool_kind": "other_tools"})), "tool_kind is a gear-only key")
        for lid, *_ in TOOLS:
            self.assertEqual(list(v.iter_errors(load(lid))), [], lid)

    def test_diamond_is_the_priced_material_id(self):
        # spell@1 components.m.item names it (Revivify fixture); the contract matches equipment ids exactly.
        rv = json.loads((ROOT / "tests/validate/fixtures/spell_kinds/spells/revivify.json").read_text(encoding="utf-8"))
        self.assertEqual(rv["components"]["m"]["item"], load("diamond")["id"])

    def test_spot_values(self):
        by = {w[0]: w for w in WEAPONS}
        self.assertEqual(by["longsword"][4:6], ("1d8 slashing", "1d10 slashing"))
        self.assertEqual(by["blowgun"][4], "1 piercing")
        self.assertIsNone(by["net"][4])
        self.assertEqual((by["glaive"][2], by["whip"][2], by["lance"][2]), (10, 10, 10))
        self.assertEqual(by["dagger"][2:4], (5, (20, 60)))
        self.assertEqual((by["dart"][7], by["dart"][8]), (5, 4))   # 5 cp, 1/4 lb = 4 oz
        self.assertEqual(by["sling"][8], 0)

    def test_frozen_fl1_has_no_items(self):
        self.assertEqual(list((ROOT / "tests/compat").glob("fl*/**/items")), [])


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False).result.wasSuccessful() else 1)
