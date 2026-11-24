# Smoke #25 — Mediums SE idle_00 (fal)

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449):** This note is the production record for smoke #25. The art PR ships 26 medium creatures that have content spec rows on main, not the original 40. From this smoke, the PR includes: `succubus-incubus`, `basilisk`, `death-dog`. Held back (kept local, not in the PR): `azer`, `barbed-devil`, `bearded-devil`, `chain-devil`, `erinyes`, `lemure`, `night-hag`, `rakshasa`, `doppelganger`, `mimic`, `swarm-of-beetles`, `swarm-of-centipedes`, `swarm-of-spiders`, `swarm-of-wasps`.

**Status: SE soft-PASS (AD) · Rakshasa SE REPASS · 4-facing soft-PASS (AD) · Art Done · Anim built, awaiting AD audit**

Roster: Rakshasa | Succubus/Incubus | Basilisk | Death Dog — all **medium** 32×48, pivot (16,47) opaque.

## Pipeline / contract
`google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium` (medium LAST). post: 4/4 PASS.

## IDs
| slug | gen request_id | birefnet request_id | opaque | post |
|---|---|---|---|---|
| rakshasa | 01a1c846-5a62-7f40-a7cc-d8bc6521452a | 01a1c847-43e6-70f2-ad5d-e8fd23f3a35e | 685 | PASS |
| succubus-incubus | 01a1c846-5a9f-7e10-b19b-d106ac4cbd8c | 01a1c847-42e9-7fc3-ba9a-ad059afe0b92 | 669 → 668 after speck clean | PASS |
| basilisk | 01a1c846-5a61-7c41-9b4a-d56f1dfb1f4c | 01a1c847-4337-7031-b829-8c61b8da337f | 304 | PASS |
| death-dog | 01a1c846-5a9d-7972-8fdf-27b17a5343a8 | 01a1c847-43d7-7b43-af45-1187df70af56 | 317 | PASS |

All first take; no rejects / no v1 files. No generation missed its tell, so none were regenerated.

## Validation (4/4 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty.
Comps (8-conn), after speck clean: rakshasa 684+1 pivot · succubus-incubus 663+2+2+1 pivot · basilisk 303+1 pivot · death-dog 316+1 pivot.
Feet: succubus, basilisk, and death dog plant on y45 with a row-46 gap (same floater as azer / wolf / the #23–#24 batches / most prior mediums). Rakshasa plants on y46 (top y1) because the tall upright robe pose is height-limited by `fal_post_to_bin`'s fit (nh=46, pasted at y=1, bottom y46) — same class as the #22 dryad and satyr (y46). Not a post-script miss; the figure fills the bin.
Basilisk is short in the bin (top at y26, 304 opaque) because the wide low body is width-limited by the same fit — same class as the #24 erinyes (top y17) and the #23 bearded devil (top y20). The whole creature is inside the bin, not cropped.
No cast shadow or plinth in any raw. No wings crept into the non-winged three. Smoke 1–24 frames md5: 0 modified/removed. Guard: `.smoke25_md5_pre_se.json` (13696, post-#24 4f idle, pre-#25) → 0 changed / 0 removed / 4 smoke25 SE additions. 592 concurrent smoke24 anim clips also appeared vs that snapshot (Animator bake during this pass, not written here); clips.json was not touched by this pass.

## Speck clean (no fal call)
Isolated 1px fringe speck with no opaque 8-neighbours, #1E2228, set to alpha 0. Confirmed isolated first. No other pixel changed. Baseline stayed y45; pivot (16,47) unchanged.
- succubus-incubus SE (10,28) — opaque 669→668. The 1px fringe component is gone; leftover comps are 663+2+2+1 pivot.
Rakshasa, basilisk, and death dog had none.

Leftover non-1px components left as-gen for AD:
- Succubus/Incubus: 2px #1E2228 island at (8–9, 26) and 2px #1E2228 island at (8,31)+(9,30) — detached left-wing / tail-adjacent fragments that lost their 1px bridge at 32px. Connected in the raw. Not isolated 1px fringe. Left as-gen.

## Soft notes (SE)
- Rakshasa: SE ✓ (head toward the lower-right). Upright tiger-headed fiend in fine layered robes with a khaki sash and hem trim (4.5% khaki), clawed hands in the raw. The raw is unmistakably a tiger head (stripes, ears, muzzle) — that is the tell, and it is present. At 32px the stripes and ears are lost (same class as other lost faces); the head reads as a dark animal-snouted lump on a tall robed body, which still separates it from the winged succubus. The backward-bending palms do not survive at 32px. Tallest of the four (685 opaque, top y1, feet on y46). No wings. No shadow or plinth.
- Succubus/Incubus: SE ✓ (standing, head slightly right). One sprite in true fiend form: large bat wings fully inside the bin (the strongest 32px tell — a wide V of membrane), horned hooded head (two horn peaks readable at the crown), thin hooked tail on the left, fully dressed in modest long robes with a khaki front panel (7.9%). Neutral standing pose, clearly non-sexualized — no revealing outfit, no body-shape emphasis, just a robed winged fiend. Wings, horns, and tail all read at 32px. The two 2px islands noted above are left as-gen. Isolated speck at (10,28) cleaned. Heavy in the bin (668 opaque, top y2) despite the wings. No shadow or plinth.
- Basilisk: SE ✓ (head lower-right, tail curled up on the left). Heavy low long-bodied reptile; the raw shows a thick lizard head and a row of many legs (well more than four, eight-legged intent). At 32px the individual eight legs are not countable, but the low long body plus a row of leg lumps underneath is the tell — it does not read as a four-legged dog or a snake. Shortest of the four (304 opaque, top y26) because the wide pose is width-limited; still fully inside the bin. Khaki mid-body / leg block (12.2%). No wings. No shadow or plinth.
- Death Dog: SE ✓ (both heads toward the lower-right). Gaunt mangy four-legged dog with TWO heads on two necks — both heads read at 32px (upper snouted head on the right, lower hanging head beneath it), and that is the tell. Almost no khaki (0.9%). Distinct from the #22 blink dog (single head, warmer 20.2% khaki on muzzle / legs / tail tip, pointed-up ears, raised tail, 303 opaque, top y20) and the #24 hell hound (single head, bulkier crouch, pale snout-smoke puff and back-flame ridge, 6.5% khaki, 323 opaque, top y20). This one is leaner, darker, slightly shorter (317 opaque, top y22), with no mouth-smoke or back-flames, and the two-head silhouette separates it immediately. No wings. No shadow or plinth.
- Distinctness at 32px: all four separable (tall robed tiger-headed humanoid / modest-robed bat-winged horned fiend / short low many-legged reptile / gaunt two-headed dog). No shadow, plinth, or unwanted wing creep in any raw.

## Paths
- frames/{slug}/medium/se/mon_{slug}_medium_se_idle_00.png
- fal_raw/{slug}_se_idle_00_{raw,cutout}.png
- previews/mon_smoke25_se_idle_00_fal_x8.png (order rakshasa|succubus-incubus|basilisk|death-dog; #C2B08E; 34px cells; ×8 nearest)


## 2026-11-10 ~8:48 PM ET — AD SE audit
- AD soft-PASS on Succubus/Incubus, Basilisk, and Death Dog. Keep the succubus's two 2px wing/tail islands. The low basilisk is OK.
- Rakshasa SE REPASS for likeness: at 32px it read as a dark robed pillar (no tiger head, ears, or snout). Brief: enlarge and lighten the tiger head (pale muzzle, pointed ears clear of the silhouette), one backward-palmed clawed hand extended out from the robe, robe darker than the head so the head pops. Fine if it no longer fills the full bin height. Then 4f all four (rakshasa uses the new SE), each NE on its own SE baseline.


## Rakshasa SE REPASS — 2026-11-10 ~8:50 PM ET
v1 backups taken first (untouched after this): `frames/rakshasa/medium/se/_v1/mon_rakshasa_medium_se_idle_00.png`, `fal_raw/rakshasa_se_idle_00_raw_v1.png`, `fal_raw/rakshasa_se_idle_00_cutout_v1.png`.
Regen: `google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. First take; no second regen. post: PASS. No isolated 1px fringe specks.

| slug | REPASS gen request_id | REPASS birefnet request_id | opaque | post |
|---|---|---|---|---|
| rakshasa | 01a1c84b-8cb3-7a22-b0d6-7f8ae6e18702 | 01a1c84c-d0a3-7401-a866-97c75f47467e | 671 | PASS |

The new 32px frame no longer reads as a pillar. The tiger head is the lightest mass (khaki / #828282 concentrated in y1–y14), with a pale muzzle and two pointed ear peaks that break the outline (left ear tip at (5,1–2), right at (15–16,2)). The robe stays darker than the head, so the head pops. One arm is extended to the right; the backward palm itself is lost at 32px, but the held-out clawed arm is the tell. Still fills the bin (top y1, feet y46) because the tall pose is height-limited by `fal_post_to_bin`'s fit — same class as the v1 and the #22 dryad/satyr. AD said it was fine if it dropped off full height; it did not drop off, but the head is now a much larger share of the silhouette. Opaque 685→671 (−14). Khaki 4.5%→21.3% (pale head plus sash / hem / feet trim). Pivot (16,47) is 8-connected to the body via diagonal feet at (15,46) and (17,46) — not a contract fail; v1's y46 feet sat at x11–14 and left the pivot detached. Comps: 671 (one body; pivot attached diagonally). No shadow or plinth in the raw. No wings.

Preview: `previews/mon_smoke25_rakshasa_se_old_vs_new_x8.png` (v1 left, REPASS right; #C2B08E; ×8 nearest). The SE strip `previews/mon_smoke25_se_idle_00_fal_x8.png` was rebuilt with the new rakshasa so it matches the current SE.


## 4-facing (idle_00) — 2026-11-10 ~8:55 PM ET
SE = existing AD soft-PASS for succubus-incubus / basilisk / death-dog (untouched, md5 unchanged) plus the Rakshasa SE REPASS above. NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with the current SE raw as likeness reference (`image_urls` = uploaded `fal_raw/{slug}_se_idle_00_raw.png`) so NE keeps the SE design. Then `fal-ai/birefnet/v2` (CDN gen `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE, NW←NE via the smoke24 `mirror()` (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). Script: `scripts/build_smoke25_idle_4facing.py` (smoke24 clone). Config: `scripts/smoke25_common.py`. All NE first take; no rejects / no v1 files. Required tells were present on the first take (succubus both wings + horns + tail; death-dog both heads; basilisk many legs and low body; rakshasa tiger head and ears), so none were regenerated.

NE baselines already matched their SE after post (rakshasa y46; the other three y45, row 46 empty). No vertical shift was required, so no clip case.

| slug | NE gen request_id | NE birefnet request_id | post |
|---|---|---|---|
| rakshasa | 01a1c84e-6312-71e2-afc2-538ffbb6b190 | 01a1c84f-7df4-7a43-a638-c2d54fd995a0 | PASS |
| succubus-incubus | 01a1c84e-6312-7cd2-896a-62aa55c71784 | 01a1c84f-7e44-7d33-aff7-2371bc414cfe | PASS |
| basilisk | 01a1c84e-630e-7380-8e60-be8294a86982 | 01a1c84f-7e35-7ec2-a0fe-6154705098be | PASS |
| death-dog | 01a1c84e-62e2-7422-9991-eabc514c51fc | 01a1c84f-7e93-7b73-a176-4f2aa639cb0a | PASS |

### Speck clean (no fal call)
Isolated 1px fringe speck with no opaque 8-neighbours, #1E2228, set to alpha 0. Confirmed isolated first. No other pixel changed. Baseline stayed y45; pivot (16,47) unchanged.
- succubus-incubus NE (10,28) — opaque 671→670. Same site as the SE speck that was cleaned on the original SE pass. Leftover comps are 665+2+2+1 pivot.
Rakshasa, basilisk, and death dog NE had none.

Leftover non-1px components left as-gen (AD: keep the succubus islands):
- Succubus/Incubus all four facings: 2px #1E2228 island at (8–9, 26) and 2px #1E2228 island at (8,31)+(9,30) on SE/NE; mirrored to (23–24, 26) and (23,30)+(24,31) on SW/NW. Detached left-wing / tail-adjacent fragments that lost their 1px bridge at 32px. Connected in the raw. Not isolated 1px fringe. Left as-gen.

### Validation 16/16 PASS (size · pivot α255 · hard alpha · palette · empty edge cols · non-empty)
| slug | se opaque / base | sw | ne | nw | 8-conn comps (se/sw/ne/nw) | khaki% se→ne |
|---|---|---|---|---|---|---|
| rakshasa | 671 / y46 | 671 / y46 | 687 / y46 | 687 / y46 | 671 / 671 / 686+1 / 686+1 | 21.3→17.5 |
| succubus-incubus | 668 / y45 | 664 / y45 | 670 / y45 | 666 / y45 | 663+2+2+1 / 659+2+2+1 / 665+2+2+1 / 661+2+2+1 | 7.9→3.6 |
| basilisk | 304 / y45 | 300 / y45 | 385 / y45 | 382 / y45 | 303+1 / 299+1 / 384+1 / 381+1 | 12.2→3.4 |
| death-dog | 317 / y45 | 314 / y45 | 303 / y45 | 300 / y45 | 316+1 / 313+1 / 302+1 / 299+1 | 0.9→1.0 |

The +1 on succubus / basilisk / death-dog (all facings) and on rakshasa NE/NW is the detached pivot pixel (16,47) (contract). Rakshasa SE/SW comps are 671 (pivot 8-connected via diagonal feet at (15,46) and (17,46) SE, mirrored on SW). All four keep their SE baseline on every facing: rakshasa y46; the other three y45 with a row-46 gap. Tops: rakshasa y1 all facings; succubus-incubus y2 all facings; basilisk y26 SE/SW, y21 NE/NW; death-dog y22 SE/SW, y19 NE/NW.

No cast shadow or plinth in any NE raw. No wings crept into the non-winged three. The succubus keeps both bat wings, the horns, and the tail, fully inside the bin, on NE and NW.

md5 guard (`scripts/smoke25_md5_guard.py check-4f` vs `.smoke25_md5_pre_4f.json`, 14292 frames before the REPASS and this 4f pass): 1 changed (rakshasa medium se idle_00, the expected REPASS) + 12 added (smoke25 ne/sw/nw idle_00) + 0 removed. The other three #25 SE idle_00 files, smoke 1–24 frames, and clips.json: 0 modified/removed. Concurrent #24 / #23 / #22 anim added during this pass: 0.

### Soft notes (4f)
- Rakshasa NE: a true rear / back-three-quarter — back of the dark robe, khaki sash and hem from behind, pale tiger head in profile toward the upper-right, both pointed ears breaking the outline. Likeness holds: oversized light head on a darker robe, ears readable at 32px. The SE's extended clawed arm is not held out on the back view (both arms hang at the sides); the head-and-ears tell carries the turn instead. Khaki 21.3→17.5 (slight drop, the face is in profile). Opaque 671→687 (+16, +2%) — a small bulk increase, still filling the bin (top stays y1, feet y46). No shadow or plinth.
- Succubus/Incubus NE: the raw is a true rear / back-three-quarter (back of the modest robes, both bat wings spread, horns and hooked tail). At 32px the wide V of wings still dominates, so the silhouette is close to SE — same class as the #24 erinyes NE. The turn reads mainly as a less-khaki front (the khaki front panel is hidden: 7.9→3.6) and a slightly more centered hood. Both wings, both horn peaks, and the tail are present and fully inside the bin (first take; no regen). Opaque 668→670 (essentially unchanged), top stays y2. The two 2px islands noted above are left as-gen. Isolated speck at (10,28) cleaned. Still fully clothed and non-sexualized. No shadow or plinth.
- Basilisk NE: the strongest turn of the batch. Head toward the upper-right, long tail now curling along the lower-right, a row of many leg lumps still under the low body. Likeness holds: heavy low long-bodied reptile, not a four-legged dog or a snake, not stood up. Opaque 304→385 (+81, +27%) — a real bulk increase; the NE stance is 5px taller (top y26→y21) and longer. Khaki 12.2→3.4 (the mid-body khaki block is much less visible from this angle, not a redesign). No wings. No shadow or plinth.
- Death Dog NE: a clear three-quarter / rear-side turn. Both heads still read at 32px, now more side-by-side toward the upper-right rather than stacked. Likeness holds: gaunt mangy four-legged dog, two necks, no mouth-smoke, no back-flames, no wings. Slightly thinner and a little taller: 317→303 (−14, −4%), top y22→y19 (3px taller). Khaki stays ~1% (0.9→1.0, no khaki drift). Still distinct from the #22 blink dog and the #24 hell hound. No shadow or plinth.
- Distinctness at 32px: all four stay separable on every facing (pale-headed robed tiger fiend / modest-robed bat-winged horned fiend / short low many-legged reptile / gaunt two-headed dog).
- SW/NW mirrors lose 0–4 px from the col-31→clear shift (rakshasa loses none; succubus-incubus SE→SW loses 4, NE→NW loses 4; basilisk SE→SW loses 4, NE→NW loses 3; death-dog SE→SW loses 3, NE→NW loses 3).

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_idle_00.png
- fal_raw/{slug}_{se,ne}_idle_00_{raw,cutout}.png
- fal_raw/rakshasa_se_idle_00_{raw,cutout}_v1.png and frames/rakshasa/medium/se/_v1/ (REPASS backups)
- previews/mon_{slug}_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw) · previews/mon_smoke25_4facing_idle_00_fal_x8.png (rows rakshasa|succubus-incubus|basilisk|death-dog, 1036×1560)
- previews/mon_smoke25_rakshasa_se_old_vs_new_x8.png (v1 | REPASS, 532×396)


## Anim (2026-11-10) — built, awaiting AD audit
Medium 32×48, pivot (16,47); source = Art 4f soft-PASS fal idle_00 (never written). The rakshasa SE key is the REPASS v2 at `frames/rakshasa/medium/se/mon_rakshasa_medium_se_idle_00.png` (md5 `3f602a1ec9a055160463c35d031d6989`); the v1 backup under `frames/rakshasa/medium/se/_v1/` and `fal_raw/rakshasa_se_idle_00_{raw,cutout}_v1.png` was not opened as a source and was not overwritten. Template = modest smoke #18–#24 (same bob/sway/scale amplitudes, no species flourishes — no wing flap, no tiger-claw rake, no multi-head snap), with the per-creature exceptions below.
Regenerate (from repo root): `python3 scripts/build_smoke25_idle_clips.py && python3 scripts/build_smoke25_walk_clips.py && python3 scripts/build_smoke25_remaining_clips.py && python3 scripts/smoke25_md5_guard.py check-anim` (config: `scripts/smoke25_common.py`; locked idle_00 in `.smoke25_idle00_locked.json`; baseline taken first with `smoke25_md5_guard.py baseline`). `smoke25_md5_guard.py check` / `check-se` and `check-4f` still compare frames to the Art snapshots.

split_y se/sw/ne/nw: rakshasa 45/45/46/46 · succubus-incubus 35/35/35/35 · basilisk 36/36/33/33 · death-dog 36/36/36/36. All grounded; never floater split=H-1=47. Baseline respected: rakshasa plants on y46 (top y1, fills the bin — same class as #22 dryad/satyr); the other three plant on y45 with the contract row-46 pivot gap on every facing.
- Rakshasa: standard detect (SE/SW split 45 plants the y45–46 foot rows; NE/NW split 46 plants the y46 foot row). Standard modest template. Because the figure fills the bin (top y1 on every facing), fall lift is capped by the existing headroom rule: f00/f02 lift 3→1, f01 lift 4→0 on all four facings. lift→0 is accepted (same handling as dryad/satyr).
- Succubus/Incubus: standard detect, treated as grounded (standing, wings spread), not a floater. Top y2. Wing-span edge-col truncations are expected (SE/NE walk f02/f03 dx −1→0; many remaining-clip dx/sw caps). The two 2px #1E2228 wing/tail islands on every facing (SE/NE at (8–9, 26) and (8,31)+(9,30); SW/NW mirrored to (23–24, 26) and (23,30)+(24,31)) are intentional Art (AD: keep them). They sit above split, so they move with the body (idle lift shifts them 1 px; walk hop shifts them 2 px) and were not deleted. Idle component scan logged all eight islands as `COMP-EXEMPT` (not a stray-pixel fault); validation does not fail on them.
- Basilisk: low many-legged reptile. `detect_low` splits at the midpoint of the occupied body so the lower half is the undercarriage / leg band (standard 50%-from-foot would have planted only y42–45 SE/SW and y44–45 NE/NW). Walk is a low leg-ripple: body lift capped at 0–1 px (`[0,1,1,1,0,0]`, no airborne hop), plus staggered 1px x-shifts of even/odd undercarriage rows on f01–f03; no ripple row was skipped for an edge column. Tagged `"quadruped": true`, `"low": true`, `"leg_ripple": true`. 5 distinct walk frames on every facing; plant ≠ f02. Remaining clips use the standard modest template (the low body has headroom, so fall lift is not capped).
- Death Dog: standard detect; quadruped walk = mastiff/hyena modest template (legs planted below split, torso bob/sway, 1px airborne on f02), tagged `"quadruped": true`. SE walk f02/f03 dx −1→0 (col-1 head/snout).
Floaters: none. Bridge: all four `full_row`. Subtle: none (no hop fallback). Walk distinct frames: rakshasa 5/5/5/5 · succubus-incubus 4/5/4/5 · basilisk 5/5/5/5 · death-dog 4/5/5/5 (se/sw/ne/nw).

Counts: 608 frames / 144 clips tagged `"smoke": 25`: idle 64 (16), walk 96 (16), react 48, attack 96, cast 64, stun 64, death 80, fall 48, fall_impact 48 (16 each). Validation ALL PASS ×3 (exit 0) plus a full post-bake pixel pass (608/608, palette / hard alpha / empty edge cols / grounded pivot, 0 errors). Idle_00 lock: 16/16, including rakshasa SE v2. v1 backup hash unchanged (`e017f8ba6ad4fa222d469f95977c5901`).
Truncations: `truncations.smoke25` 234 notes (death 64, fall 40 of which 24 are lift caps, attack 39, cast 27, fall_impact 27, react 21, stun 16), `truncations.smoke25_walk` 10 (succubus-incubus se+ne f02/f03 dx −1→0; basilisk se+ne f02/f03 dx −1→+1; death-dog se f02/f03 dx −1→0). Rakshasa and basilisk/death-dog SW+NW (and rakshasa all-facing walk) kept the 1px walk sway. Lift caps: rakshasa fall 12 (3→1 / 4→0 / 3→1, all facings); succubus-incubus fall 12 (3→2 / 4→0 / 3→2, all facings). No basilisk or death-dog lift caps. No basilisk ripple-row edge skips.
md5 guard: 17742 entries (smoke 1–24 frames including #24 anim outputs + 16 locked smoke25 Art idle_00 + clips.json non-smoke25 entries/truncations); drift 0. Locked #25 idle_00 also verified against `.smoke25_idle00_locked.json`.
Contacts: 36 `previews/mon_{slug}_medium_{clip}_cycle_4facing_x8.png` (slug ∈ rakshasa, succubus-incubus, basilisk, death-dog; clip ∈ idle, walk, react, attack, cast, stun, death, fall, fall_impact) plus per-creature audit sheets `previews/mon_{slug}_medium_anim_all_clips_4facing_x8.png` and combined `previews/mon_smoke25_anim_all_clips_4facing_x8.png`. x4 downscales of the per-creature sheets: `/workspace/smoke25_{slug}_x4.png`.

Art soft notes carried into Anim (not compensated): Rakshasa still fills the bin after the SE REPASS (top y1, feet y46, pale oversized tiger head on a darker robe; NE is a back-three-quarter with the arms hanging, ears still readable). Succubus/Incubus stays fully clothed and non-sexualized; the wide V of bat wings dominates every facing (NE reads mainly as a less-khaki front, 7.9→3.6) and the two 2px wing/tail islands stay. Basilisk is short and width-limited (SE top y26, 304 opaque) with a stronger, bulkier NE (+27% opaque, top y26→y21) that still reads as a low many-legged reptile, not a dog or a snake. Death Dog is the gaunt two-headed dog (both heads read at 32px), leaner and almost no khaki, distinct from the #22 blink dog and the #24 hell hound; NE is a slightly thinner, 3px taller three-quarter (317→303, top y22→y19) with the heads more side-by-side. SW/NW mirrors already lost 0–4 px from the col-31→clear shift at Art 4f; Anim does not restore them.

**AD sign-off (records, 2026-11-24 CT): Art+Anim PASS (soft notes as recorded above). This batch was passed by AD in chat and reported Done to Producer at the time; this line only writes that verdict into the doc. Supersedes any "awaiting AD audit" status above. Local only, no PR.**
