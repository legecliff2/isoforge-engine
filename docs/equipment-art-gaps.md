# Equipment art coverage and gaps

Snapshot of the presentation art in this tree at `663ed618`. `equip_kit@1` is presentation art: the schema description says it is not a sim content document, is not registered in the engine schema registry, and is not folded into `Combat::hash`, `log_hash`, or chunk `content_hash` (`schemas/equip_kit/v1.schema.json`). The same description says per-body attach tables are not this file.

## Lookup

`EquipArtTable::find` (`game/presentation/include/presentation/equip_art.hpp`, `game/presentation/src/equip_art.cpp`) resolves one part and one view in this order:

1. `content_id` plus the requested size bin. An empty bin means medium. A bin name other than `tiny`, `small`, `medium`, `large`, or `huge` is treated as medium.
2. The same `content_id` in the medium bin, when the requested bin was not medium. A `{content_id}/{bin}/` file wins over the flat medium file for that bin. Flat files are the medium bin.
3. The kind default, medium only, at `_kind/{weapon|shield|light|other}/{part}.{view}.png`, when the query names a kind.
4. Nothing. The caller then draws placeholder quads (`equip_quads` in `game/vertical_slice/src/equip_visuals.cpp`). A missing file is not a checker. A `kit.json` this loader cannot read does not block the sprites; it only drops pivot and `back_role` overrides.

Content ids are matched whole. A quiver file is not a bolt-case file. The presentation test that pins the order is `equipment art: content id, then medium, then kind, then nothing` in `game/presentation/tests/presentation_tests.cpp`.

`equip_art_choice` (`game/vertical_slice/src/equip_visuals.cpp`) asks for the part the layout chose, then the view for the facing. Hip-main and hip-off both look up the `hip` part. A null sprite is the quad path. The vertical-slice executable loads the shipped kit (`SLICE_EQUIP_ART_ROOT` = `content/art/equipment`, `game/vertical_slice/CMakeLists.txt`). `SliceView`'s one-argument constructor uses that root (`game/vertical_slice/src/slice_view.hpp`).

## Bodies

`load_body_attach` (`game/vertical_slice/src/equip_visuals.cpp`) reads an optional `attach.json`. A missing key keeps the shared point. `loaded` is false when no file was found. West facings negate x: sw mirrors se, nw mirrors ne (`attach_points`).

Search order:

- `visual_kind` `"paperdoll"` reads `{standin_root}/../paperdoll/{bin}/attach.json` first. The vertical slice's stand-in root is `content/art/monster_standins`, so that path is `content/art/paperdoll/{bin}/attach.json`.
- Any other visual kind reads `{standin_root}/srd/{dir}/{bin}/attach.json`. `dir` is `monster_art_dir` (`game/presentation/src/standin.cpp`): the monster id's last segment, underscores written as hyphens, with `succubus` mapped to `succubus-incubus`.
- Both then fall through to `{standin_root}/frames/{bin}/attach.json`.
- Then the shared table in `AttachPoints` (`game/vertical_slice/src/equip_visuals.hpp`).

The only `attach.json` under `content/art/` is `content/art/paperdoll/medium/attach.json`. No monster kit and no generic frame bin ships one.

### Paperdoll medium (H4 mannequin)

`content/art/paperdoll/` contains only `medium/attach.json`. There is no paperdoll sprite in that directory. The file's points, in screen pixels from the token anchor:

| key | se | ne |
|---|---|---|
| `main` | [4, 3] | [4, 3] |
| `off` | [-4, 3] | [-4, 3] |
| `back` | [-2, -9] | [-1, -9] |
| `hip-main` | [1, -3] | [4, -3] |
| `hip-off` | [-5, -3] | [-6, -3] |
| `neck` | [1, -11] | absent |

`ne` has no `neck` key, so that point stays the shared neck `(0, 2)`. Away facings do not draw the neck (below), so the stored ne neck is unused by the neck layer. Pinned by `equip attach: paperdoll uses the humanoid table; a medium stand-in does not` in `game/vertical_slice/tests/equip_visuals_tests.cpp`.

### Monster SRD bins

Every directory under `content/art/monster_standins/srd/` is a medium bin and none contains `attach.json`. Those 37 slugs are: `azer`, `barbed-devil`, `basilisk`, `bearded-devil`, `blink-dog`, `couatl`, `death-dog`, `dryad`, `erinyes`, `ettercap`, `gargoyle`, `gray-ooze`, `green-hag`, `grick`, `harpy`, `hell-hound`, `invisible-stalker`, `lemure`, `medusa`, `night-hag`, `rust-monster`, `satyr`, `sea-hag`, `shrieker`, `succubus-incubus`, `swarm-of-bats`, `swarm-of-beetles`, `swarm-of-centipedes`, `swarm-of-insects`, `swarm-of-poisonous-snakes`, `swarm-of-quippers`, `swarm-of-rats`, `swarm-of-ravens`, `swarm-of-spiders`, `swarm-of-wasps`, `violet-fungus`, `xorn`.

A stand-in of one of these bodies therefore does not load a per-monster attach table. The same test checks `content/art/monster_standins/srd/green-hag/medium/attach.json` is absent and that `srd51.monster.green_hag` at medium comes back unloaded.

### Generic frame bins

`content/art/monster_standins/frames/` has `tiny`, `small`, `medium`, and `large`. None has `attach.json`. The header on `load_body_attach` says `frames/{bin}` stays the generic blob and does not carry the humanoid points. After a miss, the body uses the shared table:

The shared `se` and `ne` points are the same values. West negates x.

| point | shared se and ne |
|---|---|
| `main_hand` | (9, 4) |
| `off_hand` | (-9, 4) |
| `back` | (-7, 2) |
| `hip_main` | (6, 8) |
| `hip_off` | (-6, 8) |
| `neck` | (0, 2) |

An unknown bin (`standin_canvas` width 0) returns this table immediately and does not read a file.

## Items with authored sprites

`content/art/equipment/kit.json` is `equip_kit@1`, cell `[24, 24]`, no `atlas`, no `kinds`, and no `bin` on any part. The description in that file says it lists only sprites that exist, that no non-medium bin is authored, and that a pivot is written only for a sprite that exists. The loose files are the flat medium layout `{content_id}/{part}.{view}.png`. There is no `_kind/` directory and no `{content_id}/{bin}/` directory.

Seven content ids have sprites. Every listed pivot is `[12, 12]`.

| content_id | part | views | `back_role` |
|---|---|---|---|
| `srd51.item.quiver` | `back` | se, ne | `quiver` |
| `srd51.item.case_crossbow_bolt` | `back` | se, ne | `bolt_case` |
| `srd51.item.backpack` | `back` | se, ne | `pack` |
| `srd51.item.shield` | `back` | se, ne | omitted |
| `srd51.item.pouch` | `hip` | se, ne | omitted |
| `srd51.item.component_pouch` | `hip` | se, ne | omitted |
| `srd51.item.holy_symbol_amulet` | `neck` | se only | omitted |

That is 13 PNGs. No shipped sprite uses `main_hand`, `off_hand`, or `held_light`.

Anything else misses step 1 and step 2. Step 3 also misses, because no kind PNG exists, so the draw is `equip_quads`. That includes every other content id, a shield asked for as a hand part (the shield files are `back` only), a weapon's hand or sheath, and a held light. A weapon drawn as hip-main or hip-off gets no quad either: `equip_quads` returns an empty list for a weapon on those parts ("the hand quad is the weapon").

Quad sizes when there is no sprite (`equip_quads`):

- Back, shield kind: 24×18. Back, other kind: 14×16. Back, weapon: 3×24.
- Hip, non-weapon: 8×7. The comment calls this the belt pouch.
- Neck: 5×6.
- Weapon in hand: a 3×16 blade (3×22 when `hands > 1`) plus a 7×2 grip. Shield in hand: 11×13 plus a 3×3 boss. Light: a torch body 3×10 or a 6×7 body, plus a flame quad when `sheds_light`. Other in hand: 5×5.

`back_role` on the shipped kit is only the three values above. The schema enum is `quiver`, `bolt_case`, `pack`, `other`. `other` is not used in this manifest. Shields and two-handed weapons omit `back_role`; `tools/validate/equip_kit.py` rejects one on those items and requires `quiver` / `bolt_case` / `pack` on the three container ids. A missing manifest field is not a role. `container_back_role` then falls back to the content id's last segment: `quiver`, `case_crossbow_bolt`, and `backpack`. Any other slug, including `shield`, is `BackRole::None` (`game/vertical_slice/src/equip_visuals.cpp`).

## `component_pouch` reuses the pouch art

The loader does not alias `srd51.item.component_pouch` to `srd51.item.pouch`. Each id has its own directory and its own `kit.json` entry (hip, se and ne, pivot `[12, 12]`). The reuse is a second copy of the same PNG bytes:

- `content/art/equipment/srd51.item.component_pouch/hip.se.png` is byte-identical to `content/art/equipment/srd51.item.pouch/hip.se.png`
- `content/art/equipment/srd51.item.component_pouch/hip.ne.png` is byte-identical to `content/art/equipment/srd51.item.pouch/hip.ne.png`

They are separate files. `EquipArtTable::find` still matches the content id whole, so a component pouch does not open the pouch directory.

The fixture test `equip art: the H4 fixture kit loads all 13 sprites at pivot [12, 12]` in `game/vertical_slice/tests/equip_visuals_tests.cpp` reads `game/vertical_slice/tests/fixtures/h4_equip_kit` (not the shipped tree) and checks that `srd51.item.pouch/hip.se.png` and `srd51.item.component_pouch/hip.se.png` in that fixture have the same bytes. That test does not compare `hip.ne.png`. In the fixture, all four hip PNGs are byte-identical to each other; those fixture bytes are not the shipped PNGs.

Layout is separate from the pixels. `assign_hips` treats a slug `component_pouch` as the component pouch and a slug `pouch` as the belt pouch. The belt pouch fills the hip anchor only when no component pouch is carried (`game/vertical_slice/src/equip_visuals.cpp`).

## Neck is SE-only

The neck part has the toward view only.

- The schema accepts a neck part only when its single view is `se` (`schemas/equip_kit/v1.schema.json`). `tools/validate/equip_kit.py` reports `neck has the toward view se only` otherwise, and rejects a file named `neck.ne`.
- The loader's `parse_sprite_name` rejects neck plus the away view, so `neck.ne.png` is not an entry (`game/presentation/src/equip_art.cpp`). A neck pivot whose view is `ne` is skipped while reading `kit.json`. The presentation test is `equipment art: neck.se loads and neck.ne is rejected`.
- `facing_shows_neck` is true only for `Facing::SouthEast` and `Facing::SouthWest` (`game/vertical_slice/src/equip_visuals.hpp`). The comment there says profile e/w are not toward, M0 has none, and this function does not treat them as se/sw.
- `equip_layers` draws a neck layer only when `facing_shows_neck` is true. Away facings (ne, nw) get no neck layer, so they draw neither the sprite nor the neck quad.
- `equip_art_choice` returns no sprite for a neck part when the facing does not show the neck, and it does not look up `neck.ne`.
- On SE the sprite is `neck.se`, unmirrored. On SW the same sprite is drawn with `mirror_x` (the west mirror about `pivot_x`). NE and NW stay empty. z is `kZWornFront` (5): in front of the body (`kZBody` is 4) and behind both hands (`kZFarHand` is 6). The test name is `equip neck: visible on toward facings, hidden on away, z5`.

The shipped amulet entry matches that rule: one part, `neck`, one view, `se`, pivot `[12, 12]`, file `content/art/equipment/srd51.item.holy_symbol_amulet/neck.se.png`. There is no `neck.ne.png`.

Other facings of a neck item: SW mirrors the SE sprite. NE and NW draw nothing. The paperdoll's missing `ne` neck key does not invent an away sprite. The shared neck `(0, 2)` is what a toward facing uses when the body file did not set `neck`; the paperdoll SE facing uses `[1, -11]` instead.

## Views, pivots, hips, back priority

### Views

`se` is toward and `ne` is away. `sw` and `nw` are not stored. West is an engine mirror of the east view and has no file and no manifest field (`equip_art.hpp`, the schema's `view_name`). `equip_art_choice` picks the away view when `facing_shows_back` is true (NE and NW; the comment also names n, which M0 does not have) and the toward view otherwise. `mirror_x` is set for SW and NW. M0's comment on `facing_shows_back` says the eight-facing rule is away = n / ne / nw and toward or profile = s / se / sw / e / w, and that M0 has the four diagonals.

### Pivots

One pivot per sprite, the pixel that sits on the attach point. Absent, or outside the image, the loader uses bottom-centre `(width/2, height-1)` (`equip_art.hpp`, `EquipArtTable::load`). The schema says a pivot must lie inside the cell, that H4 cells are 24×24, and that H4's provisional pivot is `[12, 20]` where that art names one. The same header says `[12, 20]` is not a default and that H4 art will declare `[12, 12]`. The shipped manifest names `[12, 12]` on every sprite that exists. For a 24×24 image, bottom-centre would be `(12, 23)`.

### Hip-main and hip-off

The `hip` part has no side. The layout assigns the anchor (`equip_visuals.hpp`). `assign_hips`:

- A weapon in the main hand and a weapon in the off hand (two different item instances) puts a sheath on hip-main and hip-off. Both sheaths hide the pouch.
- No weapon in hand: the pouch, if any, takes hip-main.
- One weapon in hand: that sheath takes hip-main and the pouch takes hip-off.
- The component pouch wins over the belt pouch. Two of the same pouch kind keep the lower item instance id.

Both hip anchors use `worn_z` for the hip slot. On a toward or profile facing that is `kZWornFront` (5). On an away facing it is `kZBack` (2). The comment on `worn_z` says art decided hip-off stays on that rule for M0, including the far side, and that the far side moves to z 2 only when true E/W facings ship. Both hips therefore share one z on the four diagonals. The test is `equip hips: sheath, two-weapon fighting, pouch, and hidden extras`.

### Back priority

One back item is drawn (`choose_back`). Rank from `back_rank`, lower rank first, and a tie keeps the lower item instance id (`equip back: one item, fixed priority, lower instance id wins a tie`):

1. `BackRole::Quiver` while a bow is in a hand, or `BackRole::BoltCase` while a crossbow is in a hand. A wielded weapon is main-hand or off-hand, not covered. Crossbow is an id containing `crossbow`; bow is an id containing `bow` that is not a crossbow. A bow sitting on the back is not wielded.
2. Kind shield whose slot is already back. A shield in the hand is not this rank.
3. Kind weapon with `hands >= 2` or `versatile`.
4. `BackRole::Pack`.
5. A quiver or bolt case that did not match a wielded bow or crossbow.
6. Anything else on the back.

Shields and stowed two-handed or versatile weapons are not containers. Those ranks come from kind, hands, and versatile, not from `back_role`. The manifest role, when the table has one, replaces the content-id fallback (`container_back_role`). The shipped kit's three roles match that fallback.

Back z is the opposite of hip: in front of the body (`kZWornFront`, 5) on an away facing, behind it (`kZBack`, 2) on a toward or profile facing. The test is `equip visuals: back and hip z flip around the body on all four facings`.

## Tests that read the shipped kit

Two tests open the real `content/art/equipment/kit.json`:

- `EquipKitSchema.test_shipped_manifest_matches_the_files_on_disk` in `tests/validate/test_equip_kit_schema.py`. It calls `equip_kit.validate_live`, whose `KIT_PATH` is `content/art/equipment/kit.json` (`tools/validate/equip_kit.py`), then reads that path again and checks `$schema` is `equip_kit@1`, `cell` is `[24, 24]`, `atlas` is absent, and no `bin` key appears. The module's opening docstring still says "the shipped (empty) H4 catalog". The kit at this commit is not empty; the test checks the files on disk, not that `items` is empty.
- `shipped equipment kit: loads, and every sprite it declares resolves` in `game/vertical_slice/tests/equip_visuals_tests.cpp`. It loads `SLICE_EQUIP_ART_ROOT`, which CMake sets to `content/art/equipment`, opens `kit.json` there, and requires each declared sprite whose PNG exists to resolve with the manifest pivot. It also checks that `srd51.item.no_such_pack` has no sprite and falls back to one 14×16 back quad.

`validate_packs.py` calls `equip_kit.validate_live` on the same path during content validation. That is the validator, not a third test case named for the shipped kit.

`equip art: the H4 fixture kit loads all 13 sprites at pivot [12, 12]` reads `SLICE_H4_EQUIP_FIXTURE`, set to `game/vertical_slice/tests/fixtures/h4_equip_kit`. That fixture's `kit.json` lists the same seven ids, parts, views, roles, and `[12, 12]` pivots. Its PNGs are not the shipped PNGs.

The other vertical-slice view tests construct `SliceView` with `SLICE_EMPTY_EQUIP_KIT`, set in `game/vertical_slice/CMakeLists.txt` to `game/vertical_slice/tests/fixtures/empty_equip_kit`. That fixture's `kit.json` is `equip_kit@1`, cell `[24, 24]`, `items` empty. The same define is passed to `vertical_slice_tests`, `vertical_slice_overlay_bench`, `vertical_slice_equip_snapshot`, and `vertical_slice_footprint_snapshot`. Call sites are in `aoe_preview_tests.cpp`, `cover_debug_tests.cpp`, `debug_equip_tests.cpp`, `equip_snapshot.cpp`, `equip_visuals_tests.cpp` (`render_frame`), `faerie_fire_outline_tests.cpp`, `footprint_snapshot.cpp`, `footprint_visuals_tests.cpp`, `light_view_tests.cpp`, `overlay_bench.cpp`, `overlay_determinism_tests.cpp`, `standin_view_tests.cpp`, `unseen_feedback_tests.cpp`, `vertical_slice_tests.cpp`, and `zone_overlay_tests.cpp`.

Two cases in `equip_visuals_tests.cpp` do not use that fixture. They write a temporary `kit.json` with an empty `items` array (`write_empty_equip_kit`): `equip back: a manifest back_role drives the pick, and a missing field keeps the content id`, and `equip art: missing H4 parts fall back to quads, and the neck quad shows only toward`. The presentation kit tests build their own temp directories and do not read either fixture or the shipped kit.
