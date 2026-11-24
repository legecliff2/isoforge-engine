# Smoke #28 — Mediums SE idle_00 (fal)

**Status: Art Done (shrieker, violet-fungus, swarm-of-bats, swarm-of-beetles) · Anim built for all four, awaiting AD audit**

Roster: Shrieker | Violet Fungus | Swarm of Bats | Swarm of Beetles — all **medium** 32×48, pivot (16,47) opaque.

Source: Non-humanoid SRD monster art tracker, D list (canvas `40c1bf63-1238-4b58-b7b5-7fc0075af844`), the next four mediums after Gray Ooze, in tracker order. Slugs: `shrieker`, `violet-fungus`, `swarm-of-bats`, `swarm-of-beetles`.

## Pipeline / contract
`google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium` (medium LAST). post: 4/4 PASS. No #28 NE on this pass.

## IDs
| slug | gen request_id | birefnet request_id | opaque (after speck clean) | post |
|---|---|---|---|---|
| shrieker | 01a1c86e-1c98-7da1-abd5-b1d3f31bbc90 | 01a1c86e-e271-7410-b026-9e6038a928ff | 534 | PASS |
| violet-fungus | 01a1c86e-1c00-7403-9f90-e47d98163a70 | 01a1c86e-e22a-77f1-98e8-2b131cb0b5c3 | 427 | PASS |
| swarm-of-bats | 01a1c86e-1c35-7fa3-b5f8-533a984d2ca5 | 01a1c86e-e1df-7a31-a903-4243ef25c647 | 166 | PASS |
| swarm-of-beetles | 01a1c86e-1c96-7310-a69f-015876bb5619 | 01a1c86e-e248-75a2-95ef-bbb757e1de4c | 268 | PASS |

All first take; no generation missed its tell, so none were regenerated.

## Validation (4/4 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty.

| slug | opaque | top | baseline | khaki% | seam% | 8-conn comps |
|---|---|---|---|---|---|---|
| shrieker | 534 | y17 | y45 | 18.2 | 10.5 | 533+1 pivot |
| violet-fungus | 427 | y13 | y45 | 25.1 | 0.5 | 426+1 pivot |
| swarm-of-bats | 166 | y21 | y42 | 0.6 | 4.2 | 149 + two 3px + five 2px + 1 pivot |
| swarm-of-beetles | 268 | y27 | y44 | 5.2 | 1.9 | 264 + one 3px + 1 pivot |

Feet: shrieker and violet-fungus plant on y45 with a row-46 gap (same floater as azer / wolf / most prior mediums). Both are width-limited by `fal_post_to_bin`'s fit (the shrieker's broad cap and the violet fungus's reaching tendrils span cols 1–30) — the whole figure is inside the bin, not cropped. Swarm of beetles plants on y44 after an isolated y45 fringe speck was cleaned (see below); it sits low and short (top y27), as a ground clump should. Swarm of bats plants on y42 (top y21) — a little higher off the ground, which matches the brief of a loose clump low in the air rather than a ground sit; also width-limited, whole cluster inside the bin, not cropped. Not a post-script miss on any of the four.

No cast shadow or plinth in any raw. No human features or invented Xenogarth IP. Smoke 1–27 frames were not edited on this pass.

## Speck clean (no fal call)
Isolated 1px fringe speck with no opaque 8-neighbours, set to alpha 0. Confirmed isolated first. No other pixel changed.
- violet-fungus SE (22,29) #1E2228 — opaque 428→427. Leftover comps are 426+1 pivot. Baseline stayed y45; pivot (16,47) unchanged.
- swarm-of-bats SE — six isolated 1px: (2,24) #1E2228, (21,31) #1E2228, (25,37) #1E2228, (30,43) #5C6370, (24,44) #5C6370, (28,44) #828282. Opaque 172→166. After the two y44 fringe pixels were removed, the remaining cluster's baseline is y42 (the lowest leftover 2px island is at (18,41)+(18,42)). Pivot unchanged.
- swarm-of-beetles SE — five isolated 1px: (5,27) #5C6370, (2,33) #1E2228, (1,37) #1E2228, (5,40) #5C6370, (27,45) #5C6370. Opaque 273→268. The (27,45) fringe pixel had been the only opaque pixel on y45, so after the clean the remaining clump plants on y44. Pivot unchanged.
Shrieker had none.

Leftover non-1px components left as-gen for AD (these are outlying individuals in the swarms, not fringe noise; larger art islands are kept):
- swarm-of-bats: two 3px islands at (11–13,23) and (26–28,39); five 2px islands at (3,21)+(2,22), (23,30)+(24,31), (5,34)+(6,34), (27,36)+(28,37), and (18,41)+(18,42).
- swarm-of-beetles: one 3px island at (4,29)+(3,30)+(3,31).
Shrieker and violet-fungus are each one body plus the detached pivot pixel.

## Soft notes (SE)
- Shrieker: SE ✓ (broad cap toward the lower-right). A large grey mushroom with a broad, wrinkled cap and a thick stout stalk. No limbs, no tendrils, no face. The broad cap and thick stalk are the tell and both survive at 32px. Heaviest of the four (534 opaque, top y17, feet on y45); the wide cap is width-limited by the fit, so it sits mid-height in the bin, fully inside, not cropped. Khaki 18.2% (warm midtones on the cap, quantized to #8C7A62). Distinct from the violet fungus. No shadow or plinth.
- Violet Fungus: SE ✓ (reaching tendrils toward the lower-right). A mushroom with a smaller, darker cap and a stalk, plus about four long tentacle-like tendrils reaching out down-right from the base. It does not read as a plain mushroom and does not look like the shrieker. The tendrils are the tell and still read at 32px as separate reaching prongs. 427 opaque, top y13, feet on y45 — taller than the shrieker because of the tendril spread, also width-limited (cols 1–30), fully inside the bin, not cropped. Khaki 25.1% (the tendrils and lower stalk). No face, no limbs in the human sense, no wings. No shadow or plinth.
- Swarm of Bats: SE ✓ (the clump drifting toward the lower-right). One sprite that reads as a loose cluster of many small bats flying low in the air, not a single bat and not one large animal. In the raw, many individual bat silhouettes with tiny wings are visible; at 32px those collapse into a speckled diagonal mass, but it still reads as a flock of many small flying creatures rather than one animal. Lightest of the four (166 opaque, top y21, lowest mass on y42) — slightly off the ground, which matches a low-air clump. Almost no khaki (0.6%). The leftover 2–3px islands are outlying individual bats, left as-gen. No shadow or plinth.
- Swarm of Beetles: SE ✓ (the clump toward the lower-right). One sprite that reads as a loose cluster of many small beetles gathered on the ground, not a single beetle and not one large insect. At 32px the individual beetles shrink into a low, dark, textured clump, still clearly a mass of many small bugs and clearly distinct from the airborne bat flock. 268 opaque, top y27, feet on y44 — low and short, as a ground swarm should be. Khaki 5.2%. One leftover 3px island (an outlying beetle) left as-gen. No wings. No shadow or plinth.
- Distinctness at 32px: all four separable (broad-capped thick-stalk mushroom / mushroom with reaching tendrils / loose airborne bat flock / low ground beetle clump). No shadow, plinth, or unwanted humanoid creep in any raw.

## Paths
- frames/{slug}/medium/se/mon_{slug}_medium_se_idle_00.png
- fal_raw/{slug}_se_idle_00_{raw,cutout}.png
- previews/mon_smoke28_se_idle_00_fal_x8.png (order shrieker|violet-fungus|swarm-of-bats|swarm-of-beetles; #C2B08E; 34px cells; ×8 nearest; 1088×384)

md5 guard: `scripts/smoke28_md5_guard.py check-se` vs `.smoke28_md5_pre_se.json` (15520 frames after Medusa 4f and before any #28 frames). Expected: 4 added (#28 SE idle_00) + 0 changed + 0 removed. Concurrent S21–S27 Animator adds/changes, if any, are excluded from the fail set. clips.json was not touched by this pass. No #28 NE / SW / NW files.


## 2026-11-10 ~9:36 PM ET — AD SE audit
- AD soft-PASS on Shrieker, Violet Fungus, and Swarm of Beetles SE. Proceed to NE and 4f on those three. Each NE baseline must match its SE (shrieker and violet-fungus y45, beetles y44). The violet fungus tentacles must stay readable on NE.
- Swarm of Bats SE REPASS for likeness: at 32px it read as noise and looked nearly identical to the beetle clump. Brief: 6–9 distinct bats, each a 3–5px wide M/V wing silhouette with a dark body pixel, spread loosely through the air with clear gaps between them. Keep it airborne (off the ground) and avoid single-pixel speck noise. Up to 2 regenerations. Do not 4f bats until AD approves v2.


## 4-facing (idle_00) — shrieker, violet-fungus, swarm-of-beetles — 2026-11-10 ~9:36 PM ET
SE = existing AD soft-PASS (untouched, md5 unchanged). NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with the SE raw as likeness reference (`image_urls` = uploaded `fal_raw/{slug}_se_idle_00_raw.png`) so NE keeps the SE design. Then `fal-ai/birefnet/v2` (CDN gen `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE, NW←NE via the smoke26 `mirror()` (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). Script: `scripts/build_smoke28_idle_4facing.py` (three slugs only; a bats row can be added later).

All three NE were first take. Required tells were present (shrieker broad cap + thick stalk, no tentacles; violet-fungus tentacles still readable; beetles still a ground clump of many small beetles), so none of the successful takes were regenerated.

Shrieker and violet-fungus NE baselines already matched their SE after post (both y45). Swarm-of-beetles NE sat at y45 after post (and after the 1px speck clean below); it was shifted up 1px with no fal call so the baseline matches SE at y44. Top went y26→y25. No clip (the figure stayed inside y1–y46). No other baseline shift was required.

| slug | NE gen request_id | NE birefnet request_id | post |
|---|---|---|---|
| shrieker | 01a1c872-88c2-78d3-af18-f5237094e29a | 01a1c873-bb1f-7ac3-a454-753939fd0c3d | PASS |
| violet-fungus | 01a1c872-8877-78d3-8618-8cb0f5410e4d | 01a1c873-bb6a-7232-ae00-bf9e40d412cd | PASS |
| swarm-of-beetles | 01a1c872-8884-7700-98ce-8b56eead2375 | 01a1c873-bb22-7091-8433-f4636e67c422 | PASS |

### Speck clean (no fal call)
Isolated 1px fringe speck with no opaque 8-neighbours, set to alpha 0. Confirmed isolated first.
- swarm-of-beetles NE (10,28) #1E2228 — opaque 252→251, then the y44 baseline shift (see above). Pivot unchanged.
Shrieker NE and violet-fungus NE had none.

Leftover non-1px components left as-gen for AD:
- violet-fungus NE/NW: two 2px islands at (25,34)+(26,33) and (29,23)+(30,22) on NE (and their NW mirrors) — tendril tips, part of the tell.
- swarm-of-beetles SE/SW: one 3px island at (4,29)+(3,30)+(3,31) (already noted on SE). NE/NW: one 4px island at (5,33)+(6,33)+(7,31)+(7,32) on NE (and its NW mirror) — an outlying beetle.
Shrieker is one body plus the detached pivot on every facing.

### Validation 12/12 PASS (size · pivot α255 · hard alpha · palette · empty edge cols · non-empty)
| slug | se opaque / base / top | sw | ne | nw | 8-conn comps (se/sw/ne/nw) | khaki% se→ne |
|---|---|---|---|---|---|---|
| shrieker | 534 / y45 / y17 | 532 / y45 / y17 | 534 / y45 / y17 | 532 / y45 / y17 | 533+1 / 531+1 / 533+1 / 531+1 | 18.2→15.4 |
| violet-fungus | 427 / y45 / y13 | 424 / y45 / y13 | 427 / y45 / y15 | 424 / y45 / y15 | 426+1 / 423+1 / 422+2+2+1 / 419+2+2+1 | 25.1→25.1 |
| swarm-of-beetles | 268 / y44 / y27 | 268 / y44 / y27 | 251 / y44 / y25 | 251 / y44 / y25 | 264+3+1 / 264+3+1 / 246+4+1 / 246+4+1 | 5.2→0.4 |


The +1 on every facing is the detached pivot pixel (16,47) (contract). All three keep their SE baseline on every facing: shrieker and violet-fungus y45 with a row-46 gap; swarm-of-beetles y44. Tops: shrieker y17 on every facing; violet-fungus y13 SE/SW, y15 NE/NW; swarm-of-beetles y27 SE/SW, y25 NE/NW.

No cast shadow or plinth in any NE raw. The violet fungus tentacles stay readable on NE and NW.

SW/NW mirrors lose 0–3 px from the col-31→clear shift (shrieker SE→SW and NE→NW lose 2; violet-fungus SE→SW and NE→NW lose 3; swarm-of-beetles loses 0).

### Soft notes (4f)
- Shrieker NE: a modest turn on a radial mushroom, same class as earlier modest turns AD has accepted. The raw keeps the same large grey mushroom with a broad wrinkled cap and a thick stout stalk, no limbs and no tentacles. Likeness holds. Opaque unchanged at 534, top stays y17, feet stay y45. Khaki 18.2→15.4 (a slight drop, not a redesign). No shadow or plinth.
- Violet Fungus NE: a true-enough turn with the tentacles still clearly reaching toward the upper-right — about four distinct tendril prongs, including two 2px tips left as-gen. It does not read as a plain mushroom or as the shrieker. Likeness holds. Opaque unchanged at 427, top y13→y15 (2px shorter), feet stay y45. Khaki 25.1→25.1 (no drift). The whole figure including the tendrils is inside the bin, not cropped. No shadow or plinth.
- Swarm of Beetles NE: a modest turn of the same ground clump, slightly more elongated toward the upper-right. It still reads as a cluster of many small beetles, not a single beetle and not a flying swarm. Lighter: 268→251 (−17, −6%), top y27→y25 (2px taller after the 1px up-shift to match the SE y44 baseline). Khaki 5.2→0.4 (the warmer shell accents recede from behind; not a redesign). First take; one isolated 1px fringe speck cleaned, then the baseline shift. No shadow or plinth.
- Distinctness at 32px: all three stay separable on every facing (broad-capped thick-stalk mushroom / mushroom with reaching tendrils / low ground beetle clump).

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_idle_00.png (slug = shrieker | violet-fungus | swarm-of-beetles)
- fal_raw/{slug}_{se,ne}_idle_00_{raw,cutout}.png
- previews/mon_{slug}_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw) · previews/mon_smoke28_4facing_idle_00_fal_x8.png (rows shrieker|violet-fungus|swarm-of-beetles, 1036×1172; room to add a bats row later)


## Swarm-of-bats SE REPASS — 2026-11-10 ~9:36 PM ET
v1 backups taken first (untouched after this): `frames/swarm-of-bats/medium/se/_v1/mon_swarm-of-bats_medium_se_idle_00.png`, `fal_raw/swarm-of-bats_se_idle_00_raw_v1.png`, `fal_raw/swarm-of-bats_se_idle_00_cutout_v1.png`.
Regen: `google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. First take; no second regen. post: PASS. No isolated 1px fringe specks (and speck clean must not delete small bats: each bat is its own island of at least 3px; the smallest here is 13px).

| slug | REPASS gen request_id | REPASS birefnet request_id | opaque | post |
|---|---|---|---|---|
| swarm-of-bats | 01a1c872-88b8-7b31-b2f5-e61460984653 | 01a1c873-bc09-77c1-a4e4-e650222de58b | 164 | PASS |

The raw is seven large, simple, clearly separated bats with wings spread in M/V shapes and dark bodies, with white gaps between them, flying in a loose airborne flock — not a tight pile and not a noisy spray of dots. After birefnet + quantize, two mid-right bats in the raw share one 10×9 island, so the 32px frame has **6 distinct bat islands** (inside the 6–9 target). Per-bat widths (bbox): 9, 10, 7, 10, 11, 10 px. Heights 6, 9, 4, 6, 6, 8. Pixel counts 25, 39, 13, 26, 33, 27. All pairwise gaps are at least 1px (minimum is a 1px gap between bats 2 and 4; the rest are 2–14px). No single-pixel speck noise.

Those widths are larger than the 3–5px target. A trial deterministic shrink of the already-quantized 32px islands (NN-scale each fal-made island toward 5px) destroyed the M/V silhouettes, merged two islands, and left a 1px remnant, so that shrink was **not used**. The fal/quantize result was already clearly separated bats, not tiny or noisy. The only post step applied: a 5px upward nudge of the whole fal-made flock so it is airborne (base y45→y40, top y18→y13), clearly off the ground relative to the beetles (y44). Shapes, widths, and gaps were not changed. Pivot stayed at (16,47). No bats were hand-drawn.

Likeness vs the beetles: v2 is a loose airborne flock of a few large M/V wing silhouettes with visible gaps; the beetles remain a low, dense ground clump of many tiny bodies. They no longer look nearly identical. Khaki 11.0% (two of the bats have khaki wings). Contract PASS (32×48, pivot (16,47) α255, hard alpha, palette, empty edge cols). Comps: 39+33+27+26+25+13+1 pivot. Swarm-of-bats 4f remains HOLD pending AD approval of v2.

Preview: `previews/mon_smoke28_swarm-of-bats_se_old_vs_new_x8.png` (v1 left, REPASS right; #C2B08E; 34px cells; ×8 nearest; 528×384). The SE strip `previews/mon_smoke28_se_idle_00_fal_x8.png` was rebuilt with the new bats so it matches the current SE.

md5 guard (`scripts/smoke28_md5_guard.py check-4f` vs `.smoke28_md5_pre_4f.json`, 15524 frames after the #28 SE idle_00 and before this mixed pass): 9 added (shrieker, violet-fungus, swarm-of-beetles ne/sw/nw idle_00) + 1 changed (swarm-of-bats SE REPASS) + 0 removed. The three #28 soft-PASS SE idle_00 files: 0 modified/removed. Smoke 1–27 idle art: 0 modified/removed by this pass. Concurrent prior-batch anim, if any, is excluded from the fail set. clips.json was not touched by this pass. Swarm-of-bats has no ne/sw/nw files.


## 2026-11-10 ~9:39 PM ET — AD 4f / bats v2 SE verdict
- AD soft-PASS on Shrieker, Violet Fungus, and Swarm of Beetles 4f. Their Art is Done. SE/NE/SW/NW for those three were not touched on this pass.
- AD PASS on Swarm of Bats SE v2 (the 7–11px bat widths are accepted). Proceed to Swarm of Bats NE and 4f. About 6 distinct bat islands (6–9 is OK), each with at least a 1px gap, airborne with the base near y40 like SE. No 1px speck noise; speck clean must not remove bats. Up to 2 regenerations if bats merge or come out as mush.


## 4-facing (idle_00) — swarm-of-bats — 2026-11-10 ~9:44 PM ET

SE v2 = existing AD PASS (untouched, md5 unchanged). NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with the v2 SE raw as likeness reference (`image_urls` = uploaded `fal_raw/swarm-of-bats_se_idle_00_raw.png`) so NE keeps the same loose flock of large, simple M/V bats. Then `fal-ai/birefnet/v2` (CDN gen `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE v2, NW←NE via the smoke26 `mirror()` (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). First take; bats stayed distinct, so no regen. post: PASS. Isolated 1px fringe speck clean ran after the nudge and removed 0 (it must not delete bats: each bat is its own island of at least 3px; the smallest here is 13px).

| slug | NE gen request_id | NE birefnet request_id | post |
|---|---|---|---|
| swarm-of-bats | 01a1c87b-a354-7a10-9cf3-c79d27b6c465 | 01a1c87c-a3cc-7340-b0f3-ef6cf9138110 | PASS |

Submission key (NE /edit): `smoke28-ne-swarm-of-bats-idle00-d014be`. Birefnet key: `smoke28-ne-swarm-of-bats-idle00-birefnet-d014be`. Uploaded SE raw: `https://v3b.fal.media/files/b/0ab1fbc7/VL5vWgSySUkQxjqjkXMHx_swarm-of-bats_se_idle_00_raw.png`.

The 1024 NE raw is seven large, simple, clearly separated M/V-wing bats with dark bodies and white gaps, plus one 1px speck that did not survive the bin. After birefnet + quantize, two mid-right bats in the raw share one island (same class of merge as SE v2), so every 32px facing has **6 distinct bat islands** (inside the 6–9 target). No single-pixel speck noise on any facing. After mirroring, islands stay distinct with a minimum 1px gap on SW and NW.

### Validation (4/4 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty.

| facing | opaque | top | baseline | khaki | islands | widths (px) | min gap | 8-conn comps (art) |
|---|---|---|---|---|---|---|---|---|
| se | 164 | y13 | y40 | 18 | 6 | 7, 9, 10, 10, 10, 11 | 1px | 39+33+27+26+25+13 |
| ne | 161 | y13 | y40 | 16 | 6 | 7, 9, 10, 10, 10, 10 | 1px | 37+32+27+26+25+13 |
| sw | 163 | y13 | y40 | 18 | 6 | 7, 9, 9, 10, 10, 11 | 1px | 39+33+27+25+25+13 |
| nw | 160 | y13 | y40 | 16 | 6 | 7, 9, 9, 10, 10, 10 | 1px | 37+32+27+25+25+13 |

NE post sat at top y18 / base y45 (the same height-limited bin seat SE had before its airborne nudge). The only post step applied: a 5px upward whole-flock nudge so the base matches SE (y45→y40, top y18→y13). Shapes, widths, and gaps were not changed. Pivot stayed at (16,47). No bats were hand-drawn. SW/NW mirrors lose 1 px from the col-31→clear shift (SE 164→SW 163, NE 161→NW 160); island count and the 1px minimum gap are unchanged.

### Soft notes (bats 4f)
- NE is a likeness-preserving modest turn of the same loose airborne flock: large simple M/V silhouettes with dark bodies and visible gaps, clearly off the ground, not a tight pile and not a noisy spray of dots. It does not read as the beetle ground clump. First take; no regen.
- At 32px the island layout is close to SE (same six-bat arrangement, small palette/shape tweaks — NE mid-right island is 10px wide / 32 px vs SE 11px / 33 px). The 1024 raw still shows seven distinct bats at varied angles. Likeness holds.
- Airborne on every facing at base y40, same as SE, well above the beetle clump at y44.
- Distinctness vs the rest of #28 holds on every facing (broad-capped thick-stalk mushroom / mushroom with reaching tendrils / loose airborne M/V flock / low ground beetle clump).

### Paths
- frames/swarm-of-bats/medium/{se,sw,ne,nw}/mon_swarm-of-bats_medium_{f}_idle_00.png
- fal_raw/swarm-of-bats_{se,ne}_idle_00_{raw,cutout}.png (SE is the AD-PASS v2)
- previews/mon_swarm-of-bats_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw)
- previews/mon_smoke28_4facing_idle_00_fal_x8.png now has four rows: shrieker | violet-fungus | swarm-of-bats | swarm-of-beetles (1036×1560)

md5 guard (`scripts/smoke28_md5_guard.py check-bats-4f` vs `.smoke28_md5_pre_bats_4f.json`, 15977 frames after the #28 mixed 4f + bats SE v2 and before this bats 4f): 3 added (swarm-of-bats ne/sw/nw idle_00) + 0 changed + 0 removed. The four #28 SE idle_00 files and the nine shrieker/violet-fungus/swarm-of-beetles ne/sw/nw files: 0 modified/removed. Smoke 1–27 idle art: 0 modified/removed by this pass. Concurrent S27 anim additions (148) are excluded from the fail set. clips.json was not touched by this pass.


## 2026-11-10 ~9:45 PM ET — AD Swarm-of-bats 4f verdict
- AD PASS on Swarm of Bats 4f. All of Smoke #28 is Art Done (shrieker, violet-fungus, swarm-of-bats, swarm-of-beetles). The 16 #28 idle_00 frames were not touched on this note pass.


## Anim (2026-11-10 ~9:50 PM ET) — built, awaiting AD audit

Bake is local only (no repo / git / PR). Swarm-of-bats is Art Done (4f PASSed during this window) and was not touched: no bats anim clips, no overwrite of SE v2, the v1 SE backup, or NE/SW/NW idle_00.

Roster baked: shrieker | violet-fungus | swarm-of-beetles — medium 32×48, pivot (16,47). Full 9 clips × 4 facings (idle / walk / react / attack / cast / stun / death / fall / fall_impact). Modest template from #18–#27. Shrieker and violet-fungus plant on y45 with a row-46 pivot gap; swarm-of-beetles plants on y44. Never the floater split=H-1. idle_00 was locked first (12/12, not bats) and never overwritten.

### Grounding / special handling
- Shrieker: stationary body_only mushroom. `detect_stalk` splits at 36/36/36/36 (cap y17–35, stalk y36–45). Walk is an in-place pulse, locomotion dx all 0 (no travel): lift `[0,1,1,1,0,0]` plus a uniform cap lean of rows above the split on f02 (−1) and f03 (+1), then a planted squash on f05 — four distinct frames (plant, lift, lift+lean, squash). The stalk stays planted. Tagged `"stationary": true`, `"bridge": "body_only"`, `"vacated_fill": "split_row"`. Tight cap edges clip a few rows on the lean (logged; the band still shifts as a unit).
- Violet Fungus: stationary body_only mushroom with tendrils. `detect_tendril_split` splits at 25/25/22/22. Walk is the same in-place pulse (dx all 0, lift capped at 1) with a modest tendril-run sway on f01 (−1) and f03 (+1): side runs move 1px, the stalk core stays, and a run is left unshifted if any pixel would enter col 0/31 (so the AD-accepted 2px tips are not clipped into a new 1px island). Tendrils stay attached. Tagged `"stationary": true`, `"tendril_sway": true`, `"bridge": "body_only"`, `"vacated_fill": "split_row"`.
- Swarm of Beetles: low ground-clump shimmer/crawl. `detect_standard` splits at 43/43/40/40. Base stays planted on y44. Walk is a 1px torso bob plus a crawl dx `[0,0,-1,-1,0,0]` (no staggered undercarriage ripple — that would tear the gappy clump). Existing outlying-beetle islands are kept as they are (COMP-EXEMPT); no new islands were added. Tagged `"swarm": true`, `"low": true`, `"bridge": "full_row"`, `"vacated_fill": "split_row"`.

**Standing vacated_fill rule (from the #26 grick PASS, standing for every body_only creature and applied here to all three):** on every clip, not just walk, whenever the part above split lifts, the vacated row (`split-1`) is filled by duplicating the split row (full row, main-body runs only — idle_00 extra islands that sit on the split row, e.g. a violet-fungus tendril tip, are not cloned into the gap) and `body_only` is skipped on that fill so side runs are not cleared. Tagged `"vacated_fill": "split_row"` on all 108 clips.

The contract pivot stays planted on every frame, including fall lifts (the whole-sprite lift excludes the pivot row).

### COMP-EXEMPT (intentional Art islands, kept, not new comps)
- Violet Fungus NE: two 2px tendril-tip islands at (29,23)+(30,22) and (25,34)+(26,33). NW mirrors at x2–3 y22–23 and x6–7 y33–34. AD-accepted, part of the tell.
- Swarm of Beetles SE: 3px outlying beetle at (4,29)+(3,30)+(3,31); SW mirror at x28–29 y29–31. NE: 4px outlying beetle at x5–7 y30–32; NW mirror at x25–27 y30–32. (The NE/NW island sits at y30–32 on the locked idle_00, one row above the Art-note coordinates y31–33.)
A 1px palette-safe reattach closes *new* near-body tears after death flatten / fall_impact squash (Chebyshev ≤4). It does not bridge the COMP-EXEMPT islands. 15 reattach notes (violet-fungus death/fall_impact; beetles NE/NW death f03–f04 and SE/SW fall_impact f00). Moved COMP-EXEMPT islands that travel farther than 5px under a flatten or a fall lift are still the same extras (size-matched), not new comps.

### Counts, validation, gap/component check
456 frames / 108 clips tagged `"smoke": 28`: idle 48 (12), walk 72 (12), react 36, attack 72, cast 48, stun 48, death 60, fall 36, fall_impact 36 (12 each). Validation ALL PASS ×3 (exit 0) plus a full post-bake pixel pass (456/456, palette / hard alpha / empty edge cols 0 and 31 / planted pivot, 0 errors). Idle_00 lock: 12/12.

Hard check on every frame of every clip: 0 fully transparent rows inside the body bbox, 0 new 8-conn components vs idle_00 (COMP-EXEMPT islands above are allowed and were not added to).

### Truncations / lift
Edge-tight plates (shrieker cap, violet-fungus tendrils, beetle clump) step dx toward 0 or clip overflow rather than occupy col 0/31; death/fall_impact/cast width-scale is stepped down the same way. 14 walk notes (`truncations.smoke28_walk`: 6 cap-lean clips, 8 tendril-sway counts) and 172 remaining-clip notes (`truncations.smoke28`). No headroom lift caps (shrieker top y17, violet-fungus y13/15, beetles y25/27).

### md5
Guard `scripts/smoke28_md5_guard.py check-anim` vs `.smoke28_md5_baseline.json` (20001 entries, taken after the 12 idle_00 locks and before any non-idle write): 0 changed / 0 removed on the protected set (s1–s27 including all already-baked #27 frames, and the 12 locked #28 idle keys). Concurrent Art while this bake ran: 4 ADDED next-batch SE idle_00 (`swarm-of-centipedes`, `swarm-of-insects`, `swarm-of-poisonous-snakes`, `swarm-of-quippers`) — not in the protected set. Swarm-of-bats 4f already on disk at baseline and unchanged by this bake: SE v2 `7d65006cd4322a9f90e0c341ec51d080`, v1 `frames/swarm-of-bats/medium/se/_v1/mon_swarm-of-bats_medium_se_idle_00.png` `42e59f066dfafd78e762fe4b5cdc0717`, NE `66f5a2986566a0c6a8a3951e76e3e4a4`, SW `02e3599a39a6698491ebef9ad92b2474`, NW `0787aab11ea574b7b208073aec032dc9`. 0 bats anim frames written.

Locked #28 idle_00 (unchanged):
- shrieker se `2f436735a031373cf1780e17a617a67a` / sw `ee021cb7dac752d3f83a538fbb2c5b39` / ne `b4ef7d943b0e6983ee84bf19f9c7b632` / nw `07c0dde1757ec538576c3706bd254eb7`
- violet-fungus se `bbbaf47176908add8a2c91166c802ba0` / sw `2eafdd1f9e2593fd4f1a4097d63643a2` / ne `fd125eaf00ec6db096459d08434f3ba4` / nw `b7725d183f6f0e90ea62fc0147f70615`
- swarm-of-beetles se `8333c91000583f20b7098dc884b6689f` / sw `c9ab402b6bfaf173673a20a5bb5502bb` / ne `a9b10abffd104f88859b8af17c0126bb` / nw `97465fb9f18209508f30ae94c8ae3acf`

### Art soft notes carried into Anim (not compensated)
Shrieker stays the broad-capped thick-stalk fungus, no tendrils (534/532/534/532 opaque, top y17, plant y45, one body + pivot). Violet Fungus stays the mushroom with reaching tendrils; the tendrils stay attached and the AD-accepted 2px tips on NE/NW remain part of the tell (427/424/427/424 opaque, top y13 SE/SW, y15 NE/NW, plant y45). Swarm of Beetles stays the low gappy ground clump, not an airborne flock (268/268/251/251 opaque, top y27 SE/SW, y25 NE/NW, plant y44); the 3px/4px outlying beetles stay islands. SW/NW mirrors already lost a few px from the col-31→clear shift at Art 4f; Anim does not restore them.

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_{clip}_{ii}.png (slug = shrieker | violet-fungus | swarm-of-beetles)
- scripts/smoke28_common.py, scripts/build_smoke28_{idle,walk,remaining}_clips.py, scripts/build_smoke28_previews.py, scripts/smoke28_md5_guard.py
- .smoke28_idle00_locked.json · .smoke28_md5_baseline.json
- previews/mon_{slug}_medium_{clip}_cycle_4facing_x8.png · previews/mon_{slug}_medium_anim_all_clips_4facing_x8.png (4768×4832) · previews/mon_smoke28_anim_all_clips_4facing_x8.png (the three stacked, 4768×14544)
- /workspace/smoke28_{slug}_x4.png (2384×2416) · /workspace/smoke28_{slug}_walk_f00-f05_x8.png (1624×1560)


## Anim — Swarm of Bats (2026-11-10 ~9:58 PM ET) — #28 complete, awaiting AD audit

AD PASSed Swarm-of-Bats 4f Art, so its Anim was baked on a per-bat flock pipeline to finish Smoke #28 as one set of four. Local only (no repo / git / PR). The AD-passed v2 SE key (`7d65006cd4322a9f90e0c341ec51d080`) is the locked source; the v1 backup `frames/swarm-of-bats/medium/se/_v1/mon_swarm-of-bats_medium_se_idle_00.png` (`42e59f066dfafd78e762fe4b5cdc0717`) was not written. Bats was added to the 12 already-locked idle_00 keys (now 16/16). The trio's already-baked #28 frames were not rewritten.

### Grounding / per-bat motion
Floater, not a grounded clump. `split_y` = H-1 = 47/47/47/47 on every facing. Tagged `"floater": true`, `"flock": true`, `"airborne": true`, `"bridge": null`, `"vacated_fill": null`, `"stationary": true` (travel dx 0). The contract pivot at (16,47) is restored from idle_00 on every frame; no other pivot-row pixels are invented. The flock stays airborne with no ground contact (lowest bat in idle is base y40; death/fall drift it down to at most y44).

The flock flutters in place. Each of the 6 islands is moved on its own:
- Bob ±1px, phase-offset by bat index so they do not move in lockstep (`bob` cycle `[0,-1,0,1]` on idle, `[0,-1,0,1,0,-1]` on walk, adapted per clip).
- Wing-flap alternation: a modest 1px wing-row raise or lower (duplicate the top or bottom row one pixel), falling back to a 1px width change if the raise/lower would leave the bin, merge two bats, or split one.
- Travel dx is 0. Every facing already occupies col 1 and col 30, so a uniform ±1 flock dx would enter an edge column.

Bats 3 and 4 sit only a 1px (Chebyshev 2) gap apart on every facing. A closing pair of bobs (3 down and/or 4 up) or a downward flap on bat 3 would 8-connect them. The resolver drops only the offending flap or the closing bob; the rest of the flock keeps its phase. 24 soft notes (`truncations.smoke28_bats`) log those demotions (bat 3 lower skipped on idle/walk/attack/stun; bat 3 down-bob zeroed on react f02; bat 5 raise→widen on react f01).

Death drifts the flock down `[0,1,2,3,4]` (still distinct, last frame ≠ previous, inside the bin). Fall is a looping `[2,3,2]` drift with residual flap; fall_impact settles `[4,3,2]`. React / attack / cast / stun keep the same per-bat bob+flap language with clip-specific cycles (no whole-sprite squash — that would merge the flock).

### COMP-EXEMPT / component checks
The 6 idle_00 bat islands are intentional Art and are logged COMP-EXEMPT (SE 25+39+13+26+33+27 px, SW 25+39+13+25+33+27, NE 25+37+13+26+32+27, NW 25+37+13+25+32+27; top y13, base y40). Hard check on every frame: component count stays exactly at the idle_00 count (6, pivot skipped), never a merge and never a split; palette / hard alpha / empty edge cols 0 and 31; no new pivot-row pixels; the "no transparent rows in bbox" rule is applied **per bat island**, not across the gaps between bats.

### Counts, validation
#28 total is now 608 frames / 144 clips tagged `"smoke": 28` (the three already baked 456/108, plus bats 152/36: idle 16, walk 24, react 12, attack 24, cast 16, stun 16, death 20, fall 12, fall_impact 12). Bats validation ALL PASS (exit 0) plus a full post-bake pixel pass (152/152, 0 errors). Idle_00 lock: 16/16 (0 drift). v1 backup unchanged.

### md5
Guard `scripts/smoke28_md5_guard.py check-bats-anim` vs `.smoke28_md5_baseline_bats_anim.json` (20571 entries, taken after the 16 idle_00 locks and before any bats non-idle write): 0 changed / 0 added / 0 removed on the protected set (s1–s27, the trio's already-baked #28 frames, the 16 locked #28 idle keys, and the bats v1 backup). Concurrent Art: none this pass.

Locked bats idle_00 (unchanged): se `7d65006cd4322a9f90e0c341ec51d080` / sw `02e3599a39a6698491ebef9ad92b2474` / ne `66f5a2986566a0c6a8a3951e76e3e4a4` / nw `0787aab11ea574b7b208073aec032dc9`. v1 `42e59f066dfafd78e762fe4b5cdc0717`.

### Art soft notes carried into Anim (not compensated)
Swarm of Bats stays the loose airborne flock of large, simple M/V-wing bats (164/163/161/160 opaque, top y13, base y40, 6 distinct islands with a 1px minimum gap). It does not read as the beetle ground clump. Anim does not restore the 1 px SW/NW lost from the col-31→clear mirror shift at Art 4f (SE 164→SW 163, NE 161→NW 160).

### Paths
- frames/swarm-of-bats/medium/{se,sw,ne,nw}/mon_swarm-of-bats_medium_{f}_{clip}_{ii}.png (v1 SE under se/_v1/ untouched)
- scripts/smoke28_bats.py, scripts/build_smoke28_bats_clips.py (plus the existing smoke28_* bakers for the trio)
- .smoke28_idle00_locked.json (16 keys) · .smoke28_md5_baseline_bats_anim.json
- previews/mon_swarm-of-bats_medium_{clip}_cycle_4facing_x8.png · previews/mon_swarm-of-bats_medium_anim_all_clips_4facing_x8.png (4768×4832)
- previews/mon_smoke28_anim_all_clips_4facing_x8.png (all four stacked in tracker order shrieker | violet-fungus | swarm-of-bats | swarm-of-beetles, 4768×19400)
- /workspace/smoke28_swarm-of-bats_x4.png (2384×2416) · /workspace/smoke28_swarm-of-bats_walk_f00-f05_x8.png (1624×1560)

**AD sign-off (records, 2026-11-24 CT): Art+Anim PASS (soft notes as recorded above). This batch was passed by AD in chat and reported Done to Producer at the time; this line only writes that verdict into the doc. Supersedes any "awaiting AD audit" status above. Local only, no PR.**
