# Smoke #22 — Mediums SE idle_00 (fal)

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449):** This note is the production record for smoke #22. The art PR ships 26 medium creatures that have content spec rows on main, not the original 40. From this smoke, the PR includes: `blink-dog`, `dryad`, `green-hag`, `satyr`. Held back (kept local, not in the PR): `azer`, `barbed-devil`, `bearded-devil`, `chain-devil`, `erinyes`, `lemure`, `night-hag`, `rakshasa`, `doppelganger`, `mimic`, `swarm-of-beetles`, `swarm-of-centipedes`, `swarm-of-spiders`, `swarm-of-wasps`.

**Status: SE soft-PASS (AD) · 4-facing soft-PASS (AD) · Art Done · Anim built, awaiting AD audit**

Roster: Blink Dog | Dryad | Green Hag | Satyr — all **medium** 32×48, pivot (16,47) opaque.

## Pipeline / contract
`google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium` (medium LAST). post: 4/4 PASS.

## IDs
| slug | gen request_id | birefnet request_id | opaque | post |
|---|---|---|---|---|
| blink-dog | 01a1c82a-bbd7-7e73-a260-3ff7bb31cc6c | 01a1c82b-b1db-7c33-8339-45d8d42f1a0f | 303 | PASS |
| dryad | 01a1c82a-bc62-77a2-bbbd-b67b557d2063 | 01a1c82b-b214-78e1-90a9-a0ee92cf34f2 | 438 | PASS |
| green-hag | 01a1c82a-bc1a-7152-9c30-07a457f13a41 | 01a1c82b-b1f6-7951-9f24-6a7f75ec40cc | 560 → 558 after speck clean | PASS |
| satyr | 01a1c82a-bc65-7d60-a76c-e42092adbf91 | 01a1c82b-b21d-7502-b2ff-fd31da86e502 | 420 | PASS |

All first take; no rejects / no v1 files.

## Validation (4/4 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty.
Comps (8-conn), after the Green Hag SE speck clean: blink-dog 302+1 pivot · dryad 438 (body connects through row 46 into the pivot) · green-hag 557+1 pivot · satyr 419+1 pivot.
Feet: blink-dog and green-hag plant on y45 with a row-46 gap (same floater as azer / wolf / most prior mediums). Dryad and satyr plant on y46 and fill the bin (top at y1) — same as prior full-height mediums (deva, animated-armor, deer). Not a post-script miss; tall standing figures hit `fal_post_to_bin`'s y=1 clamp.
No cast shadow or plinth in any raw. Smoke 1–21 frames md5: 0 modified/removed (12316 before; only 4 smoke22 SE additions, then the hag SE clean on this 4f pass).

## Soft notes (SE)
- Blink Dog: SE ✓ (head lower-right). Lean wolf-like fey dog on four legs, large pointed ears, raised tail. Clearly reads as a dog at 32px — the only quadruped in the batch, shortest sprite (top at y20). Khaki on legs / muzzle / tail tip (20.2%). No invented bits.
- Dryad: SE-ish (upright, slight right turn). Slender woodland fey woman, bark-like skin, bulky leafy hair. At 32px the face is lost; the tell is the tall narrow stance + dark leafy hair mass + khaki body accents (9.6%). Distinct from the hag (upright vs hunched) and the satyr (no horns, no goat legs).
- Green Hag: SE ✓ (hunched, leaning lower-right). Gaunt crone, long tangled hair, ragged shawl, long clawed fingers that still read as a reaching right-hand at 32px — strongest silhouette tell of the three bipeds.
- Satyr: SE ✓ (body angled right). Goat legs with hooves, bare human torso, panpipes at the chest. Horns are fairly large ram-curls rather than "small curled" — that actually helps at 32px (the horn arc is the head tell). Panpipes collapse to a small dark chest block and may not read as pipes. Goat-leg / hoof lower body is clearly darker than the torso (khaki only 3.1%).
- Distinctness at 32px: all four separable (quadruped dog / tall leafy woman / hunched clawed crone / horned goat-legged male). No shadow or plinth crept into any raw.

## Paths
- frames/{slug}/medium/se/mon_{slug}_medium_se_idle_00.png
- fal_raw/{slug}_se_idle_00_{raw,cutout}.png
- previews/mon_smoke22_se_idle_00_fal_x8.png (order blink-dog|dryad|green-hag|satyr; #C2B08E; 34px cells; ×8 nearest)

## 2026-11-10 ~8:21 PM ET — AD SE soft-PASS + Green Hag speck clean
- AD soft-PASS on SE (Blink Dog, Dryad, Green Hag, Satyr). Softs accepted: Dryad's face is lost but leafy hair carries her; Satyr's pipes block is fine.
- Clean (no fal call): Green Hag SE 1px fringe specks at (10,14) and (27,35) set to alpha 0. Both were confirmed isolated first (opaque #1E2228, no opaque 8-neighbours). No other pixel changed. Baseline stayed y45; pivot (16,47) unchanged. Opaque 560→558. Comps 557+1 pivot (the two 1px fringe components are gone).
- Current baselines kept: y45 for Dog and Hag, y46 for Dryad and Satyr.

## 4-facing (idle_00) — 2026-11-10 ~8:21 PM ET
SE = existing AD soft-PASS after the hag speck clean above. NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with the existing SE raw as likeness reference (`image_urls` = uploaded `fal_raw/{slug}_se_idle_00_raw.png`) so NE keeps the SE design. Then `fal-ai/birefnet/v2` (CDN gen `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE, NW←NE via the smoke21 `mirror()` (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). Mirrors ran after the hag speck clean, so SW is from the cleaned SE. Script: `scripts/build_smoke22_idle_4facing.py` (smoke21 clone). Config: `scripts/smoke22_common.py`. All NE first take; no rejects / no v1 files.

NE baselines already matched their SE after post (blink-dog and green-hag y45, dryad and satyr y46). No vertical shift was required. Dryad and satyr NE already fill the bin (top y1, baseline y46) but did not need a shift, so no clip case.

| slug | NE gen request_id | NE birefnet request_id | post |
|---|---|---|---|
| blink-dog | 01a1c82f-ba68-7de2-be9c-5bb72ed67f76 | 01a1c830-9037-7bf1-9e2e-c1d32cf3c693 | PASS |
| dryad | 01a1c82f-ba0c-7d90-b0e1-4010a144902e | 01a1c830-9090-72a3-8134-6206aa5bc91e | PASS |
| green-hag | 01a1c82f-ba56-7773-8a32-269a7fc23d10 | 01a1c830-9088-7c82-b1db-0f0ea843f332 | PASS |
| satyr | 01a1c82f-ba60-7881-a955-49c4080bb3d4 | 01a1c830-9093-7b73-a927-b902baec9188 | PASS |

### Validation 16/16 PASS (size · pivot α255 · hard alpha · palette · empty edge cols · non-empty)
| slug | se opaque / base | sw | ne | nw | 8-conn comps (se/sw/ne/nw) | khaki% se→ne |
|---|---|---|---|---|---|---|
| blink-dog | 303 / y45 | 299 / y45 | 308 / y45 | 304 / y45 | 302+1 / 298+1 / 307+1 / 303+1 | 20.2→15.6 |
| dryad | 438 / y46 | 438 / y46 | 469 / y46 | 469 / y46 | 438 / 438 / 468+1 / 468+1 | 9.6→5.6 |
| green-hag | 558 / y45 | 557 / y45 | 443 / y45 | 443 / y45 | 557+1 / 556+1 / 441+1+1 / 441+1+1 | 12.6→12.9 |
| satyr | 420 / y46 | 420 / y46 | 444 / y46 | 444 / y46 | 419+1 / 419+1 / 443+1 / 443+1 | 3.1→0.0 |

The +1 in blink-dog / green-hag / satyr (all facings) and in dryad NE/NW is the detached pivot pixel (16,47) (contract). Dryad SE/SW body still connects through row 46 into the pivot (one component). Blink-dog and green-hag keep the row-46 gap; dryad and satyr plant on y46 and fill the bin (top at y1) on all four facings. Green Hag NE/NW have one extra isolated 1px outline speck (#1E2228) at (28,27) NE / (4,27) NW — no opaque 8-neighbours, 2px below the reaching arm, same downsample-fringe class as the SE specks just cleaned. Left as-gen for AD. No cast shadow or plinth in any NE raw.

md5 guard (`scripts/smoke22_md5_guard.py check` vs `.smoke22_md5_pre_4f.json`, 12468 frames before this pass): 1 changed (green-hag SE idle_00 speck clean) + 12 added (smoke22 ne/sw/nw idle_00) + 0 removed. Smoke 1–21 frames: 0 modified/removed. clips.json untouched.

### Soft notes (4f)
- Blink Dog NE: still a right-facing side profile, not a true back-three-quarter away up-right. Head is a bit more raised than SE (top y20→y17) but the chest/side is still fully visible; pose is nearly the same walk-stand as SE. Likeness holds: lean fey hound, large pointed ears, raised tail, khaki legs / muzzle / tail tip. Mild khaki drop 20.2→15.6%. Opaque almost unchanged (303→308) — no bulk change. Biggest facing-consistency risk of the four, same class as gargoyle NE in #21.
- Dryad NE: best turn of the batch. True back-three-quarter / rear view, head in profile toward the upper-right, back and leafy hair mass toward the viewer, face gone (already weak on SE). Same tall narrow stance + bulky leafy hair tell. Slightly bulkier (438→469) from the hair/back mass; khaki 9.6→5.6 (back view loses some body accents). Fills the bin (y1–y46), same as SE. Not a redesign.
- Green Hag NE: still a right-facing hunched profile with the face clearly visible and the claw reaching right — a modest camera shift, not a back-three-quarter away from the viewer. Likeness of hair / shawl / claws holds, but the body is noticeably thinner: opaque 558→443 (−115 px, ~21% less), top y12→y16. The long trailing rag-mass on the SE left side is reduced, so at 32px she reads more compact. Khaki stable 12.6→12.9%. One leftover 1px fringe speck at (28,27) (see above). Biggest bulk-change risk of the batch.
- Satyr NE: strong turn. Back-three-quarter, we see the back and ram-curl horns from behind, head in profile facing right, goat legs and hooves retained. Panpipes, already a small dark chest block on SE, nearly vanish from behind (khaki 3.1→0.0; the back is all grey). Slightly bulkier 420→444, not a redesign. Horns remain the 32px head tell.
- SW/NW mirrors lose 0–4 px from the col-31→clear shift (blink-dog loses 4; hag SE→SW loses 1; dryad and satyr lose none).

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_idle_00.png
- fal_raw/{slug}_ne_idle_00_{raw,cutout}.png
- previews/mon_{slug}_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw) · previews/mon_smoke22_4facing_idle_00_fal_x8.png (rows blink-dog|dryad|green-hag|satyr, 1036×1560)

## 2026-11-10 — Green Hag NE speck clean
- Removed the isolated 1px #1E2228 fringe speck at (28,27) in NE (no opaque 8-neighbours). NW re-mirrored, so (4,27) is gone too. No other pixel changed.

## Anim (2026-11-10) — built, awaiting AD audit
Medium 32×48, pivot (16,47); source = Art 4f soft-PASS fal idle_00 after the Green Hag SE and NE speck cleans (never written). Template = modest smoke #18–#21 (same bob/sway/scale amplitudes, no species flourishes — no blink, no leaf-sway, no claw rake, no pipe jig). Blink Dog walk is the quadruped mastiff/hyena pattern: legs planted below the detected split, torso bob/sway, 1px airborne on walk f02 (`lift=[0,1,2,1,0,0]`); tagged `"quadruped": true` in clips.json. The three bipeds use the same modest walk amplitudes.
Regenerate (from repo root): `python3 scripts/build_smoke22_idle_clips.py && python3 scripts/build_smoke22_walk_clips.py && python3 scripts/build_smoke22_remaining_clips.py && python3 scripts/smoke22_md5_guard.py check` (config: `scripts/smoke22_common.py`; locked idle_00 in `.smoke22_idle00_locked.json`; baseline taken first with `smoke22_md5_guard.py baseline`). `smoke22_md5_guard.py check-pre` still compares frames to the Art pre-4f snapshot `.smoke22_md5_pre_4f.json`.

split_y se/sw/ne/nw: blink-dog 34/34/34/34 · dryad 34/34/36/36 · green-hag 43/43/44/44 · satyr 41/41/35/35 (all grounded; per-facing detect, never floater split=H-1=47). Baselines respected: blink-dog and green-hag plant on y45 (contract row-46 pivot gap); dryad and satyr plant on y46 and fill the bin (top at y1).
Floaters: none. Bridge: all `full_row` (no BODY_BRIDGE). Subtle: none (NE walk distinct frames: blink-dog 4, dryad 5, green-hag 5, satyr 5; no hop fallback).

Counts: 608 frames / 144 clips tagged `"smoke": 22`: idle 64 (16), walk 96 (16), react 48, attack 96, cast 64, stun 64, death 80, fall 48, fall_impact 48 (16 each). Validation ALL PASS ×3 (exit 0).
Truncations: `truncations.smoke22` 175 notes (death 64, attack 25, fall 34, cast 15, fall_impact 14, react 13, stun 10; lift truncations 24, all dryad and satyr fall — f00/f02 lift 3→1 and f01 lift 4→0 on every facing, top-row headroom from filling the bin; no edge-col fallback), `truncations.smoke22_walk` 6 (walk sway dx -1→0 on f02/f03: blink-dog se+ne, green-hag se). Dryad and satyr kept the 1px walk sway on all facings. Blink-dog and green-hag had no fall lift truncations.
md5 guard: 15476 entries (smoke 1–21 frames including gargoyle #21 outputs + 16 locked smoke22 Art idle_00 + clips.json non-smoke22 entries/truncations); drift 0. Locked #22 idle_00 also verified against `.smoke22_idle00_locked.json`.
Contacts: 36 `previews/mon_{slug}_medium_{clip}_cycle_4facing_x8.png` (slug ∈ blink-dog, dryad, green-hag, satyr; clip ∈ idle, walk, react, attack, cast, stun, death, fall, fall_impact) plus per-creature audit sheets `previews/mon_{slug}_medium_anim_all_clips_4facing_x8.png` and combined `previews/mon_smoke22_anim_all_clips_4facing_x8.png`. x4 downscales of the per-creature sheets: `/workspace/smoke22_{slug}_x4.png`.

Art soft notes carried into Anim (not compensated): Blink Dog NE stays a right-facing side profile (same class as gargoyle NE in #21), not a true back-three-quarter. Dryad face is lost; leafy hair and tall narrow stance carry her; NE is the best turn (rear/back-three-quarter). Green Hag NE is still a right-facing hunched profile and is noticeably thinner than SE (opaque 558→442 after the NE speck clean); the leftover SE/NE fringe-speck issue is already cleaned. Satyr panpipes stay a small dark chest block (vanish from behind; khaki 3.1→0.0); ram-curl horns remain the 32px head tell. No compensating motion invented.
Caveats: generic torso motion only. Feet stay planted (torso bobs above detected split). Blink-dog / green-hag plates are tight on the edge cols, so attacks mostly truncate toward dx 0 (weak lunges) and walk sway is one-sided on the listed facings. Dryad and satyr are narrower, so death sw is not capped (only dx goes to 0) but fall apex lift goes to 0 because they fill the bin (top y1). Green Hag's high split (43–44) means only a short foot stump animates under a mostly static lower skirt/body. Fall_impact sw 1.2/1.08→1.02 on the tight plates.

**AD sign-off (records, 2026-11-24 CT): Art+Anim PASS (soft notes as recorded above). This batch was passed by AD in chat and reported Done to Producer at the time; this line only writes that verdict into the doc. Supersedes any "awaiting AD audit" status above.**

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449):** This note is the production record for smoke #22. The art PR ships 26 medium creatures that have content spec rows on main, not the original 40. From this smoke, the PR includes: `blink-dog`, `dryad`, `green-hag`, `satyr`. Held back (kept local, not in the PR): `azer`, `barbed-devil`, `bearded-devil`, `chain-devil`, `erinyes`, `lemure`, `night-hag`, `rakshasa`, `doppelganger`, `mimic`, `swarm-of-beetles`, `swarm-of-centipedes`, `swarm-of-spiders`, `swarm-of-wasps`.
