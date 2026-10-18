# SRD 5.1 spells, levels 0-3: authored vs deferred

> This work includes material taken from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, available at <https://dnd.wizards.com/resources/systems-reference-document>. The SRD 5.1 is licensed under the Creative Commons Attribution 4.0 International License, <https://creativecommons.org/licenses/by/4.0/legalcode>.

Owner: Tools & Content. Companion to `docs/SRD_COMBAT_COVERAGE.md` (Rules; section 8 and 10.2) and the `content/packs/srd51.core` pack.

**Source.** Spell text, levels, schools, classes, components, ranges and dice come from the [5e-bits/5e-database](https://github.com/5e-bits/5e-database) JSON mirror (`src/2014/en/5e-SRD-Spells.json`, commit `a6212beb4b278917c2cff41c2b04c4ef4f3e0d6f`; the mirror's code is MIT, the SRD data is CC-BY-4.0), the same snapshot as `docs/SRD_COMBAT_COVERAGE.md`. The 9 spells authored here were cross-checked field by field (level, range, casting time, duration, classes) against the Open5e `wotc-srd` document (api.open5e.com). One difference: Open5e lists Cleric and Warlock for Scorching Ray; the mirror (and the SRD 5.1 spell list) gives Sorcerer and Wizard, which is what the file uses. The published PDF was not re-read. The pack manifest's `attribution` string already carries the SRD notice; every spell has `source {ref: "SRD 5.1", license: "CC-BY-4.0"}`.

**Scope rule.** A spell is authored only if `spell@1` on `main` expresses its combat-relevant behavior faithfully with mechanics the engine implements today (`spell_attack`, `save` with damage and/or a condition, `heal`, `auto_hit`, `ac_bonus`, `counter`, `targets`, `scaling`). Anything that needs a mechanic reserved for a later row is deferred, not approximated. Area spells (cone, cube, sphere, line) are deferred under C3 / R-G10 following `docs/SRD_COMBAT_COVERAGE.md`: `spell@1` can store the template, but no code computes who is inside it, so the cast would not play. They can be added without a schema change once R-G10 lands. Spells with no combat effect (utility, exploration, casting time of 1 minute or more) are listed as `OOS`.

## Counts (169 SRD spells of level 0-3 in the mirror)

| level | in `srd51.core` (6 shipped + 9 new) | deferred: needs a mechanic | deferred: out of combat scope | total |
|---|---|---|---|---|
| 0 (cantrips) | 6 (1 + 5) | 7 | 11 | 24 |
| 1 | 5 (3 + 2) | 25 | 19 | 49 |
| 2 | 2 (1 + 1) | 29 | 23 | 54 |
| 3 | 2 (1 + 1) | 21 | 19 | 42 |
| **total** | **15 (6 + 9)** | **82** | **72** | **169** |

## Authored in this change (9)

| spell | level | `spell@1` shape | notes |
|---|---|---|---|
| Acid Splash | 0 | `save` DEX, 1d6 acid, none on success; `targets {count 2, max_spread_ft 5}`; cantrip dice | |
| Eldritch Blast | 0 | `spell_attack` ranged 1d10 force; cantrip `adds: beams` (5/11/17) | Agonizing Blast / Eldritch Spear are invocations (R-G4, R-G14) |
| Poison Spray | 0 | `save` CON, 1d12 poison, none on success; range 10; cantrip dice | |
| Produce Flame | 0 | `spell_attack` ranged 1d8 fire, range 30 (the hurl), `duration minutes 10`; cantrip dice | SRD range is Self (the flame in hand); the light is out of scope. Modeled as the hurl, as in the coverage doc |
| Sacred Flame | 0 | `save` DEX, 1d8 radiant, none on success; cantrip dice | "no benefit from cover" is not modeled (cover save bonus, R-G13); the SRD text is kept in `description` |
| Healing Word | 1 | `heal` 1d4 + spellcasting modifier, excludes undead/construct; bonus action, range 60; `upcast.dice 1` | |
| Inflict Wounds | 1 | melee `spell_attack` 3d10 necrotic; `upcast.dice 1` | |
| Scorching Ray | 2 | `spell_attack` ranged, `beams 3`, 2d6 fire; `upcast.beams 1` | |
| Mass Healing Word | 3 | `heal` 1d4 + spellcasting modifier, excludes undead/construct; `targets {count 6}`, bonus action, range 60; `upcast.dice 1` | |

Already shipped (unchanged): Fire Bolt, Magic Missile, Cure Wounds, Shield, Hold Person, Counterspell.

## Deferred (154 spells)

Gap ids are Rules' ids from `docs/SRD_COMBAT_COVERAGE.md` (section 11.2 maps the first-draft ids in brackets: B1 = R-G2 effects array, B2 = R-G3 timed modifiers, B3 = R-G4 damage riders, B4 = R-G5 pools, B6 = R-G7 reactions / roll windows, C1 = R-G8 conditions with source, C2 = R-G9 forced movement / teleport, C3 = R-G10 area templates, C4 = R-G11 zones / auras, C5 = R-G12 summons, C7 = R-G13 cover / lighting / vision, C8 = R-G14 per-cast options, C9 = R-G15 spell kinds). `T&C-G12` / `T&C-G14` are movement modes and creature footprint. `OOS` rows have no gap id.

| level | spell | kind | missing mechanic (from the coverage doc) | gap id |
|---|---|---|---|---|
| 0 | Chill Touch | NEW | Attack damage is DATA; "can't regain HP, undead disadvantage" rider needs R-G2/R-G3. | R-G2, R-G3 (B1, B2) |
| 0 | Dancing Lights | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Druidcraft | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Guidance | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Light | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Mage Hand | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Mending | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Message | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Minor Illusion | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Prestidigitation | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Ray of Frost | NEW | Damage DATA; speed -10 until next turn is R-G3. | R-G3 (B2) |
| 0 | Resistance | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | Shillelagh | NEW | Weapon-damage buff (R-G3). | R-G3 (B2) |
| 0 | Shocking Grasp | NEW | Melee spell attack DATA; advantage vs metal armor and "no reactions" rider R-G2/R-G3. | R-G2, R-G3 (B1, B2) |
| 0 | Spare the Dying | NEW | spell@1 has no stabilize effect kind; Rules models it only as `ActionKind::SpareTheDying` + `hooks.spare_the_dying` (coverage doc marks it DATA via that ActionKind, not via spell@1) | - |
| 0 | Thaumaturgy | OOS | none (out of combat scope): Utility/out of combat. Guidance and Resistance add d4 to a check/save: NEW R-G7 if wanted in combat. | R-G7 (B6) |
| 0 | True Strike | NEW | Advantage on next attack (R-G3). | R-G3 (B2) |
| 0 | Vicious Mockery | NEW | Damage DATA; disadvantage on next attack rider R-G2/R-G3. | R-G2, R-G3 (B1, B2) |
| 1 | Alarm | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Animal Friendship | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Bane | NEW | Penalty dice on attacks and saves (R-G3). | R-G3 (B2) |
| 1 | Bless | NEW | Bonus dice on attacks and saves (R-G3). | R-G3 (B2) |
| 1 | Burning Hands | NEW | Cone (R-G10); dice/save/upcast are DATA. | R-G10 (C3) |
| 1 | Charm Person | NEW | Charmed with source (R-G8). | R-G8 (C1) |
| 1 | Color Spray | NEW | HP-pool targeting (R-G15) + area. | R-G15 (C9) |
| 1 | Command | NEW | Forced action next turn (R-G8). | R-G8 (C1) |
| 1 | Comprehend Languages | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Create or Destroy Water | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Detect Evil and Good | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Detect Magic | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Detect Poison and Disease | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Disguise Self | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Divine Favor | NEW | Weapon damage buff (R-G3/R-G4). | R-G3, R-G4 (B2, B3) |
| 1 | Entangle | NEW | Area zone + restrained (R-G10, R-G11). | R-G10, R-G11 (C3, C4) |
| 1 | Expeditious Retreat | NEW | Bonus-action Dash for duration (R-G3). | R-G3 (B2) |
| 1 | Faerie Fire | NEW | Area + advantage against (R-G10, R-G3). | R-G3, R-G10 (B2, C3) |
| 1 | False Life | NEW | Temp HP effect kind (R-G3). | R-G3 (B2) |
| 1 | Feather Fall | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Find Familiar | NEW | Familiar (R-G12). | R-G12 (C5) |
| 1 | Floating Disk | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Fog Cloud | NEW | Obscurement zone (R-G13, R-G11). | R-G11, R-G13 (C4, C7) |
| 1 | Goodberry | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Grease | NEW | Zone + prone (R-G11). | R-G11 (C4) |
| 1 | Guiding Bolt | NEW | Attack damage DATA; advantage on next attack vs target rider (R-G2/R-G3). | R-G2, R-G3 (B1, B2) |
| 1 | Hellish Rebuke | NEW | Needs a "damaged by creature" reaction trigger (R-G7). | R-G7 (B6) |
| 1 | Heroism | NEW | Temp HP each turn, frightened immunity (R-G3). | R-G3 (B2) |
| 1 | Hideous Laughter | NEW | Prone + incapacitated (two conditions) and save-on-damage repeat (R-G2). | R-G2 (B1) |
| 1 | Hunter's Mark | NEW | R-G4. | R-G4 (B3) |
| 1 | Identify | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Illusory Script | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Jump | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Longstrider | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Mage Armor | NEW | AC set (R-G3). | R-G3 (B2) |
| 1 | Protection from Evil and Good | NEW | R-G3. | R-G3 (B2) |
| 1 | Purify Food and Drink | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Sanctuary | NEW | Attackers must save to target (R-G3/R-G7). | R-G3, R-G7 (B2, B6) |
| 1 | Shield of Faith | NEW | Non-reaction AC bonus buff (R-G3). `ac_bonus` is reaction-only. | R-G3 (B2) |
| 1 | Silent Image | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Sleep | NEW | HP-pool (R-G15). | R-G15 (C9) |
| 1 | Speak with Animals | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 1 | Thunderwave | NEW | Cube (R-G10) + push (R-G9); dice/save DATA. | R-G9, R-G10 (C2, C3) |
| 1 | Unseen Servant | OOS | none (out of combat scope): Utility / exploration. Goodberry heals items, not in-fight; Feather Fall is a falling reaction (falling not simulated). | - |
| 2 | Acid Arrow | NEW | Delayed second damage (R-G15). | R-G15 (C9) |
| 2 | Aid | NEW | Max HP and HP increase (R-G3). | R-G3 (B2) |
| 2 | Alter Self | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Animal Messenger | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Arcane Lock | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Arcanist's Magic Aura | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Augury | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Barkskin | NEW | AC floor (R-G3). | R-G3 (B2) |
| 2 | Blindness/Deafness | NEW | `save` with condition is DATA, but the caster chooses blinded or deafened per cast (parameterized effect, small R-G2). | R-G2 (B1) |
| 2 | Blur | NEW | Disadvantage against you (R-G3). | R-G3 (B2) |
| 2 | Branding Smite | NEW | Weapon-attack rider: extra radiant damage, reveals invisible, concentration (R-G4, T&C-G5). SRD level 2 (the earlier draft also listed it at level 1). | R-G4, T&C-G5 (B3) |
| 2 | Calm Emotions | NEW | Area + suppress condition (R-G10, R-G3). | R-G3, R-G10 (B2, C3) |
| 2 | Continual Flame | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Darkness | NEW | Obscurement (R-G13). | R-G13 (C7) |
| 2 | Darkvision | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Detect Thoughts | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Enhance Ability | NEW | Check advantage and bonus per mode (R-G3). | R-G3 (B2) |
| 2 | Enlarge/Reduce | NEW | Size/damage change (R-G3, T&C-G12, T&C-G14). | R-G3, T&C-G12, T&C-G14 (B2) |
| 2 | Enthrall | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Find Steed | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Find Traps | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Flame Blade | NEW | Summoned weapon (R-G12). | R-G12 (C5) |
| 2 | Flaming Sphere | NEW | Object/zone (R-G11/R-G12). | R-G11, R-G12 (C4, C5) |
| 2 | Gentle Repose | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Gust of Wind | NEW | Line + push (R-G9, R-G10). | R-G9, R-G10 (C2, C3) |
| 2 | Heat Metal | NEW | Sustained damage (R-G3). | R-G3 (B2) |
| 2 | Invisibility | NEW | Condition applied with no save, ends on attack/cast; `save` kind requires a save (R-G2/R-G3). | R-G2, R-G3 (B1, B2) |
| 2 | Knock | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Lesser Restoration | NEW | End a condition (R-G15). | R-G15 (C9) |
| 2 | Levitate | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Locate Animals or Plants | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Locate Object | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Magic Mouth | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Magic Weapon | NEW | Magic bonus (R-G17, T&C-G2/R-G3). | R-G3, R-G17, T&C-G2 (B2) |
| 2 | Mirror Image | NEW | Duplicates that absorb attacks (R-G3). | R-G3 (B2) |
| 2 | Misty Step | NEW | Teleport (R-G9). | R-G9 (C2) |
| 2 | Moonbeam | NEW | Zone (R-G11). | R-G11 (C4) |
| 2 | Pass Without Trace | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Prayer of Healing | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Protection from Poison | NEW | R-G3. | R-G3 (B2) |
| 2 | Ray of Enfeeblement | NEW | Attack + damage halving rider (R-G3). | R-G3 (B2) |
| 2 | Rope Trick | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | See Invisibility | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Shatter | NEW | Sphere (R-G10). | R-G10 (C3) |
| 2 | Silence | NEW | Zone preventing verbal components (R-G11). | R-G11 (C4) |
| 2 | Spider Climb | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 2 | Spike Growth | NEW | Zone (R-G11). | R-G11 (C4) |
| 2 | Spiritual Weapon | NEW | Summoned attacker (bonus action each turn) (R-G12). | R-G12 (C5) |
| 2 | Suggestion | NEW | Forced behavior (R-G8). | R-G8 (C1) |
| 2 | Warding Bond | NEW | Shared damage (R-G3). | R-G3 (B2) |
| 2 | Web | NEW | Area zone + restrained (R-G10, R-G11). | R-G10, R-G11 (C3, C4) |
| 2 | Zone of Truth | OOS | none (out of combat scope): Utility / out of encounter casting time. | - |
| 3 | Animate Dead | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Beacon of Hope | NEW | Save advantage, max healing (R-G3). | R-G3 (B2) |
| 3 | Bestow Curse | NEW | Touch + chosen effect (R-G2/R-G3). | R-G2, R-G3 (B1, B2) |
| 3 | Blink | NEW | Phasing (R-G9). | R-G9 (C2) |
| 3 | Call Lightning | NEW | Zone + repeating action (R-G11). | R-G11 (C4) |
| 3 | Clairvoyance | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Conjure Animals | NEW | Summons (R-G12). | R-G12 (C5) |
| 3 | Create Food and Water | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Daylight | NEW | Light/obscurement dispelling (R-G13). | R-G13 (C7) |
| 3 | Dispel Magic | NEW | End effects, check by level (R-G15). | R-G15 (C9) |
| 3 | Fear | NEW | Cone, frightened, drop items, forced Dash (R-G10, R-G8). | R-G8, R-G10 (C1, C3) |
| 3 | Fireball | NEW | Sphere (R-G10); 8d6 DEX half, `upcast.dice 1` are DATA. | R-G10 (C3) |
| 3 | Fly | NEW | Movement mode (T&C-G12, T&C-G14). | T&C-G12, T&C-G14 |
| 3 | Gaseous Form | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Glyph of Warding | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Haste | NEW | Speed, AC, extra action (R-G3, R-G5). | R-G3, R-G5 (B2, B4) |
| 3 | Hypnotic Pattern | NEW | Area, charmed + incapacitated (R-G10, R-G8). | R-G8, R-G10 (C1, C3) |
| 3 | Lightning Bolt | NEW | Line (R-G10); dice/save DATA. | R-G10 (C3) |
| 3 | Magic Circle | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Major Image | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Meld Into Stone | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Nondetection | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Phantom Steed | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Plant Growth | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Protection From Energy | NEW | Resistance buff (R-G3). | R-G3 (B2) |
| 3 | Remove Curse | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Revivify | NEW | Raise a creature dead within 1 minute (R-G15). | R-G15 (C9) |
| 3 | Sending | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Sleet Storm | NEW | Zone (R-G11). | R-G11 (C4) |
| 3 | Slow | NEW | Speed/AC/attacks (R-G3). | R-G3 (B2) |
| 3 | Speak with Dead | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Speak with Plants | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Spirit Guardians | NEW | Aura/zone around caster (R-G11). | R-G11 (C4) |
| 3 | Stinking Cloud | NEW | Zone (R-G11). | R-G11 (C4) |
| 3 | Tiny Hut | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Tongues | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Vampiric Touch | NEW | Attack damage DATA; heal half and repeat action need R-G2/R-G3. | R-G2, R-G3 (B1, B2) |
| 3 | Water Breathing | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Water Walk | OOS | none (out of combat scope): Utility / out of encounter casting time (1 minute or longer). | - |
| 3 | Wind Wall | NEW | Zone (R-G11). | R-G11 (C4) |

## Follow-ups when the mechanics land

- Authoring the area spells (Burning Hands, Thunderwave, Fireball, Lightning Bolt, Shatter, Fear, ...) needs only R-G10 plus the per-spell push or condition rider; their dice, save and upcast already fit `spell@1`.
- Every `NEW` row above is additive content once its row lands; ids are `srd51.spell.<snake_case_name>` and are not reserved yet.
- Spare the Dying: `spell@1` has no stabilize effect, only the Rules `ActionKind::SpareTheDying` hook. If Rules adds an effect kind (additive), it is a one-file change.
