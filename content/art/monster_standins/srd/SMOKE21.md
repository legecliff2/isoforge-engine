# Smoke #21 — Mediums SE idle_00 (fal)

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449) and the Group D follow-up:** This note is the production record for smoke #21. The tree now holds 37 medium creatures in Long Dusk Mapping B (26 from #449/#458, plus 11 Group D / couatl kits). From this smoke, the tree includes: `gargoyle`, `invisible-stalker`, `xorn`, `azer`. Held back (kept local): `chain-devil`, `rakshasa`, `doppelganger`, `mimic`.

**Status: SE soft-PASS (AD) · 4-facing soft-PASS (AD) · Art Done · Anim built (all four), awaiting AD audit**

Roster: Azer | Gargoyle | Invisible Stalker | Xorn — all **medium** 32×48, pivot (16,47) opaque.

## Pipeline / contract
`google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium` (medium LAST). post: 4/4 PASS.

## IDs
| slug | gen request_id | birefnet request_id | opaque | post |
|---|---|---|---|---|
| azer | 01a18638-2c86-7c22-acbf-e4f2e144e330 | 01a18638-ba9e-75d1-8bf2-16609f57f007 | 651 | PASS |
| gargoyle | 01a18638-2c86-7313-867f-9dffdc75d370 | 01a18638-ba9e-7893-9c6b-a9eae523a7df | 670 | PASS |
| invisible-stalker | 01a18638-2ccc-7d00-a70d-f53d34972c41 | 01a18638-ba9e-7442-ae5b-b4790e3ed2eb | 577 | PASS |
| xorn | 01a18638-2cc7-7540-bcd4-330600d3e833 | 01a18638-ba9e-7d92-a701-19a62b006918 | 445 | PASS |

All first take; no rejects / no v1 files.

## Validation (4/4 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty.
Comps (8-conn): azer 650+1 pivot · gargoyle 669+1 pivot · invisible-stalker 571 + pivot + 2+1+1+1 wisp specks · xorn 444+1 pivot.
No cast shadow in any raw. Smoke 1–20 frames md5: 0 modified/removed (11856 before; only 4 smoke21 SE additions).

## Soft notes
- Azer: SE-ish (near-frontal, slight right turn). Stocky armored smith with hammer, khaki apron (21% khaki). Fire-crest tell present: spiky flame hair/beard at top, ~3–4px of ragged khaki/grey spikes at 32px — readable but small; lever = enlarge crest.
- Gargoyle: SE ✓ (head right). Crouched on a plinth with horns, half-spread bat wings, curled long tail. Distinct wing+tail-loop silhouette. Note: model added a stone pedestal (not asked) — reads as statue; AD call.
- Invisible Stalker: SE ✓ (hooded head lower-right, striding). Raw is a swirl/whorl-textured hooded humanoid of wind with wispy trailing legs — not blank. At 32px it reads as a solid dark-grey hooded striding mass with ragged wisp edges + 5 detached 1–2px wisps (intentional mist, allowed by brief). Risk: at 32px the "translucent" quality is lost — reads more as a ragged hooded wraith than air; separation from others is the hood + ragged edges. Lever on REPASS: lighter (#828282-dominant) value and fewer dark fills to sell "translucent".
- Xorn: radial rocky body, mouth on top, multiple splayed limbs — clearly non-biped. Shortest sprite (top at y18), wide squat. Facing is essentially frontal/radial (inherent to xorn); mouth ring on top barely reads at 32px.
- Distinctness: all four separable by silhouette (tall stocky / winged+tail / hooded ragged / squat wide).

## Paths
- frames/{slug}/medium/se/mon_{slug}_medium_se_idle_00.png
- fal_raw/{slug}_se_idle_00_{raw,cutout}.png
- previews/mon_smoke21_se_idle_00_fal_x8.png (order azer|gargoyle|invisible-stalker|xorn; #C2B08E; 34px cells; ×8 nearest)

## 4-facing (idle_00) — 2026-11-10 ~8:05 PM ET
SE = existing AD soft-PASS (untouched, md5 unchanged). NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with the existing SE raw as likeness reference (`image_urls` = uploaded `fal_raw/{slug}_se_idle_00_raw.png`) so NE keeps the SE design. Then `fal-ai/birefnet/v2` on the gen CDN `image_url` (not $fal_ref) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE, NW←NE via h_mirror_keep_pivot (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). Script: `scripts/build_smoke21_idle_4facing.py` (smoke20 clone). Config: `scripts/smoke21_common.py`. All NE first take; no rejects / no v1 files.

| slug | NE gen request_id | NE birefnet request_id | post |
|---|---|---|---|
| azer | 01a1c821-bc89-7b60-b846-17e55d53b3a4 | 01a1c822-99ca-7a41-ac93-995d1aee9ec8 | PASS |
| gargoyle | 01a1c821-bc47-7130-b6bb-47570463a7ee | 01a1c822-99ca-7e42-9cbe-e5ef8fba744d | PASS |
| invisible-stalker | 01a1c821-bc60-7f90-8ff7-bd28d10a9c28 | 01a1c822-99c9-7211-8529-00803ea5b9eb | PASS |
| xorn | 01a1c821-bcdb-7d41-9c58-273d29a3d48d | 01a1c822-99c8-7681-b8b9-31a9a692949b | PASS |

### Validation 16/16 PASS (size · pivot α255 · hard alpha · palette · empty edge cols)
| slug | se | sw | ne | nw | 8-conn comps (se/sw/ne/nw) | khaki% se→ne |
|---|---|---|---|---|---|---|
| azer | 651 | 649 | 620 | 618 | 650+1 / 648+1 / 619+1 / 617+1 | 21→24 |
| gargoyle | 670 | 660 | 600 | 590 | 669+1 / 659+1 / 599+1 / 589+1 | 1.3→0.8 |
| invisible-stalker | 577 | 574 | 566 | 562 | 571+2+4×1 / 568+2+4×1 / 562+4×1 / 558+4×1 | 2.6→2.3 |
| xorn | 445 | 443 | 445 | 443 | 444+1 / 442+1 / 444+1 / 442+1 | 10.8→10.6 |
The +1 in every frame is the detached pivot pixel (16,47) (contract, same as SE). All 16 have a row-46 gap (floaters). Invisible-stalker extra 1–2px = intentional wisp specks (SE had them too), left as-gen. Body is one component in azer / gargoyle / xorn; stalker body + small wisp specks. No cast shadow in any NE raw. Smoke 1–20 frames + smoke21 SE md5: 0 modified/removed; only 12 additions (smoke21 ne/sw/nw) plus previews / scripts / this doc.

### Soft notes (4f)
- Azer NE: true rear / slight-right back view (closer to straight-back than a strong back-three-quarter up-right). Likeness holds: fire crest, helmet, bronze armor, khaki apron, hammer in the right hand. Apron reads as a khaki back-panel at 32px (straps/reverse of the SE front apron). Mild khaki 21→24%; slightly fewer opaque px (651→620), not bulkier. Crest still readable from behind.
- Gargoyle NE: plinth/base successfully omitted (the SE statue pedestal is gone; opaque drop 670→600 is mostly the missing block). Horns, half-spread bat wings, and curled tail retained. Facing risk: NE is still a right-facing three-quarter/profile with the face clearly visible — almost the same pose as SE minus the plinth, not a back-three-quarter facing away up-right. Also sits a couple of pixels higher in the bin (NE feet stop around y44; y45 empty) because there is no base to plant. Biggest 4f facing-consistency risk in the batch.
- Invisible Stalker NE: best facing change of the batch. Hooded head now points upper-right, back-three-quarter, wispy trailing mass lower-left. Same readable silhouette tell as SE (hooded whirl-textured humanoid outline, faintly suggested limbs, ragged wisp edges, not blank). Still a dark-grey solid-looking mass at 32px (translucent quality remains weak, same as SE). A few 1px wisp specks remain (allowed). Not a redesign.
- Xorn NE: essentially the same frontal/radial view as SE (inherent to a mouth-on-top radial body). Opaque identical (445). Slightly darker (#1E2228 227→253). Mouth ring on top still barely reads at 32px. Little to no NE rotation.
- SW/NW mirrors lose ~2–10px from the col-31→clear shift (gargoyle loses the most; normal).

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_idle_00.png
- fal_raw/{slug}_ne_idle_00_{raw,cutout}.png
- previews/mon_{slug}_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw) · previews/mon_smoke21_4facing_idle_00_fal_x8.png (rows azer|gargoyle|invisible-stalker|xorn, 1036×1560)

## 2026-11-10 — AD 4f soft-PASS + Gargoyle NE baseline fix
- AD soft-PASS on 4f. Notes accepted: Azer NE is near a rear view, Stalker is dark, Xorn NE looks like SE (radial body). No gargoyle re-gen.
- Fix: Gargoyle NE body shifted down 1px. The feet's lowest row moved from y44 to y45 to match SE (SE feet y45, row-46 gap, pivot (16,47)). The pivot pixel stayed put, and NW was re-mirrored from NE. No fal call.
- AD asked for 2px. 1px is what lines it up with SE. 2px would put the feet on y46 and close the gap above the pivot, unlike every other frame.
- Contract re-run: 16/16 ALL PASS. Gargoyle NE/NW opaque 600/590 (unchanged, no clipping). Only the gargoyle ne/nw md5 changed.
- Status: Art Done. Anim may start on 4f.

## Anim (2026-11-10) — built (all four), awaiting AD audit
Medium 32×48, pivot (16,47); source = Art 4f soft-PASS fal idle_00 (never written). Template = smoke #20/#19/#18 (same modest bob/sway/scale amplitudes, no species flourishes — no hammer swing, no wing flap / tail lash, no wisp drift, no radial-limb crawl).
Two-pass bake: azer / invisible-stalker / xorn first; gargoyle after AD soft-PASS of the NE/NW y45 foot-line rebaseline. Gargoyle was baked with `SMOKE21_ONLY=gargoyle` so the other three creatures' #21 outputs were not rewritten. Locked gargoyle idle_00 hashes (post-fix) are in `.smoke21_gargoyle_idle00_locked.json`.
Regenerate (from repo root): `python3 scripts/build_smoke21_idle_clips.py && python3 scripts/build_smoke21_walk_clips.py && python3 scripts/build_smoke21_remaining_clips.py && python3 scripts/smoke21_md5_guard.py check` (config: `scripts/smoke21_common.py`; baseline taken first with `smoke21_md5_guard.py baseline`). Partial: `SMOKE21_ONLY=gargoyle` (truncation notes merge by slug).

split_y se/sw/ne/nw: azer 41/41/40/40 · gargoyle 43/43/36/35 · invisible-stalker 40/39/40/40 · xorn 38/38/38/38 (all grounded; per-facing detect, never floater split=H-1=47). The contract row-46 gap (detached pivot pixel) is present on all idle_00 but is not treated as a floater. Gargoyle NE/NW split is lower because the post-fix feet are a 1px stalk (rows 44–45) under a thinner lower body than SE/SW.
Floaters: none. Bridge: all `full_row` (no BODY_BRIDGE). Subtle: none (NE walk 4 distinct frames for all four; no hop fallback).

Counts: 608 frames / 144 clips tagged `"smoke": 21`: idle 64 (16), walk 96 (16), react 48, attack 96, cast 64, stun 64, death 80, fall 48, fall_impact 48 (16 each). Validation ALL PASS ×3 on the first pass and ×3 on the gargoyle pass (exit 0).
Truncations: `truncations.smoke21` 266 notes (death 64, attack 56, react 32, cast 32, fall_impact 32, stun 24, fall 26; lift truncations 2: azer se/sw fall f01 lift 4→2 — top-row headroom; gargoyle none; no lift→0; no edge-col fallback), `truncations.smoke21_walk` 16 (walk sway dx -1→0 on f02/f03, se+ne each slug).
md5 guard (refreshed before the gargoyle bake): 15278 entries (smoke 1–20 frames + azer/stalker/xorn #21 frames and clips + 16 smoke21 Art idle_00 including the locked post-y45 gargoyle keys + clips.json non-gargoyle entries / non-smoke21 truncations); drift 0. Locked gargoyle idle_00 also verified against `.smoke21_gargoyle_idle00_locked.json`. Separate pre-gargoyle-anim hash of 503 files (azer/stalker/xorn outputs + gargoyle idle_00 / fal_raw / Art previews) drift 0.
Contacts: 36 `previews/mon_{slug}_medium_{clip}_cycle_4facing_x8.png` (slug ∈ azer, gargoyle, invisible-stalker, xorn; clip ∈ idle, walk, react, attack, cast, stun, death, fall, fall_impact) plus per-creature audit sheets `previews/mon_{slug}_medium_anim_all_clips_4facing_x8.png` and combined `previews/mon_smoke21_anim_all_clips_4facing_x8.png`. Gargoyle audit sheet: `previews/mon_gargoyle_medium_anim_all_clips_4facing_x8.png`.

Art soft notes carried into Anim (not compensated): Azer SE near-frontal / NE near a rear view; fire-crest still small (~3–4px). Gargoyle SE still reads with the unasked-for plinth; NE/NW omit the plinth and stay a right-facing three-quarter/profile rather than a back-three-quarter (AD-accepted facing risk); feet now on y45 on all four facings. Invisible Stalker stays a dark-grey solid-looking mass (translucent quality weak) with intentional 1–2px wisp specks. Xorn NE looks like SE (radial body); mouth ring still barely reads at 32px. No compensating motion invented.
Caveats: generic torso motion only. Feet stay planted (torso bobs above detected split). Attacks mostly truncate toward dx 0 (weak lunges); fall_impact sw 1.2/1.08→1.02; death sw capped; walk sway one-sided (se/ne). Azer se/sw fall apex lift 4→2 (headroom). Tight plates (edge cols 1 / 30). Gargoyle SE/SW vs NE/NW silhouette differs (plinth present only on the front facings).

**AD sign-off (records, 2026-11-24 CT): Art+Anim PASS (soft notes as recorded above). This batch was passed by AD in chat and reported Done to Producer at the time; this line only writes that verdict into the doc. Supersedes any "awaiting AD audit" status above.**

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449) and the Group D follow-up:** This note is the production record for smoke #21. The tree now holds 37 medium creatures in Long Dusk Mapping B (26 from #449/#458, plus 11 Group D / couatl kits). From this smoke, the tree includes: `gargoyle`, `invisible-stalker`, `xorn`, `azer`. Held back (kept local): `chain-devil`, `rakshasa`, `doppelganger`, `mimic`.
