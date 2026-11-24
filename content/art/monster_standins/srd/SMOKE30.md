# Smoke #30 — Mediums 4f idle_00 (fal) + spiders SE v2 trimmed / 4f

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449):** This note is the production record for smoke #30. The art PR ships 26 medium creatures that have content spec rows on main, not the original 40. From this smoke, the PR includes: `swarm-of-rats`, `swarm-of-ravens`. Held back (kept local, not in the PR): `azer`, `barbed-devil`, `bearded-devil`, `chain-devil`, `erinyes`, `lemure`, `night-hag`, `rakshasa`, `doppelganger`, `mimic`, `swarm-of-beetles`, `swarm-of-centipedes`, `swarm-of-spiders`, `swarm-of-wasps`.

**Status: Art Done (all four); Anim built for all four (trio AD PASS; swarm-of-spiders Anim awaiting AD audit)**

AD 2026-11-10: spiders 4f soft-PASS. Four spiders is a documented exception to the 5–9 swarm rule, because they read as spiders, they are distinct from the beetles, and v3 proved more spiders don't survive this 32px bin. The two 5–6px fragment islands were dropped from the delivered v2; the remaining four (widths 8–10) stand as the Art Done idle_00.

Roster: Swarm of Rats | Swarm of Ravens | Swarm of Spiders | Swarm of Wasps — all **medium** 32×48, pivot (16,47) opaque.

Source: Non-humanoid SRD monster art tracker, D list (canvas `40c1bf63-1238-4b58-b7b5-7fc0075af844`). AD verdict 2026-11-10: #30 4f for rats, ravens and wasps PASS (those three are Art Done). Spiders v2 was a real step up, but the two small 5–6px islands read as broken fragments. One more fal generation (v3) was authorized, with no further regens after it.

## Pipeline / contract
SE: `google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`.

NE: `google/nano-banana-2.1/edit` with the SE raw as likeness reference (`image_urls` = uploaded `fal_raw/{slug}_se_idle_00_raw.png`), three-quarter back view facing up-left, then the same birefnet → post path. SW = mirror(SE), NW = mirror(NE) via existing `mirror()`.

Swarm rule: 5–9 DISTINCT readable creatures, islands at least 1px apart (rats ≥2px), no 1px specks. AD allowed dropping or merging the two undersized spider fragments if the frame stayed contract-safe and at least 4 spiders remained. A whole-flock vertical nudge is allowed. Islands too small to be a creature may be deleted; nothing is hand-drawn. Speck clean is only truly isolated 1px fringe; connected 1px spider legs are kept. Palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282}. Hard alpha 0/255. Edge columns empty. Pivot (16,47). This pass was not pushed at the time. Animator clips left out of the fail set.

## Art Done — rats, ravens, wasps 4f (unchanged this pass)

All 12 idle_00 contract PASS; SE hashes unchanged vs the pre-v3 baseline. See the earlier 4f table in this file's prior revision for the per-facing opaque / top / base / khaki / islands / widths / min gap / request_ids. Summary: rats 6 islands, gap 2–3px, y45; ravens 8 islands, bird-winged and dark, gap 1px, y40, widths not past SE max 10; wasps 9 islands, stripes and narrow waists kept, widths stayed 7–8, gap 1px, y40, subtle NE turn. 0 gen regens on those NE takes.

## Spiders v3 (not installed)

v2 was backed up first, before any overwrite:
- `frames/swarm-of-spiders/medium/se/_v2/mon_swarm-of-spiders_medium_se_idle_00.png` (delivered untrimmed 6-spider v2)
- `fal_raw/swarm-of-spiders_se_idle_00_{raw,cutout}_v2.png` (canonical delivered v2; the earlier rejected v2 take 1 was moved aside as `…_v2_take1.png`)

v3 target: 5 larger spiders, each 9–11px span, small body, clear splayed legs, gaps ≥2px, dark/blue-grey, grounded at y45. One generation only, no further regens. Key suffix `c8e91f`.

- gen `01a1c89c-18dc-7c60-aaee-d7d3551543b3` key `smoke30-se-swarm-of-spiders-idle00-v3-c8e91f`
- birefnet `01a1c89d-6954-7b60-9626-3ff4bfe06fd0` key `…-v3-birefnet-c8e91f`
- raw/cutout kept as `fal_raw/swarm-of-spiders_se_idle_00_{raw,cutout}_v3.png` (canonical SE raw was not overwritten)

At 1024, v3 is six (not five) large, clearly star-legged dark/blue-grey spiders with abdomen dots, 0 near-khaki. Two neighboring pairs already share dark pixels, so the raw has 4 connected components. Estimated dest spans of those merged groups are ~14–18px.

At 32px after post (before any install): contract size/pivot/alpha/palette/edges PASS, khaki 0, top y17, base y45, **29 islands** (7 isolated 1px specks plus many broken leg stubs). Visually ~6 body clusters; after keeping n≥13: 7 islands, widths 12, 9, 5, 4, 9, 6, 10. Spans are not consistently 9–11 (one over at 12, several undersized fragments). Min gap 1px. Legs shattered at the bin; mid spiders merge/break.

**Branch taken: v3 does NOT beat v2.** More fragments, not fewer; spans not closer to 9–11 as a set; gaps still 1px (not closer to 2px); several islands read as broken pieces rather than complete spiders. v2's four larger spiders already read as spiders. v3 was not installed. No further regens.

## Kept v2 as SE; dropped the two fragment spiders; then 4f

AD allowed deleting the two undersized 5–6px islands if the frame stayed contract-safe and at least 4 spiders remained. Both 5–6px islands were deleted (w=6 n=13 at (24,31)–(29,33) and w=5 n=12 at (24,42)–(28,45)). 4 spiders remained. 2px whole-flock downward nudge (base y43→y45). 0 leftover specks. Connected 1px legs kept. Nothing hand-drawn.

### Delivered spiders 4f (trimmed v2)

| facing | opaque | top | base | khaki | islands | widths (px) | min gap | request_ids (gen / birefnet) | notes |
|---|---|---|---|---|---|---|---|---|---|
| SE | 86 | y23 | y45 | 0 (0%) | 4 | 8, 8, 9, 10 | 1px | (v2 regen 2) 01a1c896-d555-7093-ab05-fd464c82da52 / 01a1c898-63e0-7f30-9a10-6737e0f5f1ac | Trimmed v2. 69.4% #1E2228, rest #5C6370. The four remaining spiders are the ones that already read as spiders (spans 8×5, 10×6, 8×5, 9×7). |
| SW | 86 | y23 | y45 | 0 (0%) | 4 | 8, 8, 9, 10 | 1px | (mirror of SE) | — |
| NE | 121 | y23 | y45 | 0 (0%) | 4 | 8, 8, 9, 10 | 1px | 01a1c89e-aba1-7491-b782-24bc71de5c91 / 01a1c89f-e417-78e1-9eec-cb4249a6e972 | First take (0 retries; retry allowed only for no_media, which did not happen). 75.8% #1E2228. Post: 10 isolated 1px tips removed (connected legs kept), 6 px of <8 stubs deleted, the two remaining 5–6px islands dropped to match SE (w=6 n=19 and w=5 n=18), then 6px downward nudge (y39→y45). Widths 8, 8, 9, 10; heights 7, 6, 8, 7 (fuller than SE). |
| NW | 121 | y23 | y45 | 0 (0%) | 4 | 8, 8, 9, 10 | 1px | (mirror of NE) | — |

NE keys: `smoke30-ne-swarm-of-spiders-idle00-c8e91f` and `…-birefnet-c8e91f`. Likeness reference was the delivered v2 SE raw (the 6-spider source, still at `fal_raw/swarm-of-spiders_se_idle_00_raw.png`), uploaded and verified (824272 bytes) at `https://v3b.fal.media/files/b/0ab1fcae/79FTMIM4jJGbdl-xHG6Bx_swarm-of-spiders_se_idle_00_raw.png`.

NE notes: at 1024 the edit is six well-separated, clearly star-legged dark spiders (dest spans ~11.0×8.7–8.8, some neighbor dest gaps ~0.67–0.85px). At 32px the pack lands in the same layout as SE, with fuller bodies (121 vs 86 opaque). After baseline align: 83 positions in common, 2 SE-only, 37 NE-only, 74 same RGBA — a subtle / same-composition turn with more fill, in the same class AD accepted for #29 swarms and for wasps NE. Not a dramatic three-quarter-back facing change. The two right-side 5–6px islands were dropped for consistency with SE, leaving 4 contract-safe spiders.

All four facings: 32×48, pivot (16,47) α255, alpha {0,255}, on-palette, cols 0/31 empty, 0 leftover specks.

## Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{facing}_idle_00.png (all four slugs, all four facings)
- frames/swarm-of-spiders/medium/se/_v1/… (v1) and `…/se/_v2/…` (untrimmed delivered v2)
- fal_raw/swarm-of-spiders_se_idle_00_{raw,cutout}.png (delivered v2 source, untrimmed)
- fal_raw/swarm-of-spiders_se_idle_00_{raw,cutout}_v3.png (v3, not installed)
- fal_raw/swarm-of-spiders_ne_idle_00_{raw,cutout}.png
- previews/mon_smoke30_se_idle_00_fal_x8.png (rebuilt with trimmed v2 in the spiders cell; 1088×384; rats | ravens | spiders | wasps)
- previews/mon_smoke30_spiders_v1_v2_vs_beetles_x8.png (unchanged this pass: historical untrimmed v2)
- previews/mon_{rats,ravens,wasps,spiders}_medium_idle_00_4facing_fal_x8.png
- previews/mon_smoke30_4facing_idle_00_fal_x8.png (1036×1560; rows rats | ravens | wasps | spiders)

## md5 guard
`scripts/smoke30_md5_guard.py check-spiders-4f` vs `.smoke30_md5_pre_v3.json` (17332+9+0 = **17341** frames, taken after the spiders v2 backup and before v3 / the fragment drop / spiders ne/sw/nw): **PASS**. pre 17341 / cur 17788. 1 changed (`frames/swarm-of-spiders/medium/se/…_idle_00.png`, the fragment drop) + 3 added (spiders ne/sw/nw idle_00) + 0 removed. Concurrent Animator clips for the Art Done trio (rats, ravens, wasps non-idle_00): 444, excluded from the fail set (listed as CONCURRENT-ADD-S30-ANIM). Trio idle_00 hashes unchanged. Smoke 1–29 idle art: 0 modified/removed by this pass. This pass was not pushed at the time.

The Animator's `baseline` / `check-anim` commands in the same guard script were left in place and were not overwritten. `check-4f` vs the older pre-4f baseline is unchanged and is not the check for this pass.


## Anim — swarm-of-spiders (and #30 close-out)

AD queued spiders Anim after the #30 trio PASS. This pass bakes the last #30 slug from the delivered 4f keys and does not rewrite the trio frames.

### Key used

Art's v3 take was **not installed** (v3 did not beat v2). The passed 4f is the **trimmed v2**: the two 5–6px fragment islands were dropped from the delivered v2 SE, a 2px whole-flock downward nudge put the base at y45, then NE/SW/NW were generated / mirrored. Four spiders is an accepted exception to the 5–9 swarm rule.

Canonical idle_00 (what this bake locked and used; no Art-side backups were written):

| facing | md5 | opaque | islands (px) | top–base | min gap |
|---|---|---|---|---|---|
| SE | `91c38a24d0ee1667412b9bd687fdd676` | 86 | 4 (18+24+20+23) | y23–y45 | 1px (cheb 2–3) |
| SW | `781d0170a24aa1492267184800b9512c` | 86 | 4 (18+24+20+23, SE mirror) | y23–y45 | 1px |
| NE | `01caafd4942caead56ac7dfba27b4345` | 121 | 4 (26+32+31+31) | y23–y45 | 1px (cheb 2) |
| NW | `a55b29fbb6bf25adb287752d30318adc` | 121 | 4 (26+32+31+31, NE mirror) | y23–y45 | 1px |

Left on disk, untouched: `se/_v1/…` = `ef394854746078e542456d7b42881974` (rejected v1) and `se/_v2/…` = `95215408261a73d90934c69bb338a685` (untrimmed 6-island delivered v2).

### Motion

Ground swarm, baseline y45, `split_y=45`, not a floater, `vacated_fill` null. Each of the four islands gets a phase-offset ±1 bob plus a small 1px leg-twitch (retract a 1–2-neighbour tip when that is legal; otherwise a 1px widen of a peripheral row; a 1px raise is the last fallback). The resolver demotes twitch / widen / dx / dy that would merge, split, enter cols 0 or 31, or place a body pixel on y47.

Crawl is rat-like: uniform flock `dx = -1` on walk and attack f02/f03 only. All four facings had room (SE/NE left=4, right room=11; SW/NW left=12, right room=3). Individual island dx is dropped if it would close a 1px gap.

Death/fall settle is capped at +1 for islands on the ground line (`death_dy=[0,0,1,1,1]`, `fall_dy=[0,1,0]`, `impact_dy=[1,1,0]`, plus a mild compress that the resolver often demotes). `in_bin` forbids y47; every baked frame has max body y=46 and y47 empty except the contract pivot (16,47).

### Island checks (COMP-EXEMPT)

The four idle_00 islands are intentional Art, logged as COMP-EXEMPT. Every one of the 152 spider frames keeps exactly 4 islands — no merges, no splits. The no-empty-rows rule is applied per island, not across the gaps. Palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282}, hard alpha {0,255}, cols 0 and 31 empty. Hard-check rescan: 152/152 PASS.

### Counts

Spiders: **36 clips / 152 frames**. #30 total now **144 clips / 608 frames** (36/152 × four slugs). `clips.json` `"smoke": 30`, `scope: per_island`, `swarm: true`. Resolver notes in `truncations.smoke30`: 144 spider lines appended; trio notes kept (rats 58, ravens 138, wasps 120; 460 total). These are demotions, not failed frames.

### Trio recap (notes were displaced when Art rewrote this file for the spiders 4f PASS)

swarm-of-rats (ground, y45): 6 islands, crawl dx −1 (SE/NE/SW) / 0 (NW, right-tight), death settle +1 to +2 with y46 cap. swarm-of-ravens and swarm-of-wasps (floaters, base y40, split=H−1): 8 and 9 islands, dx 0, death/fall drift capped so the lowest island stays at or above y44. AD PASSed that trio Anim. Those 108 clips / 456 frames were not rewritten this pass.

### md5 guard

`scripts/smoke30_md5_guard.py check-spiders-anim` vs `.smoke30_md5_baseline_spiders_anim.json` (taken after locking the 4 spider idle_00, before this bake): **PASS**. 22065 entries, **0 changed / 0 added / 0 removed**. Protected set: Smoke 1–29, the #30 trio (idle and non-idle), the 16 locked idle_00 (12 trio + 4 spiders), and the Art-side `se/_v1` and `se/_v2` backups. No concurrent Art this pass. The earlier trio `baseline` / `check-anim` commands are unchanged.

Locked spider idle_00 (also in `.smoke30_idle00_locked.json`):

- SE `91c38a24d0ee1667412b9bd687fdd676`
- SW `781d0170a24aa1492267184800b9512c`
- NE `01caafd4942caead56ac7dfba27b4345`
- NW `a55b29fbb6bf25adb287752d30318adc`


### Anim paths

- `frames/swarm-of-spiders/medium/{se,sw,ne,nw}/mon_swarm-of-spiders_medium_{facing}_{clip}_{frame}.png` (plus the locked idle_00)
- `previews/mon_swarm-of-spiders_medium_anim_all_clips_4facing_x8.png` (4768×4832)
- `/workspace/smoke30_swarm-of-spiders_x4.png` (2384×2416)
- `/workspace/smoke30_swarm-of-spiders_walk_f00-f05_x8.png` (1624×1560)
- `previews/mon_smoke30_anim_all_clips_4facing_x8.png` (4768×19400; rows rats | ravens | spiders | wasps, tracker order)

**AD sign-off (records, 2026-11-24 CT): Art+Anim PASS (soft notes as recorded above). This batch was passed by AD in chat and reported Done to Producer at the time; this line only writes that verdict into the doc. Supersedes any "awaiting AD audit" status above.**

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449):** This note is the production record for smoke #30. The art PR ships 26 medium creatures that have content spec rows on main, not the original 40. From this smoke, the PR includes: `swarm-of-rats`, `swarm-of-ravens`. Held back (kept local, not in the PR): `azer`, `barbed-devil`, `bearded-devil`, `chain-devil`, `erinyes`, `lemure`, `night-hag`, `rakshasa`, `doppelganger`, `mimic`, `swarm-of-beetles`, `swarm-of-centipedes`, `swarm-of-spiders`, `swarm-of-wasps`.
