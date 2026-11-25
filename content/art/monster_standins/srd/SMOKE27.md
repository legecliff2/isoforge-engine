# Smoke #27 — Mediums SE idle_00 (fal)

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449) and the Group D follow-up:** This note is the production record for smoke #27. The tree now holds 37 medium creatures in Long Dusk Mapping B (26 from #449/#458, plus 11 Group D / couatl kits). From this smoke, the tree includes: `medusa`, `rust-monster`, `gray-ooze`. Held back (kept local): `chain-devil`, `rakshasa`, `doppelganger`, `mimic`.

**Status: Art Done (medusa, mimic, rust-monster, gray-ooze) · Anim built for all four, awaiting AD audit**

Roster: Medusa | Mimic | Rust Monster | Gray Ooze — all **medium** 32×48, pivot (16,47) opaque.

## Pipeline / contract
`google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium` (medium LAST). post: 4/4 PASS.

## IDs
| slug | gen request_id | birefnet request_id | opaque | post |
|---|---|---|---|---|
| medusa | 01a1c85e-996f-7d13-b4fb-6b31b118997b | 01a1c85f-6176-7d00-87c9-d60f7551b620 | 521 | PASS |
| mimic | 01a1c85e-99d5-77d3-bb2c-b2da18b16804 | 01a1c85f-61a2-7910-b8c9-79aec8eab319 | 658 | PASS |
| rust-monster | 01a1c85e-99ab-7a00-a0fa-b6ceb43b06f3 | 01a1c85f-61b6-72c3-aa5d-64128aa1085c | 195 | PASS |
| gray-ooze | 01a1c85e-997e-7101-9444-47853f671534 | 01a1c85f-61f0-71d0-bd9f-c8b8a1bab9e8 | 258 | PASS |

All first take; no rejects / no v1 files. No generation missed its tell, so none were regenerated.

## Validation (4/4 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty.
Comps (8-conn): medusa 520+1 pivot · mimic 657+1 pivot · rust-monster 194+1 pivot · gray-ooze 257+1 pivot.
Feet: mimic, rust-monster, and gray-ooze plant on y45 with a row-46 gap (same floater as azer / wolf / the #23–#26 batches / most prior mediums). Medusa plants on y46 (top y1) because the tall upright robed pose is height-limited by `fal_post_to_bin`'s fit (nh=46, pasted at y=1, bottom y46) — same class as the #22 dryad and satyr, the #25 rakshasa, and the #26 doppelganger. Not a post-script miss; the figure fills the bin.
Rust monster is short in the bin (top at y24, 195 opaque) because the long antennae-plus-tail span is width-limited by the same fit — same class as the #25 basilisk (top y26) and the #26 grick (top y21). The whole creature, including both antennae and the tail, is inside the bin, not cropped. Gray ooze is intentionally low and short (top y32, 258 opaque), same sitting-low class as the #24 lemure brief, but much lower and lighter than the lemure itself (top y14, 535 opaque).
No cast shadow or plinth in any raw. No wings crept into any of the four. Smoke 1–26 frames md5: 0 modified/removed. Guard: `.smoke27_md5_pre_se.json` (14912, post-#26 4f idle, pre-#27) → 0 changed / 0 removed / 4 smoke27 SE additions. 592 concurrent smoke26 anim clips also appeared vs that snapshot (Animator bake during this pass, not written here); clips.json was not touched by this pass.

## Speck clean (no fal call)
Isolated 1px fringe speck with no opaque 8-neighbours, #1E2228, set to alpha 0. Confirmed isolated first. No other pixel changed. Baseline stayed y46; pivot (16,47) unchanged.
- medusa SE (21,21) — opaque 522→521. Leftover comps are 520+1 pivot.
Mimic, rust-monster, and gray-ooze had none.

Leftover non-1px components left as-gen for AD:
- None. Each frame is one body plus the detached pivot pixel.

## Soft notes (SE)
- Medusa: SE ✓ (head toward the lower-right, bow held out to the right). A woman in a modest full-length robe with a khaki sash and a quiver at her hip, holding a bow in a neutral standing pose — clearly non-sexualized, no revealing outfit, no body-shape emphasis. The identity tell is the hair: a large dense crown of writhing snakes with many separate serpent heads sticking out of the silhouette in all directions. That crown still reads at 32px as a spiky mass of individual snake-head nubs, not as ordinary curly hair. The face is mostly lost at 32px (same class as other lost faces); the snake-hair silhouette and the bow carry her. Khaki 24.0% (the sash). Tallest of the four — fills the bin (521 opaque, top y1, feet on y46). No wings. No shadow or plinth.
- Mimic: SE ✓ (gaping lid and tongue toward the lower-right). A wooden treasure chest whose lid is thrown wide open into a toothy maw, with a long khaki tongue / sticky pseudopod reaching out down-right. It does not read as a plain closed chest. At 32px the individual teeth shrink into a dark jagged interior, but the open-lid silhouette plus the reaching tongue are the tell and both survive. Heaviest of the four (658 opaque, top y14, feet on y45); the wide chest is width-limited by the fit, so it sits mid-height in the bin, fully inside, not cropped. Khaki 15.7% (the wood and tongue). No legs, no wings, no human features. No shadow or plinth.
- Rust Monster: SE ✓ (head and antennae toward the lower-right). A low four-legged insectoid with a hard plated carapace, two long feathery antennae extending forward, and a feathery / paddle-like tail behind. The antennae are the tell: at 32px they still read as two long forward prongs sticking clearly out of the silhouette, and the paddle tail sticks out the back. The four legs shrink to small nubs but remain visible. Lightest of the four (195 opaque, top y24, feet on y45) because the long antennae-plus-tail span is width-limited — the whole creature including both antennae and the tail is inside the bin, not cropped. Khaki 9.2%. No wings, no human features. No shadow or plinth.
- Gray Ooze: SE ✓ (raised pseudopod toward the lower-right). A low, wide, glistening puddle of featureless grey slime with a raised pseudopod reaching down-right and a smaller secondary drip / tendril beside it. No face, no human head or torso, not a melting person. Sits low and short in the bin as requested (258 opaque, top y32, feet on y45). Zero khaki (0.0%) — almost all mid-grey / seam (#828282), so it stays cool grey rather than warm sludge. Distinct from the #24 lemure, which is a taller khaki melting humanoid sludge (535 opaque, top y14, khaki 33.3%, a vaguely human dripping head and torso). No wings, no legs, no clothes. No shadow or plinth.
- Distinctness at 32px: all four separable (tall snake-haired robed woman with a bow / gaping toothy chest with a reaching tongue / small long-antennaed insectoid / low cool-grey slime puddle with a pseudopod). No shadow, plinth, or unwanted wing creep in any raw.

## Paths
- frames/{slug}/medium/se/mon_{slug}_medium_se_idle_00.png
- fal_raw/{slug}_se_idle_00_{raw,cutout}.png
- previews/mon_smoke27_se_idle_00_fal_x8.png (order medusa|mimic|rust-monster|gray-ooze; #C2B08E; 34px cells; ×8 nearest)


## 2026-11-10 ~9:15 PM ET — AD SE audit
- AD soft-PASS on Mimic, Rust Monster, and Gray Ooze SE. Proceed to NE and 4f on those three. Each NE on y45 to match its SE. The mimic must keep its lid, maw and tongue on NE. The rust monster's antennae and tail must still read.
- Medusa SE REPASS for likeness: at 32px the hair read as a wild bushy mane, so she could pass for a hag. Brief: fewer, thicker serpents — about 5–6 distinct S-curved snakes rising from the head, a lighter grey (#828282 after quantize) head blob on each tip, and clear dark gaps between the snakes. Keep the robe, the neutral bow pose, the non-sexualized design, and the y46 baseline. Up to 2 regenerations if the snakes don't read as separate S-curves at 32px. Do not 4f Medusa until AD approves v2.


## Medusa SE REPASS — 2026-11-10 ~9:18 PM ET
v1 backups taken first (untouched after this): `frames/medusa/medium/se/_v1/mon_medusa_medium_se_idle_00.png`, `fal_raw/medusa_se_idle_00_raw_v1.png`, `fal_raw/medusa_se_idle_00_cutout_v1.png`.
Regen: `google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. First take; no second regen. post: PASS. No isolated 1px fringe specks.

| slug | REPASS gen request_id | REPASS birefnet request_id | opaque | post |
|---|---|---|---|---|
| medusa | 01a1c864-066a-79a1-9668-7b12caea8edb | 01a1c865-a87a-7c72-af60-ef752dfc7085 | 524 | PASS |

The raw shows five distinct thick S-curved snakes rising from the scalp, each ending in a pale grey serpent-head blob, with clear dark gaps of background between the snakes — not a dense bushy mane. At 32px those five S-curves still read as separate prongs with gaps and slightly lighter (#828282) tips, so she no longer reads as a wild-haired hag. The modest full-length robe, khaki sash, quiver, and bow in a neutral standing pose are kept; still fully clothed and non-sexualized. Baseline stayed y46 (top y1) because the tall upright pose is still height-limited by `fal_post_to_bin`'s fit — same class as v1. Opaque 521→524 (+3). Khaki 24.0%→4.4% (the v1 sash was a large khaki panel; v2's sash is a smaller mid-torso accent, and the pale snake heads quantized to #828282 / seam at 5.2% as requested, not to khaki). Comps: 523+1 pivot. No shadow or plinth in the raw. No wings. Medusa 4f remains HOLD pending AD approval of v2.

Preview: `previews/mon_smoke27_medusa_se_old_vs_new_x8.png` (v1 left, REPASS right; #C2B08E; ×8 nearest). The SE strip `previews/mon_smoke27_se_idle_00_fal_x8.png` was rebuilt with the new medusa so it matches the current SE.


## 4-facing (idle_00) — mimic, rust-monster, gray-ooze — 2026-11-10 ~9:20 PM ET
SE = existing AD soft-PASS (untouched, md5 unchanged). NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with the SE raw as likeness reference (`image_urls` = uploaded `fal_raw/{slug}_se_idle_00_raw.png`) so NE keeps the SE design. Then `fal-ai/birefnet/v2` (CDN gen `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE, NW←NE via the smoke26 `mirror()` (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). Script: `scripts/build_smoke27_idle_4facing.py` (smoke26 clone, three slugs only). Medusa is not in this 4f.

Mimic and rust-monster NE were first take. Gray-ooze NE first job (`01a1c864-6a02-7601-b91d-7c427181d782`) failed with `no_media_generated` (no output, not a usage/quota wall; the uploaded SE raw verified at 607558 bytes). One retry with a slightly tighter prompt succeeded; that retry is the frame used below. Required tells were present (mimic lid + maw + tongue; rust-monster antennae and tail; gray-ooze puddle + raised pseudopod), so none of the successful takes were regenerated.

NE baselines already matched their SE after post (all three y45, row 46 empty). No vertical shift was required, so no clip case. No isolated 1px fringe specks (no opaque 8-neighbours) on any NE or on the Medusa REPASS; nothing was cleaned.

| slug | NE gen request_id | NE birefnet request_id | post |
|---|---|---|---|
| mimic | 01a1c864-6a66-7162-a4ed-dd34c8859f6b | 01a1c865-a86d-7c60-9b49-a131da1ca87b | PASS |
| rust-monster | 01a1c864-6a65-76f2-8788-89342c144e3c | 01a1c865-a8c1-7092-a13f-a3eb5c96bd02 | PASS |
| gray-ooze | 01a1c865-9e33-7250-bb4b-2c1d03d28860 (retry; first job no_media_generated) | 01a1c866-5f53-72f0-ba6b-511e29cd29a5 | PASS |

### Validation 12/12 PASS (size · pivot α255 · hard alpha · palette · empty edge cols · non-empty)
| slug | se opaque / base | sw | ne | nw | 8-conn comps (se/sw/ne/nw) | khaki% se→ne |
|---|---|---|---|---|---|---|
| mimic | 658 / y45 | 653 / y45 | 556 / y45 | 550 / y45 | 657+1 / 652+1 / 555+1 / 549+1 | 15.7→7.7 |
| rust-monster | 195 / y45 | 193 / y45 | 207 / y45 | 204 / y45 | 194+1 / 192+1 / 206+1 / 203+1 | 9.2→10.1 |
| gray-ooze | 258 / y45 | 256 / y45 | 270 / y45 | 268 / y45 | 257+1 / 255+1 / 269+1 / 267+1 | 0.0→3.3 |

The +1 on every facing is the detached pivot pixel (16,47) (contract). All three keep their SE baseline on every facing: y45 with a row-46 gap. Tops: mimic y14 SE/SW, y17 NE/NW; rust-monster y24 SE/SW, y23 NE/NW; gray-ooze y32 SE/SW, y29 NE/NW.

No leftover non-1px components on any facing — each frame is one body plus the detached pivot pixel.

No cast shadow or plinth in any NE raw. No wings crept in. The mimic keeps the open lid, toothy maw, and reaching tongue on NE and NW. The rust monster's two long antennae and paddle tail still read on every facing.

md5 guard (`scripts/smoke27_md5_guard.py check-4f` vs `.smoke27_md5_pre_4f.json`, 15508 frames after the #27 SE idle_00 and before this mixed pass): 9 added (smoke27 ne/sw/nw idle_00 for the three soft-PASS slugs) + 1 changed (medusa SE REPASS) + 0 removed. The three #27 soft-PASS SE idle_00 files: 0 modified/removed. Smoke 1–26 idle art: 0 modified/removed by this pass. Concurrent #26 anim during this pass: 12 changed (grick walk_01–03 on all four facings — Animator rewrite, not written here); clips.json was not touched by this pass. Medusa has no ne/sw/nw files.

### Soft notes (4f)
- Mimic NE: a true rear / back-three-quarter — back and side of the wooden chest, the lid still thrown open, the toothy maw visible in the opening, and the khaki tongue reaching toward the upper-right. Lid, maw, and tongue all kept (first take; no regen). Likeness holds: not a plain closed chest. Khaki 15.7→7.7 (the wood face is less dominant from behind, not a redesign). Lighter and 3px shorter: 658→556 (−102, −16%), top y14→y17. The whole figure including lid and tongue is inside the bin, not cropped. No shadow or plinth.
- Rust Monster NE: a true rear / back-three-quarter — back of the plated carapace, the two long feathery antennae reaching toward the upper-right, and the paddle tail out the lower-left. Antennae and tail both still read at 32px as the tell (first take; no regen). Likeness holds: low four-legged insectoid, no wings. Khaki 9.2→10.1 (no real drift). Slightly heavier and 1px taller: 195→207 (+12, +6%), top y24→y23. Still short in the bin because the long antennae-plus-tail span is width-limited; the whole creature is inside the bin, not cropped. No shadow or plinth.
- Gray Ooze NE: a modest turn on a low puddle, same class as earlier modest turns AD has accepted. The raw keeps the featureless grey slime and raises the main pseudopod into a taller hook reaching toward the upper-right, with the secondary lower drip still present — that raised hook is the tell of the turn, and it reads at 32px. Likeness holds: no face, no human head or torso, still a low cool-grey puddle (not a lemure). Slightly taller because of the hook: 258→270 (+12, +5%), top y32→y29 (3px taller). Khaki 0.0→3.3 (a small warm patch under the hook in the raw quantized to khaki; not a redesign). First NE job produced no media (`no_media_generated`); the retry is this frame. No shadow or plinth.
- Distinctness at 32px: all three stay separable on every facing (gaping toothy chest with a reaching tongue / small long-antennaed insectoid / low cool-grey slime puddle with a pseudopod).
- SW/NW mirrors lose 2–6 px from the col-31→clear shift (mimic SE→SW loses 5, NE→NW loses 6; rust-monster SE→SW loses 2, NE→NW loses 3; gray-ooze SE→SW loses 2, NE→NW loses 2).

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_idle_00.png (slug = mimic | rust-monster | gray-ooze)
- frames/medusa/medium/se/mon_medusa_medium_se_idle_00.png (v2) · frames/medusa/medium/se/_v1/mon_medusa_medium_se_idle_00.png (v1)
- fal_raw/{slug}_{se,ne}_idle_00_{raw,cutout}.png · fal_raw/medusa_se_idle_00_{raw,cutout}_v1.png
- previews/mon_{slug}_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw; slug = mimic | rust-monster | gray-ooze in this earlier pass) · previews/mon_smoke27_medusa_se_old_vs_new_x8.png (v1 | v2). Combined 4f grid later rebuilt with a medusa row first; see the Medusa 4f section below.


## 2026-11-10 ~9:25 PM ET — AD 4f / Medusa v2 SE verdict
- AD soft-PASS on Mimic, Rust Monster, and Gray Ooze 4f. Their Art is Done. SE/NE/SW/NW for those three were not touched on this pass.
- AD PASS on Medusa v2 SE. Proceed to Medusa NE and 4f. Baseline must match SE (y46). The five or so thick, separate S-curved snakes with pale grey heads and dark gaps must stay distinct on the NE (seen from behind, still separate prongs, not a mane). Keep the robe, bow, and quiver, and keep her non-sexualized. Up to 2 regenerations if the snakes merge into a mane.


## 4-facing (idle_00) — medusa — 2026-11-10 ~9:25 PM ET
SE = existing AD-passed v2 (untouched, md5 unchanged). NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with the v2 SE raw as likeness reference (`image_urls` = uploaded `fal_raw/medusa_se_idle_00_raw.png`) so NE keeps the v2 design. Then `fal-ai/birefnet/v2` (CDN gen `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE v2, NW←NE via the smoke26 `mirror()` (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). Script: `scripts/build_smoke27_idle_4facing.py` (now four slugs, medusa first).

Medusa NE was first take. The snakes stayed distinct as separate prongs with pale grey heads and dark gaps, both in the raw and at 32px, so no regen was used (0 of 2 allowed). Robe, bow, and quiver kept; still fully clothed and non-sexualized. No isolated 1px fringe specks. NE baseline already matched SE after post (y46, top y1); no vertical shift was required, so no clip case.

| slug | NE gen request_id | NE birefnet request_id | post |
|---|---|---|---|
| medusa | 01a1c86b-235b-74d1-bf44-669f8c620ac9 | 01a1c86b-cb57-7a50-bd81-09dff0576c8e | PASS |

### Validation 4/4 PASS (size · pivot α255 · hard alpha · palette · empty edge cols · non-empty)
| slug | se opaque / base / top | sw | ne | nw | 8-conn comps (se/sw/ne/nw) | khaki% se→ne |
|---|---|---|---|---|---|---|
| medusa | 524 / y46 / y1 | 524 / y46 / y1 | 539 / y46 / y1 | 539 / y46 / y1 | 521+2+1 / 521+2+1 / 538+1 / 538+1 | 4.4→6.5 |

The +1 on every facing is the detached pivot pixel (16,47) (contract). All four facings plant on y46 and fill the bin (top y1), same height-limited class as SE v2. Opaque 524→539 (+15, +2.9%). The pre-existing 2px #1E2228 island on SE v2 at (23,22)+(24,21) — already AD-passed, left as-gen — is inherited by SW; NE and NW are one body plus the detached pivot. SW/NW lose 0 px from the col-31→clear shift because the figure stays inside x=3–28 on both sources.

No isolated 1px fringe specks (no opaque 8-neighbours) on any medusa facing; nothing was cleaned.

No cast shadow or plinth in the NE raw. No wings. The bow and quiver stay on every facing.

md5 guard (`scripts/smoke27_md5_guard.py check-medusa-4f` vs `.smoke27_md5_pre_medusa_4f.json`, 15517 frames after the mixed pass and before this medusa 4f): 3 added (medusa ne/sw/nw idle_00) + 0 changed + 0 removed. The four #27 SE idle_00 files and the nine mimic/rust-monster/gray-ooze ne/sw/nw files: 0 modified/removed. Smoke 1–26 idle art: 0 modified/removed by this pass. Concurrent Animator adds/changes of prior-batch clips, if any, are excluded from the fail set. clips.json was not touched by this pass.

### Soft notes (Medusa 4f)
- Medusa NE: a true rear / back-three-quarter — back of the modest full-length robe, khaki sash and quiver from behind, bow in the right hand, head in profile toward the upper-right. The identity tell holds from behind: six thick S-curved snakes with pale grey (#828282) heads and clear dark gaps between them, still reading at 32px as separate prongs, not a mane and not ordinary curly hair (first take; no regen). Still fully clothed and non-sexualized. Khaki 4.4→6.5 (the back of the sash and quiver are a bit more visible; not a redesign). Slightly heavier: 524→539 (+15, +2.9%), top stays y1, feet stay y46. Seam 5.2% unchanged. No shadow or plinth.
- Distinctness at 32px: medusa stays separable from the other three on every facing (tall snake-haired robed woman with a bow / gaping toothy chest with a reaching tongue / small long-antennaed insectoid / low cool-grey slime puddle).

### Paths
- frames/medusa/medium/{se,sw,ne,nw}/mon_medusa_medium_{f}_idle_00.png (se = AD-passed v2)
- frames/medusa/medium/se/_v1/mon_medusa_medium_se_idle_00.png (v1 backup)
- fal_raw/medusa_{se,ne}_idle_00_{raw,cutout}.png · fal_raw/medusa_se_idle_00_{raw,cutout}_v1.png
- previews/mon_medusa_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw, 1024×384) · previews/mon_smoke27_4facing_idle_00_fal_x8.png (rows medusa|mimic|rust-monster|gray-ooze, 1036×1560)


## 2026-11-10 ~9:30 PM ET — AD Medusa 4f verdict
AD PASS on Medusa 4f (the pre-existing 2px #1E2228 island on SE/SW at (23,22)+(24,21) is accepted). Medusa is Art Done. No further Medusa frame edits on this pass.


## Anim (2026-11-10 ~9:35 PM ET) — built, awaiting AD audit

This bake was not pushed at the time. Medusa is Art Done and was not touched: no medusa anim clips, no overwrite of v2 SE, v1 SE, or NE/SW/NW idle_00.

Roster baked: mimic | rust-monster | gray-ooze — medium 32×48, pivot (16,47). Full 9 clips × 4 facings (idle / walk / react / attack / cast / stun / death / fall / fall_impact). Modest template from #18–#26. All three plant on y45 with a row-46 pivot gap. Never the floater split=H-1. idle_00 was locked first (12/12) and never overwritten.

### Grounding / special handling
- Mimic: legless chest. No mid-body split (`split_y` = foot+1 = 46/46/46/46) so the whole sprite is the torso and nothing tears. Walk is a hop/lurch of the whole chest as one piece; template lift `[0,1,2,1,0,0]` gives a 1px-airborne apex on f02 (like the mastiff/hyena dog hops), with the contract pivot kept planted. Tagged `"legless": true`, `"hop": true`. Tight lid/tongue plates clip a few mid-chest rows on SE/NE f02/f03 when dx would enter col 0 (logged; the row still shifts as a unit).
- Rust Monster: quadruped insect. `detect_standard` splits at 38/38/42/42. Walk is a modest torso bob with lift capped at `[0,1,1,1,0,0]` so the feet stay planted, plus an alternating leg-row ripple on the undercarriage (basilisk-style). Tagged `"quadruped": true`, `"low": true`, `"leg_ripple": true`. SE f02/f03 flip dx to +1 because the antennae already sit in col 1; NE ripple skips the tight foot rows that would enter col 0 (logged).
- Gray Ooze: legless puddle, lemure-like slide. `detect_puddle` splits at the top of the lowest wide bulge (36/36/36/34) so the base stays planted on y45. Whole body shares one uniform dx; lift capped at `[0,1,1,1,0,0]` (no hop, no leg swing). `bridge=body_only`. **Standing vacated_fill rule (from the #26 grick PASS):** on every clip, not just walk, whenever the part above split lifts, the vacated row (`split-1`) is filled by duplicating the split row (full row, all runs) and `body_only` is skipped on that fill so side runs are not cleared. Tagged `"legless": true`, `"vacated_fill": "split_row"` on all 36 gray-ooze clips. SE/NE walk f02/f03 clip a couple of mid-puddle rows on the edge (logged).

The contract pivot stays planted on every frame, including the mimic hop apex and the fall lifts (the whole-sprite lift excludes the pivot row).

### Rust-monster death / fall_impact
The insect is sparse (195–207 opaque). The template death flatten (sh=0.3 / sw=1.25) and fall_impact squash (sw=1.2) detached antennae, tail and foot nubs as new 8-conn islands. Death uses a milder curve `sh=[1,.90,.80,.70,.60]`, `sw=[1,1.04,1.08,1.10,1.12]`; fall_impact uses `sh=[.85,.92,1]`, `sw=[1.08,1.04,1]`. A 1px palette-safe reattach closes any remaining Chebyshev-2 gap to the main body. 5 distinct death frames (last ≠ previous) and 3 distinct fall_impact frames on every facing.

### Counts, validation, gap/component check
456 frames / 108 clips tagged `"smoke": 27`: idle 48 (12), walk 72 (12), react 36, attack 72, cast 48, stun 48, death 60, fall 36, fall_impact 36 (12 each). Validation ALL PASS ×3 (exit 0) plus a full post-bake pixel pass (456/456, palette / hard alpha / empty edge cols / planted pivot, 0 errors). Idle_00 lock: 12/12.

Hard check on every frame of every clip: 0 fully transparent rows inside the body bbox, 0 new 8-conn components vs idle_00. No COMP-EXEMPT islands (each idle_00 is one body + the detached pivot).

### Truncations / lift
Edge-tight plates (mimic lid/tongue, rust-monster antennae, gray-ooze puddle) step dx toward 0 or clip overflow rather than occupy col 0/31; death/fall_impact width-scale is stepped down the same way on all three. 13 walk notes (`truncations.smoke27_walk`) and 139 remaining-clip notes (`truncations.smoke27`). No headroom lift caps (mimic top y14/17, rust-monster y23/24, gray-ooze y29/32).

### md5
Guard `scripts/smoke27_md5_guard.py check-anim` vs `.smoke27_md5_baseline.json` (19251 entries, taken after the 12 idle_00 locks and before any non-idle write): 0 changed / 0 removed on the protected set (s1–s26 including the patched grick walk, the 12 locked #27 idle keys, and medusa idle_00 / v1). Concurrent Art while this bake ran: 1 CHANGED next-batch idle (`swarm-of-bats` SE) and 3 ADDED next-batch NE idles (`shrieker`, `swarm-of-beetles`, `violet-fungus`) — not in the protected set. Medusa v2 SE `b4db6a6d0f5c29f15f0e750c4144ca19`, v1 `3a2b3b3d391a726e7e0468e4d26ee46e`, NE `b327ed39d0245a0250343f100d06efec`, SW `ce768552b4e4d25ac682ace821324c9f`, NW `a763ac08fb4468fd586dec029f16a955` — all unchanged; 0 medusa anim frames written.

### Art soft notes carried into Anim (not compensated)
Mimic stays the gaping toothy chest with a reaching khaki tongue; the lid, maw and tongue remain the tell on every facing (NE keeps them). Heaviest of the three (658/653/556/550 opaque, top y14 SE/SW, y17 NE/NW). Rust Monster stays the low four-legged insectoid with two long forward antennae and a paddle tail; both antennae and the tail stay inside the bin and still read (195/193/207/204 opaque, top y24 SE/SW, y23 NE/NW). Gray Ooze stays a low cool-grey featureless slime puddle with a raised pseudopod (not a lemure); NE raises the hook (258/256/270/268 opaque, top y32 SE/SW, y29 NE/NW). SW/NW mirrors already lost 2–6 px from the col-31→clear shift at Art 4f; Anim does not restore them.

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_{clip}_{ii}.png (slug = mimic | rust-monster | gray-ooze)
- scripts/smoke27_common.py, scripts/build_smoke27_{idle,walk,remaining}_clips.py, scripts/build_smoke27_previews.py, scripts/smoke27_md5_guard.py
- .smoke27_idle00_locked.json · .smoke27_md5_baseline.json
- previews/mon_{slug}_medium_{clip}_cycle_4facing_x8.png · previews/mon_{slug}_medium_anim_all_clips_4facing_x8.png (4768×4832) · previews/mon_smoke27_anim_all_clips_4facing_x8.png (the three stacked)
- /workspace/smoke27_{slug}_x4.png (2384×2416) · /workspace/smoke27_{slug}_walk_f00-f05_x8.png (1624×1560)


## Anim — Medusa (2026-11-10 ~9:42 PM ET) — #27 complete, awaiting AD audit

AD PASSed Medusa 4f Art, so her Anim was baked on the same pipeline as mimic / rust-monster / gray-ooze to finish Smoke #27. This bake was not pushed at the time. The AD-passed v2 SE key (`b4db6a6d0f5c29f15f0e750c4144ca19`) is the locked source; the v1 backup `frames/medusa/medium/se/_v1/mon_medusa_medium_se_idle_00.png` (`3a2b3b3d391a726e7e0468e4d26ee46e`) was not written. Medusa was added to the 12 already-locked idle_00 keys (now 16/16).

### Grounding / special handling
Usual modest humanoid biped template, grounded. `detect_standard` splits at 44/44/46/46 (se/sw/ne/nw). Every facing plants on y46 and fills the bin (top y1) — same class as the #22 dryad/satyr, #25 rakshasa, and #26 doppelganger. Never floater split=H-1.

- Fall lift is capped by the full-bin headroom rule: f00/f02 3→1, f01 4→0 (the f01 1.1 stretch already uses the last free row). lift→0 is accepted. Logged on all facings; stored lift is `[1,0,1]`.
- Walk lift is also capped at `[0,1,1,1,0,0]`. A template lift-2 hop would crop the snake-crown tips off the canvas (they already sit at y0 after the 1px torso bob). Feet stay planted on y46; 4 distinct walk frames on every facing; no walk truncations (the plate has headroom in cols 1 and 30).
- Snakes / hair: the crown (rows y1–16, head line y17 on every facing) moves rigidly with the head. The modest template applies one uniform torso dx and a uniform vertical lift/squash — no per-row strand jitter or independent snake offset. Tagged `"rigid_hair": true`, `"full_bin": true`. `bridge=full_row` (not body_only; vacated_fill is not used).
- The pre-existing 2px #1E2228 island on SE v2 at (23,22)+(24,21), inherited by SW, is AD-accepted and logged `COMP-EXEMPT`. It is not a new component vs idle_00.

### Counts, validation, gap/component check
#27 totals now 608 frames / 144 clips tagged `"smoke": 27` (medusa adds 152 frames / 36 clips; the other three are unchanged): idle 64 (16), walk 96 (16), react 48, attack 96, cast 64, stun 64, death 80, fall 48, fall_impact 48 (16 each). Validation ALL PASS ×3 (exit 0) plus a full post-bake pixel pass (608/608, palette / hard alpha / empty edge cols 0 and 31 / planted pivot, 0 errors). Idle_00 lock: 16/16.

Hard check on every medusa frame of every clip (and a re-check of the other three): 0 fully transparent rows inside the body bbox, 0 new 8-conn components vs idle_00. A 1px palette-safe reattach closes a Chebyshev-3/4 gap on a few death and fall-f01 frames (squash/stretch of the tall plate can briefly leave a snake tip or robe nub off the main blob).

### Truncations / lift
`truncations.smoke27` gained 17 medusa notes (3 fall-lift caps + 14 edge dx/sw steps: attack f03 SE/NE −3→−2, a couple of cast/death/fall_impact width-scale trims). `truncations.smoke27_walk` is unchanged (0 medusa walk notes). No headroom left for a 3–4px fall lift.

### md5
Guard `scripts/smoke27_md5_guard.py check-medusa-anim` vs `.smoke27_md5_baseline_medusa_anim.json` (19814 entries, taken after the 4 medusa idle_00 locks and before any medusa non-idle write; the snapshot includes s1–s26 and all already-baked #27 mimic / rust-monster / gray-ooze frames and clips): 0 changed / 0 added / 0 removed on the protected set. Concurrent Art: none on this pass. v2 SE and v1 backup unchanged.

### Art soft notes carried into Anim (not compensated)
Medusa stays the tall robed woman with a bow and quiver, non-sexualized. The identity tell is the crown of about five or six thick, separate S-curved snakes with pale grey (#828282) heads and dark gaps — still reading as separate prongs, not a mane, on every facing including the NE back-three-quarter. She fills the bin (524/524/539/539 opaque, top y1, feet y46). The AD-accepted 2px island on SE/SW is left as-gen and moves with the body. Anim does not restore any SW/NW mirror loss (0 px on medusa; the figure stays inside x=3–28).

### Paths
- frames/medusa/medium/{se,sw,ne,nw}/mon_medusa_medium_{f}_{clip}_{ii}.png (se idle_00 = AD-passed v2)
- frames/medusa/medium/se/_v1/mon_medusa_medium_se_idle_00.png (v1 backup, untouched)
- previews/mon_medusa_medium_anim_all_clips_4facing_x8.png (4768×4832)
- /workspace/smoke27_medusa_x4.png (2384×2416) · /workspace/smoke27_medusa_walk_f00-f05_x8.png (1624×1560)
- previews/mon_smoke27_anim_all_clips_4facing_x8.png regenerated with all four (rows medusa | mimic | rust-monster | gray-ooze, 4768×19400)

**AD sign-off (records, 2026-11-24 CT): Art+Anim PASS (soft notes as recorded above). This batch was passed by AD in chat and reported Done to Producer at the time; this line only writes that verdict into the doc. Supersedes any "awaiting AD audit" status above.**

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449) and the Group D follow-up:** This note is the production record for smoke #27. The tree now holds 37 medium creatures in Long Dusk Mapping B (26 from #449/#458, plus 11 Group D / couatl kits). From this smoke, the tree includes: `medusa`, `rust-monster`, `gray-ooze`. Held back (kept local): `chain-devil`, `rakshasa`, `doppelganger`, `mimic`.
