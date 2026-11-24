# Smoke #29 — Mediums SE idle_00 (fal)

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449):** This note is the production record for smoke #29. The art PR ships 26 medium creatures that have content spec rows on main, not the original 40. From this smoke, the PR includes: `swarm-of-insects`, `swarm-of-poisonous-snakes`, `swarm-of-quippers`. Held back (kept local, not in the PR): `azer`, `barbed-devil`, `bearded-devil`, `chain-devil`, `erinyes`, `lemure`, `night-hag`, `rakshasa`, `doppelganger`, `mimic`, `swarm-of-beetles`, `swarm-of-centipedes`, `swarm-of-spiders`, `swarm-of-wasps`.

**Status: Art Done (swarm-of-centipedes, swarm-of-insects, swarm-of-poisonous-snakes, swarm-of-quippers) · Anim built for all four, awaiting AD audit**

Roster: Swarm of Centipedes | Swarm of Insects | Swarm of Poisonous Snakes | Swarm of Quippers — all **medium** 32×48, pivot (16,47) opaque.

Source: Non-humanoid SRD monster art tracker, D list (canvas `40c1bf63-1238-4b58-b7b5-7fc0075af844`), the next four mediums after Swarm of Beetles, in tracker order. Slugs: `swarm-of-centipedes`, `swarm-of-insects`, `swarm-of-poisonous-snakes`, `swarm-of-quippers`. Confirmed against the tracker D list on 2026-11-10.

## Pipeline / contract
`google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium` (medium LAST). post: 4/4 PASS. No #29 NE on this pass.

Swarm rule (from AD, based on the bats): each swarm must read at 32px as 5–9 DISTINCT small creatures of the named type, each with a readable silhouette, no speck noise, and no generic clump. Prompted fal for 6–9 large, simple, well-separated creatures on a plain white background in flat construction greys so they survive quantize as islands at least 1px apart. A whole-flock vertical nudge is allowed. Islands too small to be a creature may be deleted; nothing was hand-drawn. Speck clean must not delete real creatures.

## IDs
| slug | gen request_id | birefnet request_id | opaque (after post/nudge/speck) | post |
|---|---|---|---|---|
| swarm-of-centipedes | 01a1c87f-8b5a-7df2-95f2-7225a4e2547d | 01a1c880-7c60-7e73-8842-d6e76a29025a | 247 | PASS |
| swarm-of-insects | 01a1c87f-8bb9-7a92-bd63-d8822a40d4b6 | 01a1c880-7c60-7e73-8842-d6d4e98a1ec5 | 152 | PASS |
| swarm-of-poisonous-snakes | 01a1c87f-8b7c-72a3-86e9-0737ca3a5d2e | 01a1c880-7c36-7991-a3bf-9ac88e46d7e8 | 131 | PASS |
| swarm-of-quippers | 01a1c87f-8bc0-75c3-a5cb-ed217779563e | 01a1c880-7bdf-7963-a904-e1dc8e178f2a | 278 | PASS |

Submission keys (suffix `c9e8b3`): `smoke29-se-{slug}-idle00-c9e8b3` and `smoke29-se-{slug}-idle00-birefnet-c9e8b3`. All first take; no generation came out as mush or the wrong count, so none were regenerated.

## Validation (4/4 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty.

| slug | opaque | top | baseline | khaki | islands | widths (px) | min gap | 8-conn comps (art) |
|---|---|---|---|---|---|---|---|---|
| swarm-of-centipedes | 247 | y16 | y44 | 41 (16.7%) | 7 | 10, 11, 11, 12, 12, 12, 14 | 1px | 40+38+37+35+32+32+32 |
| swarm-of-insects | 152 | y15 | y40 | 29 (19.2%) | 9 | 4, 5, 6, 6, 6, 6, 7, 7, 9 | 3px | 29+24+19+17+17+12+12+12+9 |
| swarm-of-poisonous-snakes | 131 | y23 | y45 | 16 (12.3%) | 7 | 10, 10, 10, 11, 12, 13, 13 | 1px | 22+21+18+18+18+17+16 |
| swarm-of-quippers | 278 | y14 | y40 | 62 (22.4%) | 7 | 8, 9, 9, 9, 9, 9, 9 | 2px | 41+41+39+39+39+39+39 |

No isolated 1px fringe specks on any frame. Speck clean ran after any nudge and removed 0 (it must not delete real creatures: the smallest island is the 9px / 4×4 insect, still a winged body, not a speck). No islands were deleted and nothing was hand-drawn.

## Post steps
- swarm-of-centipedes: none. post already sat on/near the ground at y44. Shapes, widths, and gaps unchanged.
- swarm-of-insects: 4px whole-flock upward nudge so the flock is airborne like the bats (base y44→y40, top y19→y15). Shapes, widths, and gaps unchanged. Pivot stayed at (16,47).
- swarm-of-poisonous-snakes: none. post already sat on the ground at y45.
- swarm-of-quippers: **baseline choice = low hover**, no water plane or plinth. 5px whole-flock upward nudge so the school hangs clearly off the ground at the same airborne seat as the bats (base y45→y40, top y19→y14). Shapes, widths, and gaps unchanged. Pivot stayed at (16,47).

## Soft notes (SE)
- Swarm of Centipedes: 7 distinct segmented wavy bodies with leg ticks, crawling on/near the ground at y44. They do not read as snakes (no smooth S-curve / no single head-tail) and are not a generic clump. First take. No shadow or plinth.
- Swarm of Insects: 9 distinct winged insects of varied sizes (4–9px wide), loosely spaced with 3px minimum gaps, airborne at y40. They do not read as bats (small body + insect wings, not an M/V bat) and not as a ground beetle clump. First take. No shadow or plinth.
- Swarm of Poisonous Snakes: 7 distinct S-curved snakes with visible heads, on the ground at y45. They do not read as centipedes (no leg ticks) and are not a single knot. First take. No shadow or plinth.
- Swarm of Quippers: 7 distinct small fish with fins and tails, in a low hover at y40, no water, waves, ground plane, or plinth painted. They do not read as snakes or insects. First take.
- Distinctness at 32px: all four stay separable (ground wavy legged centipedes / airborne winged insects / ground S-curve snakes with heads / low-hovering finned fish).

## Paths
- frames/{slug}/medium/se/mon_{slug}_medium_se_idle_00.png
- fal_raw/{slug}_se_idle_00_{raw,cutout}.png
- previews/mon_smoke29_se_idle_00_fal_x8.png (34px cells on #C2B08E, ×8 nearest, 1088×384; order centipedes | insects | poisonous-snakes | quippers)

md5 guard (`scripts/smoke29_md5_guard.py check-se` vs `.smoke29_md5_pre_se.json`, 16128 frames after Smoke #28 Art Done and before any #29 frames): 4 added (the four #29 SE idle_00) + 0 changed + 0 removed. Concurrent S28 anim additions (444) are excluded from the fail set. Smoke 1–28 idle art: 0 modified/removed by this pass. No #29 ne/sw/nw files. clips.json was not touched by this pass.


## 2026-11-10 ~9:52 PM ET — AD SE verdict
- AD soft-PASS on all four #29 SE idle_00 frames. The quipper low hover at y40 is accepted. The insects lean bird-like, which is accepted, but on 4f they must stay small and varied in size (4–9px wide, like SE) and must not grow.
- Proceed to #29 NE and 4f for all four. Baselines must match SE: centipedes y44, insects y40, snakes y45, quippers y40. 5–9 distinct islands per facing, gaps of at least 1px after mirroring. Up to 2 regenerations per slug if a swarm comes out as mush, the wrong count, or (for insects) if they grow or turn into birds.


## 4-facing (idle_00) — all four — 2026-11-10 ~9:57 PM ET

SE = existing AD soft-PASS (untouched, md5 unchanged). NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with each SE raw as likeness reference (`image_urls` = uploaded `fal_raw/{slug}_se_idle_00_raw.png`) so NE keeps the SE design. Then `fal-ai/birefnet/v2` (CDN gen `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE, NW←NE via the smoke26 `mirror()` (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). Script: `scripts/build_smoke29_idle_4facing.py`. post: 16/16 PASS. Isolated 1px fringe speck clean ran after any nudge and removed 0 (it must not delete creatures). Nothing was hand-drawn.

| slug | NE gen request_id | NE birefnet request_id | post | regens |
|---|---|---|---|---|
| swarm-of-centipedes | 01a1c885-fa10-7622-9abf-8357d20a1d03 | 01a1c887-6ef9-7da1-91ab-e2544cb97acd | PASS | 0 |
| swarm-of-insects | 01a1c885-f90c-7f33-a392-e5eef4737501 | 01a1c887-6eb8-78c1-97f3-55f7f9efb350 | PASS | 0 |
| swarm-of-poisonous-snakes | 01a1c885-f8da-79c1-98a1-f1c65d64c260 | 01a1c887-6f03-7792-b8ce-8a7cb814e849 | PASS | 0 |
| swarm-of-quippers | 01a1c888-657e-7622-8654-b7eddf15e42c | 01a1c889-3f83-7671-bc24-efb705e02b84 | PASS | 2 |

Submission keys (suffix `034da1`): `smoke29-ne-{slug}-idle00-034da1` and `smoke29-ne-{slug}-idle00-birefnet-034da1`. Quippers used `…-regen2-034da1` / `…-birefnet-regen2-034da1` after two `no_media_generated` (422) failures — not a usage/quota wall. Failed quippers keys/ids (no media produced): first take `01a1c885-f9b0-7170-a810-02e852e6b499` (`smoke29-ne-swarm-of-quippers-idle00-034da1`); regen 1 `01a1c887-490b-7c03-9b0b-7db68c1b0f78` (`…-regen1-034da1`). Regen 2 used a tighter prompt (modeled on earlier modest-turn NE prompts) and succeeded.

### Validation (16/16 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty. After mirroring, every facing has 5–9 distinct islands with a minimum gap of at least 1px. Baselines match SE on every facing.

| slug / facing | opaque | top | baseline | khaki | islands | widths (px) | min gap |
|---|---|---|---|---|---|---|---|
| centipedes se | 247 | y16 | y44 | 41 | 7 | 10, 11, 11, 12, 12, 12, 14 | 1px |
| centipedes ne | 242 | y16 | y44 | 34 | 7 | 10, 11, 11, 11, 12, 12, 14 | 1px |
| centipedes sw | 247 | y16 | y44 | 41 | 7 | 10, 11, 11, 12, 12, 12, 14 | 1px |
| centipedes nw | 242 | y16 | y44 | 34 | 7 | 10, 11, 11, 11, 12, 12, 14 | 1px |
| insects se | 152 | y15 | y40 | 29 | 9 | 4, 5, 6, 6, 6, 6, 7, 7, 9 | 3px |
| insects ne | 166 | y14 | y40 | 26 | 9 | 5, 5, 5, 6, 6, 6, 7, 8, 9 | 3px |
| insects sw | 150 | y15 | y40 | 29 | 9 | 4, 5, 5, 6, 6, 6, 6, 7, 9 | 3px |
| insects nw | 164 | y14 | y40 | 26 | 9 | 5, 5, 5, 5, 6, 6, 6, 8, 9 | 3px |
| poisonous-snakes se | 131 | y23 | y45 | 16 | 7 | 10, 10, 10, 11, 12, 13, 13 | 1px |
| poisonous-snakes ne | 130 | y23 | y45 | 18 | 7 | 10, 10, 10, 11, 12, 13, 13 | 1px |
| poisonous-snakes sw | 130 | y23 | y45 | 16 | 7 | 10, 10, 10, 11, 12, 12, 13 | 1px |
| poisonous-snakes nw | 129 | y23 | y45 | 18 | 7 | 10, 10, 10, 11, 12, 12, 13 | 1px |
| quippers se | 278 | y14 | y40 | 62 | 7 | 8, 9, 9, 9, 9, 9, 9 | 2px |
| quippers ne | 278 | y14 | y40 | 56 | 7 | 8, 9, 9, 9, 9, 9, 9 | 2px |
| quippers sw | 272 | y14 | y40 | 62 | 7 | 8, 8, 8, 9, 9, 9, 9 | 2px |
| quippers nw | 272 | y14 | y40 | 56 | 7 | 8, 8, 8, 9, 9, 9, 9 | 2px |

### Post steps / nudges
- swarm-of-centipedes: none. NE post already sat at the SE ground baseline y44.
- swarm-of-insects: 5px whole-flock upward nudge (base y45→y40, top y19→y14) to match SE airborne y40. Shapes, widths, and gaps unchanged. Pivot stayed at (16,47).
- swarm-of-poisonous-snakes: none. NE post already sat at the SE ground baseline y45.
- swarm-of-quippers: 5px whole-flock upward nudge (base y45→y40, top y19→y14) to match the accepted SE low-hover y40. Shapes, widths, and gaps unchanged. Pivot stayed at (16,47). No water plane or plinth.

SW/NW mirrors lose 0–6 px from the col-31→clear shift (centipedes 0; insects 2; snakes 1; quippers 6). Island count and the minimum gap are unchanged on every mirror.

### Soft notes (4f)
- Centipedes NE: a likeness-preserving modest turn of the same 7 segmented wavy bodies with leg ticks, still on/near the ground at y44. They do not read as snakes. First take. Opaque 247→242. No shadow or plinth.
- Insects NE: a likeness-preserving modest turn of the same 9 winged insects of varied sizes, still airborne at y40. Widths stay in the SE 4–9px range (NE 5–9, max still 9; they did not grow past SE and did not turn into birds). The accepted bird-like lean of the SE silhouettes is preserved. First take. Opaque 152→166 (slightly fuller after quantize, not a size blow-up of individual insects). No shadow or plinth.
- Poisonous snakes NE: a likeness-preserving modest turn of the same 7 S-curved snakes with visible heads, still on the ground at y45. They do not read as centipedes. First take. Opaque 131→130. No shadow or plinth.
- Quippers NE: a likeness-preserving modest turn of the same 7 finned fish in a low hover at y40, no water plane. Two earlier /edit attempts returned `no_media_generated` (422); the second regen succeeded and was used. Opaque unchanged at 278. No shadow or plinth.
- Distinctness at 32px holds on every facing (ground wavy legged centipedes / airborne winged insects of varied sizes / ground S-curve snakes with heads / low-hovering finned fish).

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_idle_00.png
- fal_raw/{slug}_{se,ne}_idle_00_{raw,cutout}.png
- previews/mon_{slug}_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw)
- previews/mon_smoke29_4facing_idle_00_fal_x8.png (rows centipedes | insects | poisonous-snakes | quippers, 1036×1560)

md5 guard (`scripts/smoke29_md5_guard.py check-4f` vs `.smoke29_md5_pre_4f.json`, 16576 frames after the four #29 SE idle_00 and before this 4f pass): 12 added (the four slugs' ne/sw/nw idle_00) + 0 changed + 0 removed. The four #29 SE idle_00 files: 0 modified/removed. Smoke 1–28 idle art: 0 modified/removed by this pass. No concurrent anim additions on this pass. clips.json was not touched by this pass.


## 2026-11-10 ~9:58 PM ET — AD 4f verdict
- AD PASS on Smoke #29 4f (subtle NE turns accepted for swarms). All of Smoke #29 is Art Done. The 16 #29 idle_00 frames were not touched on this note pass.


## Anim (2026-11-10 ~10:01 PM ET) — built, awaiting AD audit

This bake was not pushed at the time. Per-island approach reused from the #28 bats flock baker (`scripts/smoke29_swarms.py`, `scripts/build_smoke29_swarm_clips.py`). idle_00 was locked first (16/16) and never overwritten.

Roster baked: swarm-of-centipedes | swarm-of-insects | swarm-of-poisonous-snakes | swarm-of-quippers — medium 32×48, pivot (16,47). Full 9 clips × 4 facings (idle / walk / react / attack / cast / stun / death / fall / fall_impact). The contract pivot at (16,47) is restored from idle_00 on every frame; no other pivot-row pixels are invented.

### Per-swarm motion
Each idle_00 8-conn island gets its own ±1px bob and a phase-offset horizontal wiggle, plus a modest 1px wing-row raise/lower (falling back to a 1px width change) so the flock does not move in lockstep. A resolver drops only the offending flap, wiggle, or closing bob when two islands would 8-connect, split, or enter col 0/31; the rest of the flock keeps its phase.

- Swarm of Centipedes (7 islands, ground, baseline y44). Not a floater. Walk crawls with a modest uniform dx of −1 on f02/f03 on every facing (leftmost pixel sits at x2, so −1 stays out of col 0; +1 would enter col 31 and is not used). Death/fall settle mildly: flock_dy `[0,0,1,2,2]` / `[1,2,1]`, fall_impact `[2,1,0]`, with a 1px vertical compress on the later death and impact frames. Lowest island ends at y46, inside the bin, still distinct. Tagged `"floater": false`, `"swarm": true`, `"baseline_y": 44`, `split_y` 44/44/44/44, `"vacated_fill": null`.
- Swarm of Insects (9 islands, airborne, baseline y40). Floater, split=H-1=47, no ground contact. Travel dx is 0 on every facing (in-place flutter); several facings already occupy col 1 and/or col 30, so a uniform flock dx would enter an edge column. Islands are loosely spaced (min gap 3px), so most phase-offset bobs land without demotion. Death/fall drift down `[0,1,2,3,4]` / `[2,3,2]`, fall_impact `[4,3,2]` — lowest island ends at y44, still off the ground. Tagged `"floater": true`, `"airborne": true`, `"stationary": true`, `"baseline_y": 40`.
- Swarm of Poisonous Snakes (7 islands, ground, baseline y45). Not a floater. Only 1px of down room (plant y45 → y46 max). SE/NE already occupy col 1 and col 30, so crawl dx is 0 on those facings; SW/NW have room for a modest −1 crawl on walk f02/f03. Several 1px (Chebyshev 2) gaps (pairs 1–3, 2–3, 4–6) — the resolver drops closing bobs and downward flaps rather than merge them. Death/fall settle very mildly: flock_dy `[0,0,1,1,1]` / `[0,1,0]`, fall_impact `[1,1,0]` with a 1px compress on the later death frames and impact f00 (3 distinct impact frames). Tagged `"floater": false`, `"swarm": true`, `"baseline_y": 45`, `split_y` 45/45/45/45.
- Swarm of Quippers (7 islands, low hover, baseline y40). Floater, split=H-1=47, swimming in place, no ground contact, travel dx 0. Min gap 2px (Chebyshev 3) — a closing pair of ±1 wiggles can merge, so some per-island dx/flaps are demoted. Death/fall drift down the same as the insects (`[0,1,2,3,4]` / `[2,3,2]` / `[4,3,2]`), staying off the ground. Tagged `"floater": true`, `"airborne": true`, `"low": true`, `"stationary": true`, `"baseline_y": 40`.

### COMP-EXEMPT / component checks
All idle_00 islands are intentional Art and are logged COMP-EXEMPT:
- Centipedes: 7 islands (SE 37+38+32+35+40+32+32 px, and the matching SW/NE/NW counts; top y16, base y44, min gap 1px).
- Insects: 9 islands (SE 12+12+17+29+19+12+9+17+24 px; NE slightly fuller after quantize but still 9, widths in the SE 4–9px range; top y15/14, base y40, min gap 3px).
- Poisonous snakes: 7 islands (SE 22+16+18+18+21+17+18 px; top y23, base y45, min gap 1px).
- Quippers: 7 islands (SE 39+39+39+39+39+41+41 px; top y14, base y40, min gap 2px).

Hard check on every frame of every clip: component count stays exactly at the idle_00 count (never a merge, never a split); palette / hard alpha / empty edge cols 0 and 31; no new pivot-row pixels; the "no transparent rows in bbox" rule is applied **per island**, not across the gaps between creatures.

### Counts, validation, truncations
608 frames / 144 clips tagged `"smoke": 29`: idle 64 (16), walk 96 (16), react 48, attack 96, cast 64, stun 64, death 80, fall 48, fall_impact 48 (16 clips each). Validation ALL PASS (exit 0) plus a full post-bake pixel pass (608/608, 0 errors). Idle_00 lock: 16/16 (0 drift).

370 notes in `truncations.smoke29`: 16 facing-level crawl/floater dx decisions (centipedes crawl −1 on all four facings; snakes crawl 0 on SE/NE and −1 on SW/NW; insects and quippers travel dx 0) and 354 per-frame demotions (skipped flaps, closing bobs, or edge-bound wiggles). No frame-count truncations. No headroom issues (floaters have 6px of down room to y46; centipedes 2px; snakes 1px, which is why their settle is capped at +1).

### md5
Guard `scripts/smoke29_md5_guard.py check-anim` vs `.smoke29_md5_baseline.json` (20758 entries, taken after the 16 idle_00 locks and before any non-idle write): 0 changed / 0 added / 0 removed on the protected set (s1–s28 including all already-baked #28 frames, and the 16 locked #29 idle keys). Concurrent Art: none this pass.

Locked #29 idle_00 (unchanged):
- swarm-of-centipedes se `88edcdecdbc72459671b9505406ba496` / sw `9dd1a4e3be92b27695602aeb7d6c4677` / ne `922cf461dd5485c7558085c0c00cfb98` / nw `76a03afd2a02d4c06ec6a15fac0fa05b`
- swarm-of-insects se `8174f9aac2c5d26ca6f22c9f6fc7e900` / sw `c6e2e96dc5db52525d07756ec61f5e5e` / ne `fa264c251ae1c705c000fc5ef19a7e38` / nw `db1f9d5cd71cc8df5ceff3d2ad602ad8`
- swarm-of-poisonous-snakes se `30f8a83ca59d69923b7e12e4a8e80406` / sw `ee2105f47447a4fd28ad5d2952527f70` / ne `4160d453cb04d64851cb9599cf7d073f` / nw `37268cadec47b67326a2b290fd6e5af0`
- swarm-of-quippers se `e0cf0b1d4fc328da8d999e8cf65f4248` / sw `f920d54ed37a0e50989e70f2489c7b71` / ne `23912823c8bd3dc4e420a0ea55ebb36d` / nw `05cbaa7e048470dc38014987a0d5a0c9`

### Art soft notes carried into Anim (not compensated)
Centipedes stay 7 distinct segmented wavy bodies with leg ticks, on/near the ground at y44, not snakes. Insects stay 9 distinct winged insects of varied sizes (4–9px wide, the accepted bird-like lean of the SE silhouettes is preserved), airborne at y40, not bats and not a beetle clump. Poisonous snakes stay 7 distinct S-curved snakes with visible heads, on the ground at y45, not centipedes. Quippers stay 7 distinct finned fish in a low hover at y40, no water plane. SW/NW mirrors already lost 0–6 px from the col-31→clear shift at Art 4f (centipedes 0, insects 2, snakes 1, quippers 6); Anim does not restore them.

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_{clip}_{ii}.png
- scripts/smoke29_swarms.py, scripts/build_smoke29_swarm_clips.py, scripts/build_smoke29_previews.py, scripts/smoke29_md5_guard.py
- .smoke29_idle00_locked.json · .smoke29_md5_baseline.json
- previews/mon_{slug}_medium_{clip}_cycle_4facing_x8.png · previews/mon_{slug}_medium_anim_all_clips_4facing_x8.png (4768×4832)
- previews/mon_smoke29_anim_all_clips_4facing_x8.png (all four stacked in tracker order centipedes | insects | poisonous-snakes | quippers, 4768×19400)
- /workspace/smoke29_{slug}_x4.png (2384×2416) · /workspace/smoke29_{slug}_walk_f00-f05_x8.png (1624×1560)

**AD sign-off (records, 2026-11-24 CT): Art+Anim PASS (soft notes as recorded above). This batch was passed by AD in chat and reported Done to Producer at the time; this line only writes that verdict into the doc. Supersedes any "awaiting AD audit" status above.**

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449):** This note is the production record for smoke #29. The art PR ships 26 medium creatures that have content spec rows on main, not the original 40. From this smoke, the PR includes: `swarm-of-insects`, `swarm-of-poisonous-snakes`, `swarm-of-quippers`. Held back (kept local, not in the PR): `azer`, `barbed-devil`, `bearded-devil`, `chain-devil`, `erinyes`, `lemure`, `night-hag`, `rakshasa`, `doppelganger`, `mimic`, `swarm-of-beetles`, `swarm-of-centipedes`, `swarm-of-spiders`, `swarm-of-wasps`.
