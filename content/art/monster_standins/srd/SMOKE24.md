# Smoke #24 — Mediums SE idle_00 (fal)

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449):** This note is the production record for smoke #24. The art PR ships 26 medium creatures that have content spec rows on main, not the original 40. From this smoke, the PR includes: `hell-hound`. Held back (kept local, not in the PR): `azer`, `barbed-devil`, `bearded-devil`, `chain-devil`, `erinyes`, `lemure`, `night-hag`, `rakshasa`, `doppelganger`, `mimic`, `swarm-of-beetles`, `swarm-of-centipedes`, `swarm-of-spiders`, `swarm-of-wasps`.

**Status: SE soft-PASS (AD) · 4-facing soft-PASS (AD) · Art Done · Anim built, awaiting AD audit**

Roster: Erinyes | Hell Hound | Lemure | Night Hag — all **medium** 32×48, pivot (16,47) opaque.

## Pipeline / contract
`google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium` (medium LAST). post: 4/4 PASS.

## IDs
| slug | gen request_id | birefnet request_id | opaque | post |
|---|---|---|---|---|
| erinyes | 01a1c83d-b1f6-7b02-ac9d-6b5ec3d5f9c4 | 01a1c83e-9be1-7ba3-90fd-016da42e5608 | 255 | PASS |
| hell-hound | 01a1c83d-b1d7-7c11-9dad-a36ad763215a | 01a1c83e-9bdf-71a1-aacb-49634bea6434 | 323 | PASS |
| lemure | 01a1c83d-b222-7501-8a26-ba33294484f6 | 01a1c83e-9b97-7052-ba7b-ef6b0a7356e6 | 535 | PASS |
| night-hag | 01a1c83d-b1ed-73f2-aeca-ac2706046f05 | 01a1c83e-9b5e-79d0-a9f1-783d3d337609 | 526 | PASS |

All first take; no rejects / no v1 files. No isolated 1px fringe specks to clean.

## Validation (4/4 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty.
Comps (8-conn): erinyes 254+1 pivot · hell-hound 322+1 pivot · lemure 534+1 pivot · night-hag 525+1 pivot.
Feet: all four plant on y45 with a row-46 gap (same floater as azer / wolf / blink-dog / the #23 batch / most prior mediums). Erinyes is short in the bin (top at y17, 255 opaque) because the wide uncropped feathered wings are width-limited by `fal_post_to_bin`'s fit — same class as the #23 bearded devil (top y20) and the #22 blink-dog (top y20). Not a post-script miss; the wings are fully inside the bin, not cropped.
No cast shadow or plinth in any raw. No wings crept into the non-winged three. Smoke 1–23 frames md5: 0 modified/removed. Guard: `.smoke24_md5_pre_se.json` (13088, post-#23 4f idle, pre-#24) → 0 changed / 0 removed / 4 smoke24 SE additions. 592 concurrent smoke23 anim clips also appeared vs that snapshot (Animator bake during this pass, not written here); clips.json was not touched by this pass.

## Speck clean
None. No isolated 1px fringe specks (no opaque 8-neighbours) on any of the four frames. No leftover non-1px detached art islands either — each sprite is one body component plus the contract pivot.

## Soft notes (SE)
- Erinyes: SE ✓ (head and longsword toward the lower-right). Armored winged fiend warrior, helmeted, large feathered wings fully inside the bin (the 32px tell — a wide V of feathered mass). The longsword is a thin diagonal from the mid-body down-right; the face is lost under the helmet, and a small horn bump reads on the helm. Khaki on the left wing / midtone armor (14.1%). Smallest of the four (255 opaque, top y17) because the wings force a width-limited scale — the body is thin, but the wings themselves survive and are not cropped off. No invented bits, no shadow or plinth.
- Hell Hound: SE ✓ (head lower-right, open jaws). Muscular four-legged fiend dog, crouched and darker than the #22 blink dog. The 32px tells are the pale grey puff at the snout (mouth-smoke / flame, still connected to the head) and a grey ridge along the back (back-flames). Slightly bulkier than the blink dog (323 vs 303 opaque) at the same top y20, but the stance is lower and heavier, with less khaki (6.5% vs the blink dog's 20.1% on legs / muzzle / tail tip). The blink dog is a leaner walking fey hound with pointed-up ears and a raised tail; this one reads as a fire-breathing, more menacing hound. No wings. No shadow or plinth.
- Lemure: SE-ish (dripping head facing lower-right). Shapeless melting sludge lump — a vaguely human head and torso dripping into a wide puddle-like base, no legs. Clearly not a standing person at 32px; the puddle hem and dripping arms are the silhouette. Warmest of the four (khaki 33.3%). Heaviest mass of the batch (535 opaque, top y14). No wings, no weapons. No shadow or plinth.
- Night Hag: SE ✓ (hunched, claws reaching lower-right). Gaunt fiendish crone, dark skin, wild hair, ragged robes, clawed hands. The horns are the 32px tell: a ram-curl protrusion on the upper-right of the head, visible even with the face mostly lost. Distinct from the #22 green hag (bulkier 558, top y12, khaki shawl/hair accents at 12.5%, no horns) and the #23 sea hag (lighter 423, top y14, fully dark 0% khaki, drippier ragged hem, no horns, a single reaching claw). This night hag is almost as dark as the sea hag (khaki only 1.1%) but fuller in the robe (526, top y12, same height class as the green hag) and the horn bump separates her from both. No wings. No shadow or plinth.
- Distinctness at 32px: all four separable (winged sword-wielder / bulky fire-snouted hound / khaki sludge puddle / dark horned hunched crone). No shadow, plinth, or unwanted wing creep in any raw.

## Paths
- frames/{slug}/medium/se/mon_{slug}_medium_se_idle_00.png
- fal_raw/{slug}_se_idle_00_{raw,cutout}.png
- previews/mon_smoke24_se_idle_00_fal_x8.png (order erinyes|hell-hound|lemure|night-hag; #C2B08E; 34px cells; ×8 nearest)


## 2026-11-10 ~8:37 PM ET — AD SE soft-PASS
- AD soft-PASS on SE (Erinyes, Hell Hound, Lemure, Night Hag). Softs accepted: the Erinyes body is small under the wings; the Hell Hound is a busy crouch (the smoke and dark tone separate it from the blink dog); the Lemure is warm.
- Current baseline kept for all four: y45 with a row-46 gap. 4f kicked with each NE on y45 to match its SE. The Erinyes NE must keep both feathered wings, fully inside the bin.

## 4-facing (idle_00) — 2026-11-10 ~8:40 PM ET
SE = existing AD soft-PASS (untouched, md5 unchanged). NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with the existing SE raw as likeness reference (`image_urls` = uploaded `fal_raw/{slug}_se_idle_00_raw.png`) so NE keeps the SE design. Then `fal-ai/birefnet/v2` (CDN gen `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE, NW←NE via the smoke23 `mirror()` (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). Script: `scripts/build_smoke24_idle_4facing.py` (smoke23 clone). Config: `scripts/smoke24_common.py`. All NE first take; no rejects / no v1 files. The Erinyes first take already showed both feathered wings fully inside the frame, so it was not regenerated.

NE baselines already matched their SE after post (all four y45, row 46 empty). No vertical shift was required, so no clip case. No isolated 1px fringe specks (no opaque 8-neighbours) on any NE; nothing was cleaned.

| slug | NE gen request_id | NE birefnet request_id | post |
|---|---|---|---|
| erinyes | 01a1c841-59e3-77b0-b33f-d3ba4a32406e | 01a1c842-0ffc-7392-aac5-b226c0cae63f | PASS |
| hell-hound | 01a1c841-59e6-72a0-bb88-15c79b209249 | 01a1c842-106b-7962-b6a6-de62ee16d479 | PASS |
| lemure | 01a1c841-59c7-7a11-89dc-f0af1d3b5db8 | 01a1c842-0fe3-7530-919a-51bb8a30c727 | PASS |
| night-hag | 01a1c841-5a5b-7961-864e-e22aeff1953e | 01a1c842-102b-7db1-8d51-3f0e182c19b6 | PASS |

### Validation 16/16 PASS (size · pivot α255 · hard alpha · palette · empty edge cols · non-empty)
| slug | se opaque / base | sw | ne | nw | 8-conn comps (se/sw/ne/nw) | khaki% se→ne |
|---|---|---|---|---|---|---|
| erinyes | 255 / y45 | 254 / y45 | 276 / y45 | 276 / y45 | 254+1 / 253+1 / 275+1 / 275+1 | 14.1→16.3 |
| hell-hound | 323 / y45 | 320 / y45 | 369 / y45 | 368 / y45 | 322+1 / 319+1 / 366+2+1 / 365+2+1 | 6.5→4.9 |
| lemure | 535 / y45 | 534 / y45 | 467 / y45 | 465 / y45 | 534+1 / 533+1 / 466+1 / 464+1 | 33.3→33.4 |
| night-hag | 526 / y45 | 526 / y45 | 563 / y45 | 563 / y45 | 525+1 / 525+1 / 562+1 / 562+1 | 1.1→0.9 |

The +1 in every facing is the detached pivot pixel (16,47) (contract). All four plant on y45 with a row-46 gap on all four facings. Tops: erinyes y17 all facings; hell-hound y20 SE/SW, y19 NE/NW; lemure y14 SE/SW, y17 NE/NW; night-hag y12 SE/SW, y9 NE/NW.
Leftover non-1px components left as-gen:
- Hell hound NE/NW: a 2px #828282 island at (7, 23–24) NE / (25, 23–24) NW — a detached back-flame / smoke puff that lost its 1px bridge at 32px. Connected in the raw. Same class as the SE snout-smoke tell, not an isolated 1px fringe. Left as-gen. SE/SW have no island.

No cast shadow or plinth in any NE raw. No wings crept into the non-winged three. The Erinyes keeps both feathered wings, fully inside the bin, on NE and NW.

md5 guard (`scripts/smoke24_md5_guard.py check-4f` vs `.smoke24_md5_pre_4f.json`, 13684 frames before this pass): 0 changed + 12 added (smoke24 ne/sw/nw idle_00) + 0 removed. Smoke 1–23 frames and the four #24 SE idle_00 files: 0 modified/removed. Concurrent #23 anim added during this pass: 0. clips.json untouched.

### Soft notes (4f)
- Erinyes NE: the raw is a true rear / back-three-quarter — back of the armor, helmet and horns from behind, both feathered wings spread, longsword on the right. At 32px the wide V of wings still dominates, so the silhouette is closer to SE than the raw suggests; the turn reads mainly as a slightly more centered body under the wing V and a longer sword extension to the right. Both wings are present and fully inside the bin (first take; no regen). Likeness holds: armored winged warrior, helmet, sword, khaki on the wings (14.1→16.3, a slight increase, not a redesign). Opaque 255→276 (+21, +8%) — a little heavier, still the smallest of the four, same width-limited short stance (top stays y17). Horns on the helm read a bit more clearly from behind than the SE's small horn bump. No shadow or plinth, no cropped wings.
- Hell Hound NE: a true back-three-quarter and the strongest turn of the batch. Hindquarters and tail curl on the left, back-flame ridge, head in profile toward the upper-right with snout smoke. Likeness holds: muscular four-legged fiend dog, darker than the blink dog, grey smoke/flame tells. Opaque 323→369 (+46, +14%) — a real bulk increase; the NE stance is a longer stride and 1px taller (top y20→y19), less of a tight crouch than SE. Khaki 6.5→4.9 (slight drop). The 2px #828282 island noted above is left as-gen. No wings. No shadow or plinth.
- Lemure NE: true back-three-quarter / rear view. We see the dripping back of the sludge, the vaguely human head from behind/side, puddle base spreading toward the lower-right. Likeness holds: shapeless melting lump, no legs, no wings, no weapons. Warmth is unchanged (khaki 33.3→33.4). Thinner: 535→467 (−68, −13%), top y14→y17 (3px shorter). Still the melting puddle, still distinct. Not a redesign. No shadow or plinth.
- Night Hag NE: true back-three-quarter. Back of the shaggy robe, wild hair, both ram-curl horns readable from behind, claw reaching toward the upper-right. Likeness holds: gaunt fiendish crone, dark almost-khaki-free palette (1.1→0.9, no khaki drift). Slightly taller and heavier: 526→563 (+37, +7%), top y12→y9 (3px taller). Horns remain the 32px tell and are clearer on NE than SE. No wings. No shadow or plinth. Still distinct from the #22 green hag and the #23 sea hag.
- Distinctness at 32px: all four stay separable on every facing (winged sword-wielder / fire-snouted tailed hound / khaki sludge puddle / dark horned hunched crone).
- SW/NW mirrors lose 0–3 px from the col-31→clear shift (erinyes SE→SW loses 1, NE→NW loses none; hell-hound SE→SW loses 3, NE→NW loses 1; lemure SE→SW loses 1, NE→NW loses 2; night-hag loses none).

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_idle_00.png
- fal_raw/{slug}_ne_idle_00_{raw,cutout}.png
- previews/mon_{slug}_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw) · previews/mon_smoke24_4facing_idle_00_fal_x8.png (rows erinyes|hell-hound|lemure|night-hag, 1036×1560)

## Anim (2026-11-10) — built, awaiting AD audit
Medium 32×48, pivot (16,47); source = Art 4f soft-PASS fal idle_00 (never written). Template = modest smoke #18–#23 (same bob/sway/scale amplitudes, no species flourishes — no wing flap, no fire breath, no melt drip, no claw rake), with the per-creature exceptions below.
Regenerate (from repo root): `python3 scripts/build_smoke24_idle_clips.py && python3 scripts/build_smoke24_walk_clips.py && python3 scripts/build_smoke24_remaining_clips.py && python3 scripts/smoke24_md5_guard.py check` (config: `scripts/smoke24_common.py`; locked idle_00 in `.smoke24_idle00_locked.json`; baseline taken first with `smoke24_md5_guard.py baseline`). `smoke24_md5_guard.py check-se` and `check-4f` still compare frames to the Art snapshots.

split_y se/sw/ne/nw: erinyes 33/33/33/33 · hell-hound 37/37/41/41 · lemure 38/38/40/40 · night-hag 43/43/44/44. All grounded; never floater split=H-1=47. Baseline respected: all four plant on y45 with the contract row-46 pivot gap on every facing.
- Erinyes: standard detect, treated as grounded (standing, wings spread), not a floater. Short in the bin (top y17) because of the uncropped wing span; no fill was forced.
- Hell Hound: standard detect; quadruped walk = mastiff/hyena modest template (legs planted below split, torso bob/sway, 1px airborne on f02), tagged `"quadruped": true`.
- Lemure: legless puddle split (top of the lowest wide bulge, not the 50%-from-foot rule, which would have planted only the last 1–2 taper rows). Puddle stays on y45; body bobs above it. Walk is a minimal slide + body bob with lift capped at 1 (no airborne hop, no leg swing), `bridge=body_only`, tagged `"legless": true`. Puddle rows were verified unchanged on every walk frame. Fall/death still use the standard airborne/flatten scopes.
- Night Hag: standard detect, standard modest template.
Floaters: none. Bridge: lemure `body_only`; the other three `full_row`. Subtle: none (no hop fallback). NE walk distinct frames: erinyes 5, hell-hound 4, lemure 4, night-hag 5.

Hell-hound NE/NW 2px #828282 smoke puff (NE at (7,23–24), NW at (25,23–24)) is intentional Art (a detached back-flame that lost its 1px bridge at 32px; connected in the raw). It sits above split, so it moves with the body (walk bob/hop/squash shift it 1–2 px) and was not deleted. Idle component scan logged it as `COMP-EXEMPT` (not a stray-pixel fault); validation does not fail on it. SE/SW have no island.

Counts: 608 frames / 144 clips tagged `"smoke": 24`: idle 64 (16), walk 96 (16), react 48, attack 96, cast 64, stun 64, death 80, fall 48, fall_impact 48 (16 each). Validation ALL PASS ×3 (exit 0).
Truncations: `truncations.smoke24` 203 notes (death 64, attack 38, fall_impact 31, cast 25, react 17, stun 14, fall 14; no lift truncations; no lift→0; no edge-col fallback), `truncations.smoke24_walk` 6 (erinyes se walk f02/f03 dx -1→+1 from the wing span on col 1; hell-hound se+ne f02/f03 dx -1→0). Lemure and night-hag kept the 1px walk sway on all facings.
md5 guard: 16984 entries (smoke 1–23 frames including #23 anim outputs + 16 locked smoke24 Art idle_00 + clips.json non-smoke24 entries/truncations); drift 0. Locked #24 idle_00 also verified against `.smoke24_idle00_locked.json`.
Contacts: 36 `previews/mon_{slug}_medium_{clip}_cycle_4facing_x8.png` (slug ∈ erinyes, hell-hound, lemure, night-hag; clip ∈ idle, walk, react, attack, cast, stun, death, fall, fall_impact) plus per-creature audit sheets `previews/mon_{slug}_medium_anim_all_clips_4facing_x8.png` and combined `previews/mon_smoke24_anim_all_clips_4facing_x8.png`. x4 downscales of the per-creature sheets: `/workspace/smoke24_{slug}_x4.png`.

Art soft notes carried into Anim (not compensated): Erinyes body stays small under the wing V (top y17, width-limited); NE reads mainly as a more centered body under the same wing silhouette with a longer sword. Hell Hound NE is a longer, slightly taller stride (+14% opaque) than the SE crouch; the snout-smoke / back-flame tells and the 2px puff are left as-gen. Lemure NE is thinner and 3px shorter (535→467, top y14→y17) but still the khaki melting puddle. Night Hag NE is a true back-three-quarter with clearer ram-curl horns (taller/heavier, top y12→y9). No compensating motion invented (no wing flap, no fire breath, no melt drip).
Caveats: generic torso motion only, plus the legless walk cap above. Feet/puddle stay planted on grounded clips. Erinyes wing span causes expected one-sided or sign-flipped dx (se walk -1→+1; several remaining dx flips) rather than edge-col clears. Hell-hound plates are tight, so attacks mostly truncate toward dx 0 (weak lunges). Fall_impact sw often 1.2/1.08→1.02. Lemure fall still lifts the whole blob (airborne); only walk/idle keep the puddle planted. The puff is a 2px island on hell-hound NE/NW only — allowed exemption.

**AD sign-off (records, 2026-11-24 CT): Art+Anim PASS (soft notes as recorded above). This batch was passed by AD in chat and reported Done to Producer at the time; this line only writes that verdict into the doc. Supersedes any "awaiting AD audit" status above.**

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449):** This note is the production record for smoke #24. The art PR ships 26 medium creatures that have content spec rows on main, not the original 40. From this smoke, the PR includes: `hell-hound`. Held back (kept local, not in the PR): `azer`, `barbed-devil`, `bearded-devil`, `chain-devil`, `erinyes`, `lemure`, `night-hag`, `rakshasa`, `doppelganger`, `mimic`, `swarm-of-beetles`, `swarm-of-centipedes`, `swarm-of-spiders`, `swarm-of-wasps`.
