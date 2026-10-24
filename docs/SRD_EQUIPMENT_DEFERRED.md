# SRD 5.1 equipment: authored vs deferred mechanics

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Owner: Tools & Content. Companion to `docs/proposals/EQUIPMENT.md`, `docs/SRD_SPELLS_DEFERRED.md` and `docs/SRD_CLASSES_DEFERRED.md`; pack `content/packs/srd51.core`, items in `items/`.

These items are authored with their passive data (cost, weight, verbatim SRD description). The mechanic in each row lives only in the description text: no schema key, validator rule or C++ reads it.

| Item(s) | SRD mechanic (deferred) | Needs |
|---|---|---|
| `rope_hempen_50_feet`, `rope_silk_50_feet` | 2 hit points; burst with a DC 17 Strength check | object HP / ability-check model |
| `chain_10_feet` | 10 hit points; burst with a DC 20 Strength check | object HP / ability-check model |
| `crowbar` | advantage on Strength checks where leverage applies | ability checks with item advantage |
| `manacles` | escape DC 20 Dex; break DC 20 Str; pick lock DC 15 Dex (thieves' tools); 15 HP | restraint condition source, tools, object HP |
| `backpack` | capacity (1 cubic foot / 30 lb of gear) | general container (`container` holds ammunition only) |
| `rations_1_day`, `waterskin` | food / water consumption | survival / exhaustion model |
| `spellbook` | stores wizard spells | spell-storage / known-spells model |
| `net` | restrains a Large or smaller target; DC 10 Str escape; net AC 10, 5 slashing frees; one attack per action | net special rule in rules / sim |
| `lance` | two hands unless mounted (the within-5-ft disadvantage is implemented) | mounts |
| foci (`focus` key, CONTENT_CONVENTIONS section 18) | Replaces only material components with no listed cost that the spell does not consume. SRD 5.1 *Material (M)*: "A character can use a **component pouch** or a **spellcasting focus** (found in "Equipment") in place of the components specified for a spell. But if a cost is indicated for a component, a character must have that specific component before he or she can cast the spell." and "If a spell states that a material component is consumed by the spell, the caster must provide this component for each casting of the spell." A `component_pouch` substitutes for the same components ("except for those components that have a specific cost"). Holy symbol: "the same symbol carefully engraved or inlaid as an emblem on a shield"; "To use the symbol in this way, the caster must hold it in hand, wear it visibly, or bear it on a shield." So a cleric (or paladin) can use the emblem-bearing shield, held in hand, as the focus. | Engine consumption pending Lich: the M component waiver (cost-free, non-consumed only) and class-to-focus eligibility; the loader maps `focus` to `ItemKind::Focus` |
