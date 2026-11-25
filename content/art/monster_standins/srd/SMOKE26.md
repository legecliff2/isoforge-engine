# Smoke #26 — Mediums SE idle_00 (fal)

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449) and the Group D follow-up:** This note is the production record for smoke #26. The tree now holds 37 medium creatures in Long Dusk Mapping B (26 from #449/#458, plus 11 Group D / couatl kits). From this smoke, the tree includes: `ettercap`, `grick`, `harpy`. Held back (kept local): `chain-devil`, `rakshasa`, `doppelganger`, `mimic`.

**Status: SE soft-PASS (AD) · 4-facing soft-PASS (AD) · Art Done · Anim built, awaiting AD audit**

Roster: Doppelganger | Ettercap | Grick | Harpy — all **medium** 32×48, pivot (16,47) opaque.

## Pipeline / contract
`google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium` (medium LAST). post: 4/4 PASS.

## IDs
| slug | gen request_id | birefnet request_id | opaque | post |
|---|---|---|---|---|
| doppelganger | 01a1c854-8df0-7061-af73-68f1fffb5d5f | 01a1c855-837a-7a93-827e-e849fef87997 | 446 | PASS |
| ettercap | 01a1c854-8e62-72c3-aef5-b6c9eb8818c0 | 01a1c855-832a-70d1-a703-83061a975a3d | 453 | PASS |
| grick | 01a1c854-8e62-7253-9916-962c3f8a48e2 | 01a1c855-8663-7d71-96ad-d4f0d85eb949 | 402 | PASS |
| harpy | 01a1c854-8de6-7fe3-8367-4a6ee641a99e | 01a1c855-84e1-7862-b184-038006a6c8a7 | 669 | PASS |

All first take; no rejects / no v1 files. No generation missed its tell, so none were regenerated.

## Validation (4/4 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty.
Comps (8-conn): doppelganger 445+1 pivot · ettercap 452+1 pivot · grick 401+1 pivot · harpy 668+1 pivot.
Feet: ettercap, grick, and harpy plant on y45 with a row-46 gap (same floater as azer / wolf / the #23–#25 batches / most prior mediums). Doppelganger plants on y46 (top y1) because the tall upright gangly pose is height-limited by `fal_post_to_bin`'s fit (nh=46, pasted at y=1, bottom y46) — same class as the #22 dryad and satyr and the #25 rakshasa. Not a post-script miss; the figure fills the bin.
Grick is short in the bin (top at y21, 402 opaque) because the wide coiled trunk is width-limited by the same fit — same class as the #25 basilisk (top y26) and the #23 bearded devil (top y20). The whole creature is inside the bin, not cropped.
No cast shadow or plinth in any raw. No wings crept into the non-winged three. Smoke 1–25 frames md5: 0 modified/removed. Guard: `.smoke26_md5_pre_se.json` (14304, post-#25 4f idle, pre-#26) → 0 changed / 0 removed / 4 smoke26 SE additions. 592 concurrent smoke25 anim clips also appeared vs that snapshot (Animator bake during this pass, not written here); clips.json was not touched by this pass.

## Speck clean (no fal call)
No isolated 1px fringe specks (no opaque 8-neighbours) on any of the four. Nothing cleaned.

Leftover non-1px components left as-gen for AD:
- None. Each frame is one body plus the detached pivot pixel.

## Soft notes (SE)
- Doppelganger: SE ✓ (head toward the lower-right). True form: a tall grey featureless gangly humanoid with extremely long thin arms hanging almost to the ankles, too-long skinny legs, and a smooth hairless oval head. The raw shows two large bulbous pale eyes and no other facial detail, no clothes. At 32px the pale eyes shrink to a slightly lighter grey on the head, but the gangly too-long proportions are the tell and they read clearly — it does not read as a normal clothed person. Almost no khaki (0.7%). Tall, fills the bin (446 opaque, top y1, feet on y46). No wings. No shadow or plinth.
- Ettercap: SE ✓ (head lower-right, one long clawed arm reaching down-right). Hunched spider-like humanoid standing on two spindly legs, with a humanoid torso, long multi-jointed arms and clawed hands, and a bulbous khaki-furred abdomen (14.8% khaki — the strongest 32px tell). The raw shows a cluster of small eyes, mandibles/fangs, and optional thin webbing strands. At 32px the eyes, fangs, and webbing are lost (same class as other lost faces); the hunched khaki-backed body plus spindly reaching claws still reads as a spider-man hybrid, not as a plain eight-legged spider on the ground. Mid-height in the bin (453 opaque, top y9, feet on y45). No wings. No shadow or plinth.
- Grick: SE ✓ (head / tentacle crown toward the lower-right). Worm-like body with a thick segmented trunk in a wide S-coil, no limbs, and a head of four hooked tentacles around a beaked mouth — the tentacle crown is present in the raw and still reads at 32px as a cluster of hooks reaching off the raised head, and that is the tell. The pose is a sideways coil with the head raised, not a fully upright vertical trunk; the wide coil is why it sits short in the bin (402 opaque, top y21, feet on y45), same width-limited class as the #25 basilisk. Low khaki (2.5%). No arms, no legs, no wings. No shadow or plinth.
- Harpy: SE ✓ (head slightly right, wings up). Hag-like woman's upper body with wild tangled hair, large vulture wings fully inside the bin (the strongest 32px tell), and taloned bird legs. Clothed in ragged feathers and tattered rags covering the torso; neutral standing pose, clearly non-sexualized — a monstrous crone, no revealing outfit, no body-shape emphasis. Wings and talons both read at 32px. Almost no khaki (0.9%). Heaviest of the four (669 opaque, top y1, feet on y45) because of the spread wings. Distinct from the #25 succubus/incubus (bat-membrane V wings, horned hood, hooked tail, modest robes with a 7.9% khaki front panel, 668 opaque, top y2, humanoid legs) and the #24 erinyes (feathered wings but a smaller body under them, top y17, width-limited). No shadow or plinth.
- Distinctness at 32px: all four separable (tall gangly featureless humanoid / hunched khaki-backed spider-humanoid / short coiled tentacle-crowned worm / vulture-winged hag with bird talons). No shadow, plinth, or unwanted wing creep in any raw.

## Paths
- frames/{slug}/medium/se/mon_{slug}_medium_se_idle_00.png
- fal_raw/{slug}_se_idle_00_{raw,cutout}.png
- previews/mon_smoke26_se_idle_00_fal_x8.png (order doppelganger|ettercap|grick|harpy; #C2B08E; 34px cells; ×8 nearest)


## 2026-11-10 ~9:04 PM ET — AD SE audit
- AD soft-PASS on all four. Doppelganger reads as gangly and featureless. Ettercap's khaki abdomen plus claws gives a clear spider-humanoid read. Grick's coil, beak and tentacle crown read, and the short sit is accepted. Harpy's vulture wings dominate, monstrous and non-sexualized. Proceed to NE and 4f; on NE, the grick's tentacle crown must still read as separate hooks, not a blob.

## 4-facing (idle_00) — 2026-11-10 ~9:07 PM ET
SE = existing AD soft-PASS (untouched, md5 unchanged). NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with the SE raw as likeness reference (`image_urls` = uploaded `fal_raw/{slug}_se_idle_00_raw.png`) so NE keeps the SE design. Then `fal-ai/birefnet/v2` (CDN gen `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE, NW←NE via the smoke25 `mirror()` (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). Script: `scripts/build_smoke26_idle_4facing.py` (smoke25 clone). Config: `scripts/smoke26_common.py`. All NE first take; no rejects / no v1 files. The required tells were present on the first take (doppelganger grey/featureless/gangly; ettercap hunched khaki abdomen and claws; grick four-hooked tentacle crown readable as separate hooks at 32px, not a blob; harpy both vulture wings inside the bin, talons visible, non-sexualized), so none were regenerated.

NE baselines already matched their SE after post (doppelganger y46; the other three y45, row 46 empty). No vertical shift was required, so no clip case.

| slug | NE gen request_id | NE birefnet request_id | post |
|---|---|---|---|
| doppelganger | 01a1c859-adde-7a32-8f76-1b5463e15791 | 01a1c85a-922f-7691-9c55-98fa064452eb | PASS |
| ettercap | 01a1c859-ae9f-7161-9657-ca91ac36adc2 | 01a1c85a-92a7-74b3-a777-ded33b24b140 | PASS |
| grick | 01a1c859-b798-7be1-b273-013261470310 | 01a1c85a-e53a-7482-8afe-d0aaff567f9f | PASS |
| harpy | 01a1c859-bd89-7672-b342-ca34ffa08ff7 | 01a1c85a-eb0d-7662-a72f-7ad6aabc03c4 | PASS |

### Speck clean (no fal call)
Isolated 1px fringe speck with no opaque 8-neighbours, #1E2228, set to alpha 0. Confirmed isolated first. No other pixel changed. Baseline stayed y45; pivot (16,47) unchanged.
- harpy NE (9,39) — opaque 510→509. Leftover comps are 508+1 pivot.
Doppelganger, ettercap, and grick NE had none.

Leftover non-1px components left as-gen:
- None. Each frame is one body plus the detached pivot pixel.

### Validation 16/16 PASS (size · pivot α255 · hard alpha · palette · empty edge cols · non-empty)
| slug | se opaque / base | sw | ne | nw | 8-conn comps (se/sw/ne/nw) | khaki% se→ne |
|---|---|---|---|---|---|---|
| doppelganger | 446 / y46 | 446 / y46 | 495 / y46 | 495 / y46 | 445+1 / 445+1 / 494+1 / 494+1 | 0.7→0.0 |
| ettercap | 453 / y45 | 449 / y45 | 445 / y45 | 441 / y45 | 452+1 / 448+1 / 444+1 / 440+1 | 14.8→10.1 |
| grick | 402 / y45 | 399 / y45 | 370 / y45 | 366 / y45 | 401+1 / 398+1 / 369+1 / 365+1 | 2.5→0.0 |
| harpy | 669 / y45 | 667 / y45 | 509 / y45 | 508 / y45 | 668+1 / 666+1 / 508+1 / 507+1 | 0.9→0.4 |

The +1 on every facing is the detached pivot pixel (16,47) (contract). All four keep their SE baseline on every facing: doppelganger y46; the other three y45 with a row-46 gap. Tops: doppelganger y1 all facings; ettercap y9 all facings; grick y21 SE/SW, y22 NE/NW; harpy y1 SE/SW, y14 NE/NW.

No cast shadow or plinth in any NE raw. No wings crept into the non-winged three. The harpy keeps both vulture wings and the talons, fully inside the bin, on NE and NW. The grick's four-hooked tentacle crown still reads at 32px as a crown of separate hooks reaching off the raised head (not a blob); no regen.

md5 guard (`scripts/smoke26_md5_guard.py check-4f` vs `.smoke26_md5_pre_4f.json`, 14900 frames after the #26 SE idle_00 and before this 4f pass): 12 added (smoke26 ne/sw/nw idle_00) + 0 changed + 0 removed. The four #26 SE idle_00 files, smoke 1–25 frames, and clips.json: 0 modified/removed. Concurrent anim added during this pass: 0.

### Soft notes (4f)
- Doppelganger NE: a true rear / back-three-quarter — back of the grey featureless body, head in profile toward the upper-right, the same too-long hanging arms and skinny legs. Likeness holds: tall grey gangly true-form humanoid, no clothes, no facial detail. Khaki 0.7→0.0 (the three stray khaki pixels on SE were never a tell). Opaque 446→495 (+49, +11%) — a modest bulk increase, still filling the bin (top stays y1, feet y46). No wings. No shadow or plinth.
- Ettercap NE: a modest turn. The raw is a back three-quarter (khaki abdomen from behind, head in profile toward the upper-right, both clawed arms still reaching), but at 32px the hunched silhouette stays close to SE — same class as earlier modest turns AD has accepted. Likeness holds: hunched spider-humanoid, khaki abdomen and spindly claws still the tell. Khaki 14.8→10.1 (the abdomen is a little less dominant from this angle, not a redesign). Opaque 453→445 (−8, −2%), top stays y9, feet y45. No wings. No shadow or plinth.
- Grick NE: a clear three-quarter / rear-side turn. The coil opens into a raised S, head and tentacle crown toward the upper-right. The four-hooked tentacle crown still reads at 32px as a cluster of separate hooks reaching off the raised head — not a blob — so this is a first take, no regen. Likeness holds: legless segmented worm, beaked mouth, no arms, no wings. Slightly thinner and 1px shorter: 402→370 (−32, −8%), top y21→y22. Khaki 2.5→0.0 (the pale under-belly quantizes away from this angle, not a redesign). No shadow or plinth.
- Harpy NE: a true rear / back-three-quarter — back of the ragged feathered torso, wild hair, both vulture wings spread, taloned bird legs, head in profile toward the upper-right. Both wings and the talons stay fully inside the bin (first take; no regen). Still clothed and non-sexualized — a monstrous crone, no revealing outfit. The NE stance is shorter and lighter because the wings sit more horizontally than the SE's tall raised wing: 669→509 (−160, −24%), top y1→y14 (13px shorter). That is a real bulk drop, but the whole figure including both wings is inside the bin, not cropped. Khaki stays near zero (0.9→0.4, no khaki drift; the raw's browner hair quantized to grey). Isolated speck at (9,39) cleaned. No shadow or plinth.
- Distinctness at 32px: all four stay separable on every facing (tall gangly featureless humanoid / hunched khaki-backed spider-humanoid / short coiled tentacle-crowned worm / vulture-winged hag with bird talons).
- SW/NW mirrors lose 0–4 px from the col-31→clear shift (doppelganger loses none; ettercap SE→SW loses 4, NE→NW loses 4; grick SE→SW loses 3, NE→NW loses 4; harpy SE→SW loses 2, NE→NW loses 1).

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_idle_00.png
- fal_raw/{slug}_{se,ne}_idle_00_{raw,cutout}.png
- previews/mon_{slug}_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw) · previews/mon_smoke26_4facing_idle_00_fal_x8.png (rows doppelganger|ettercap|grick|harpy, 1036×1560)


## Anim (2026-11-10) — built, awaiting AD audit
Medium 32×48, pivot (16,47); source = Art 4f soft-PASS fal idle_00 (never written). Template = modest smoke #18–#25 (same bob/sway/scale amplitudes, no species flourishes — no wing flap, no tentacle lash, no web shot), with the per-creature exceptions below.
Regenerate (from repo root): `python3 scripts/build_smoke26_idle_clips.py && python3 scripts/build_smoke26_walk_clips.py && python3 scripts/build_smoke26_remaining_clips.py && python3 scripts/smoke26_md5_guard.py check-anim` (config: `scripts/smoke26_common.py`; locked idle_00 in `.smoke26_idle00_locked.json`; baseline taken first with `smoke26_md5_guard.py baseline`). `smoke26_md5_guard.py check` / `check-se` and `check-4f` still compare frames to the Art snapshots.

split_y se/sw/ne/nw: doppelganger 41/41/43/43 · ettercap 38/38/38/38 · grick 29/29/33/33 · harpy 34/34/37/37. All grounded; never floater split=H-1=47. Baseline respected: doppelganger plants on y46 (top y1, fills the bin — same class as #22 dryad/satyr and #25 rakshasa); the other three plant on y45 with the contract row-46 pivot gap on every facing.
- Doppelganger: standard detect, standard modest biped template. Because the figure fills the bin (top y1 on every facing), fall lift is capped by the existing headroom rule: f00/f02 lift 3→1, f01 lift 4→0 on all four facings. lift→0 is accepted (same handling as dryad/rakshasa).
- Ettercap: standard detect; biped hunch walk = standard planted legs plus a modest torso bob (`lift [0,1,2,1,0,0]`, 1px airborne on f02) and a slight forward lean of the upper torso (rows above the mid-torso hunch line y23) on f01–f03. Legs below split were verified unchanged on every non-hop walk frame. Tagged `"hunch": true`. One hunch row skipped for an edge on SE/NE f02 only (y22). Remaining clips use the standard modest template (mid-height, top y9, so fall lift is not capped).
- Grick: legless coiled worm. `detect_puddle` splits at the top of the lowest wide bulge (SE/SW 29, NE/NW 33) so the coil stays planted and the tentacle crown bobs above it. Walk is a coil slide/undulate: body lift capped at `[0,1,1,1,0,0]` (no airborne hop, no leg swing), `bridge=body_only`, tagged `"legless": true`, `"undulate": true`. **Walk patches (2026-11-10, after AD soft-PASS of the other three):** (1) The first bake's staggered per-row coil shift sheared the coil; the whole body/coil now shares one uniform dx (`[0,0,-1,-1,0,0]`). Pixels that would enter col 0/31 are clipped; SE/NE f02/f03 clip overflow on tight mid-coil rows (SE y34–36, NE y34–37). (2) A follow-up AD pass still saw a 1px vertical background gap at split_y on SE walk f01–f03: lifting the torso left `split-1` vacated, and `bridge_body_only` kept only the longest run on that row, which also detached the left tentacle stub as a new 8-conn island. Re-baked only the 4-facing walk clip again. The base stays on y45 and the pivot stays planted (the whole sprite is not lifted). When the torso lifts by 1, the vacated row (`split-1`) is filled by duplicating the split row (the row just below the gap), with `body_only` skipped on that fill so every run — including the tentacle stub — stays 8-connected to the body. Plant / squash frames (f00/f04/f05) stay byte-identical to the previous bake. 4 distinct walk frames on every facing; plant ≠ f02. Gap/component check on all 24 walk frames: 0 fully transparent rows inside the body bbox, 0 new disconnected 8-conn components vs idle_00 (and 0 partial mid-body holes). Fall/death were not retouched.
- Harpy: standard detect, treated as grounded (standing, vulture wings spread), not a floater. Biped walk, feet on y45. Wing-span edge-col truncations are expected (SE/NE walk f02/f03 dx −1→0; many remaining-clip dx/sw caps). The wings sit above split and move with the body (verified: the SE/SW wing-tip pixels at y1 rise to y0 on idle_01). idle_00 has no disconnected 8-conn wing islands (one body + the contract pivot on every facing, matching the Art 4f note); the idle component scan logged a `COMP-EXEMPT note` to that effect rather than flagging a stray-pixel fault. If a later frame grew a detached fragment it would be kept and logged `COMP-EXEMPT`, not deleted. SE/SW also fill the bin height (top y1, feet y45), so fall lift is capped the same way as the doppelganger (3→1 / 4→0 / 3→1). NE/NW are shorter (top y14) and keep the uncut fall lift.
Floaters: none. Bridge: grick `body_only`; the other three `full_row`. Subtle: none (no hop fallback). Walk distinct frames: doppelganger 5/5/5/5 · ettercap 5/5/5/5 · grick 4/4/4/4 (after the walk patch) · harpy 4/5/4/5 (se/sw/ne/nw).

Counts: 608 frames / 144 clips tagged `"smoke": 26`: idle 64 (16), walk 96 (16), react 48, attack 96, cast 64, stun 64, death 80, fall 48, fall_impact 48 (16 each). Validation ALL PASS ×3 (exit 0) plus a full post-bake pixel pass (608/608, palette / hard alpha / empty edge cols / grounded pivot, 0 errors). Idle_00 lock: 16/16.
Truncations: `truncations.smoke26` 196 notes (death 64, attack 31, fall 29 of which 18 are lift caps, fall_impact 28, cast 20, react 13, stun 11), `truncations.smoke26_walk` 10 after the grick walk patch (ettercap se+ne f02 hunch skipped row y22; harpy se+ne f02/f03 dx −1→0; grick se f02/f03 uniform coil dx −1 clipped edge overflow on rows y34–36, ne f02/f03 on y34–37). Doppelganger and ettercap kept the 1px walk sway on all facings; grick's walk is now a uniform whole-coil slide on f02/f03 (no per-row skips). Lift caps: doppelganger fall 12 (3→1 / 4→0 / 3→1, all facings); harpy SE/SW fall 6 (same 3→1 / 4→0 / 3→1). No ettercap, grick, or harpy NE/NW lift caps.
md5 guard: 18492 entries (smoke 1–25 frames including #25 anim outputs + 16 locked smoke26 Art idle_00 + clips.json non-smoke26 entries/truncations). Protected set (s1–s25 outputs and the 16 locked #26 idle_00 keys): 0 changed / 0 removed. Concurrent Art during this bake added 4 next-batch SE idle_00 files (gray-ooze, medusa, mimic, rust-monster; written ~9:11 PM ET, not written here); those are reported as CONCURRENT-ADDED and are not a bake fault. Locked #26 idle_00 also verified against `.smoke26_idle00_locked.json`.
Contacts: 36 `previews/mon_{slug}_medium_{clip}_cycle_4facing_x8.png` (slug ∈ doppelganger, ettercap, grick, harpy; clip ∈ idle, walk, react, attack, cast, stun, death, fall, fall_impact) plus per-creature audit sheets `previews/mon_{slug}_medium_anim_all_clips_4facing_x8.png` and combined `previews/mon_smoke26_anim_all_clips_4facing_x8.png`. x4 downscales of the per-creature sheets: `/workspace/smoke26_{slug}_x4.png`.

Art soft notes carried into Anim (not compensated): Doppelganger stays the tall grey featureless gangly true form (almost no khaki; NE is a back-three-quarter with the same too-long arms, +11% opaque, still filling the bin). Ettercap stays the hunched khaki-backed spider-humanoid (khaki abdomen 14.8→10.1 on NE, spindly claws still the tell; top stays y9). Grick stays the legless segmented S-coil with a four-hooked tentacle crown that still reads as separate hooks, not a blob; NE is slightly thinner and 1px shorter (402→370, top y21→y22). Harpy stays a monstrous non-sexualized vulture-winged crone; NE is a true rear with both wings and talons inside the bin but a real bulk drop (669→509, top y1→y14) because the wings sit more horizontally — the whole figure is inside the bin, not cropped. SW/NW mirrors already lost 0–4 px from the col-31→clear shift at Art 4f; Anim does not restore them.

**AD sign-off (records, 2026-11-24 CT): Art+Anim PASS (soft notes as recorded above). This batch was passed by AD in chat and reported Done to Producer at the time; this line only writes that verdict into the doc. Supersedes any "awaiting AD audit" status above.**

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449) and the Group D follow-up:** This note is the production record for smoke #26. The tree now holds 37 medium creatures in Long Dusk Mapping B (26 from #449/#458, plus 11 Group D / couatl kits). From this smoke, the tree includes: `ettercap`, `grick`, `harpy`. Held back (kept local): `chain-devil`, `rakshasa`, `doppelganger`, `mimic`.
