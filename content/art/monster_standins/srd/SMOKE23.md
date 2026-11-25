# Smoke #23 — Mediums SE idle_00 (fal)

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449) and the Group D follow-up:** This note is the production record for smoke #23. The tree now holds 37 medium creatures in Long Dusk Mapping B (26 from #449/#458, plus 11 Group D / couatl kits). From this smoke, the tree includes: `sea-hag`, `barbed-devil`, `bearded-devil`. Held back (kept local): `chain-devil`, `rakshasa`, `doppelganger`, `mimic`.

**Status: SE soft-PASS (AD) · 4-facing soft-PASS (AD) · Art Done · Anim built, awaiting AD audit**

Roster: Sea Hag | Barbed Devil | Bearded Devil | Chain Devil — all **medium** 32×48, pivot (16,47) opaque.

## Pipeline / contract
`google/nano-banana-2.1` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) → `fal-ai/birefnet/v2` (CDN `image_url`, not $fal_ref) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium` (medium LAST). post: 4/4 PASS.

## IDs
| slug | gen request_id | birefnet request_id | opaque | post |
|---|---|---|---|---|
| sea-hag | 01a1c834-e424-79f2-a9d9-4eb5ebdff091 | 01a1c835-6be7-7723-a281-4e428492ba54 | 423 | PASS |
| barbed-devil | 01a1c834-e436-70c0-b25b-564b64cebc89 | 01a1c835-6bc3-76b3-ad0a-f4b0304772ea | 348 → 347 after speck clean | PASS |
| bearded-devil | 01a1c834-e446-7613-bd45-59bef5ba34ae | 01a1c835-6c10-70f1-b36c-cc37f802b86f | 228 → 227 after speck clean | PASS |
| chain-devil | 01a1c834-e449-7dd2-9aaa-2f5eda5724bc | 01a1c835-6be8-73b2-b778-bdc20b069480 | 419 → 418 after speck clean | PASS |

All first take; no rejects / no v1 files.

## Validation (4/4 PASS)
32×48 · pivot (16,47) α255 · alpha {0,255} · palette ⊆ {#1E2228 #5C6370 #8C7A62 #828282} · cols 0/31 empty.
Comps (8-conn), after speck clean: sea-hag 422+1 pivot · barbed-devil 346+1 pivot · bearded-devil 221+5+1 pivot · chain-devil 411+4+2+1 pivot.
Feet: all four plant on y45 with a row-46 gap (same floater as azer / wolf / blink-dog / green-hag / most prior mediums). Bearded devil is short in the bin (top at y20) because the wide tail+glaive pose is width-limited by `fal_post_to_bin`'s fit — same class as blink-dog (top y20). Not a post-script miss.
No cast shadow, plinth, or wings in any raw. Smoke 1–21 frames md5: 0 modified/removed. Smoke #22 idle_00 (all 16): unchanged. Guard: `.smoke23_md5_pre_se.json` (12480, post-#22 4f idle, pre-#23 and pre-#22 anim) → 0 changed / 0 removed / 4 smoke23 SE additions. 592 concurrent smoke22 anim clips (148×4, Animator bake during this pass, not written here) also appeared vs that snapshot; clips.json was not touched by this pass.

## Speck clean (no fal call)
Isolated 1px fringe specks with no opaque 8-neighbours, all #1E2228, set to alpha 0. Confirmed isolated first. No other pixel changed. Baselines stayed y45; pivot (16,47) unchanged.
- barbed-devil SE (29,28) — opaque 348→347. Comps 346+1 pivot (the 1px fringe component is gone).
- bearded-devil SE (6,31) — opaque 228→227. Comps 221+5+1 pivot.
- chain-devil SE (11,15) — opaque 419→418. Comps 411+4+2+1 pivot.
Sea hag had none.

Leftover non-1px components left as-gen for AD:
- Bearded devil: 5px tail-tip island at (2–4, 28–29) (#1E2228) — the spade of the tail lost its 1px bridge at 32px.
- Chain devil: 4px top loop at (13–15, 13–14) and 2px right-hand chain at (28, 34–35) — dangling chain fragments, same class as intentional loose chains.

## Soft notes (SE)
- Sea Hag: SE ✓ (hunched, head and reaching claw lower-right). Hideous crone, dripping seaweed-like hair, fishy scaly skin, webbed clawed hands, tattered/netted rags. At 32px the face is lost; the tells are the hunched dripping mass + reaching claw + a fully dark palette (0% khaki). Distinct from the #22 green hag: that one is bulkier (558 opaque, top y12), has khaki shawl/hair accents (12.5%), and a more solid triangular hunch. This sea hag is darker, drippier, more ragged at the hem and hair, and slightly lighter in mass (423, top y14). Wetter / scalier / more fishlike reads at 32px via the irregular dripping silhouette and the lack of khaki. No invented bits.
- Barbed Devil: SE ✓ (lean stride, head right). Humanoid fiend bristling with spikes and barbs, clawed hands, curled tail loop on the left, khaki body midtones (16.4%). No wings. The jagged outline + tail curl is the 32px tell — clearly the spiky lean fiend of the three. Isolated speck at (29,28) cleaned.
- Bearded Devil: SE ✓ (head right, glaive pointing lower-right). Horns, khaki snake-beard mass on the face (10.6% khaki), glaive (the strongest 32px tell), spade tail on the left. No wings. Horns are fairly visible rather than tiny, which helps at this size. Noticeably smaller in the bin (top y20, 227 opaque) because the wide tail+glaive pose is width-limited — still readable, but the smallest of the four. 5px detached tail-tip component left as-gen (see above). Isolated speck at (6,31) cleaned.
- Chain Devil: SE ✓ (striding right). Dark almost-khaki-free mass (0% khaki) wrapped in irregular chain-loop tendrils; loose chains held to the right and dangling left. No wings, no tail. The loopy silhouette is the tell. Leftover 4px top loop and 2px right-hand chain left as-gen (dangling chains, not isolated 1px). Isolated speck at (11,15) cleaned.
- Distinctness at 32px: all four separable (dripping hunched crone / spiky lean tailed fiend / smaller horned glaive-wielder / dark chain-looped mass). The three devils read apart: spikes + curl tail vs glaive + horns + beard vs chain loops and no tail. No shadow, plinth, or wing creep in any raw.

## Paths
- frames/{slug}/medium/se/mon_{slug}_medium_se_idle_00.png
- fal_raw/{slug}_se_idle_00_{raw,cutout}.png
- previews/mon_smoke23_se_idle_00_fal_x8.png (order sea-hag|barbed-devil|bearded-devil|chain-devil; #C2B08E; 34px cells; ×8 nearest)


## 2026-11-10 ~8:31 PM ET — AD SE soft-PASS
- AD soft-PASS on SE (Sea Hag, Barbed Devil, Bearded Devil, Chain Devil). Softs accepted: keep the detached bearded-devil tail tip and chain-devil chain bits, they are part of the art; speck cleanup is good; the short bearded devil is accepted; the sea hag's face is lost but her silhouette carries her and she reads differently from the green hag.
- Current baseline kept for all four: y45 with a row-46 gap. 4f kicked with each NE on y45 to match its SE.

## 4-facing (idle_00) — 2026-11-10 ~8:31 PM ET
SE = existing AD soft-PASS (untouched, md5 unchanged). NE = fal `google/nano-banana-2.1/edit` (1K, 1:1, png, thinking high, safety 4, limit_generations, n=1) with the existing SE raw as likeness reference (`image_urls` = uploaded `fal_raw/{slug}_se_idle_00_raw.png`) so NE keeps the SE design. Then `fal-ai/birefnet/v2` (CDN gen `image_url`, not $fal_ref; `output_format` png, `operating_resolution` 1024x1024) → `python3 scripts/fal_post_to_bin.py <cutout> <out> medium`. SW←SE, NW←NE via the smoke22 `mirror()` (FLIP_LEFT_RIGHT, crop (0,0,31,48) pasted at (1,0), cols 0/31 cleared, pivot restored #1E2228 if cleared). Mirrors ran after the chain-devil NE speck clean below. Script: `scripts/build_smoke23_idle_4facing.py` (smoke22 clone). Config: `scripts/smoke23_common.py`. All NE first take; no rejects / no v1 files.

NE baselines already matched their SE after post (all four y45, row 46 empty). No vertical shift was required, so no clip case.

| slug | NE gen request_id | NE birefnet request_id | post |
|---|---|---|---|
| sea-hag | 01a1c839-d8d8-7920-b4f2-10b515a956d7 | 01a1c83a-9f00-7ab1-96c1-00a5a8560212 | PASS |
| barbed-devil | 01a1c839-d89a-7392-bf6a-d8394c2c1172 | 01a1c83a-9eff-7a40-9f9d-6756371702b9 | PASS |
| bearded-devil | 01a1c839-d923-7251-a136-004a10a52f7a | 01a1c83a-9efe-76e1-887e-48f2d7701276 | PASS |
| chain-devil | 01a1c839-d924-7983-86ca-1e04f279037e | 01a1c83a-9e36-7833-a4aa-36ba0bdcfe82 | PASS |

### Chain Devil NE speck clean (no fal call)
Isolated 1px fringe speck at (11,15), #1E2228, no opaque 8-neighbours — same downsample-fringe class and the same cell as the SE speck already cleaned. Set to alpha 0. Confirmed isolated first. No other pixel changed. Baseline stayed y45; pivot (16,47) unchanged. Opaque 413→412. Mirrors ran after this clean, so NW does not carry the speck.

### Validation 16/16 PASS (size · pivot α255 · hard alpha · palette · empty edge cols · non-empty)
| slug | se opaque / base | sw | ne | nw | 8-conn comps (se/sw/ne/nw) | khaki% se→ne |
|---|---|---|---|---|---|---|
| sea-hag | 423 / y45 | 423 / y45 | 409 / y45 | 409 / y45 | 422+1 / 422+1 / 408+1 / 408+1 | 0.0→0.0 |
| barbed-devil | 347 / y45 | 345 / y45 | 352 / y45 | 350 / y45 | 346+1 / 337+7+1 / 351+1 / 342+7+1 | 16.4→8.0 |
| bearded-devil | 227 / y45 | 227 / y45 | 213 / y45 | 212 / y45 | 221+5+1 / 221+5+1 / 212+1 / 211+1 | 10.6→2.3 |
| chain-devil | 418 / y45 | 416 / y45 | 412 / y45 | 411 / y45 | 411+4+2+1 / 409+4+2+1 / 405+4+2+1 / 404+4+2+1 | 0.0→0.0 |

The +1 in every facing is the detached pivot pixel (16,47) (contract). All four plant on y45 with a row-46 gap on all four facings. Tops: sea-hag y14 all facings; barbed-devil y10 SE/SW, y12 NE/NW; bearded-devil y20 SE/SW, y22 NE/NW (still the short one); chain-devil y13 all facings.
Leftover non-1px components left as-gen (same class as the SE soft-PASS):
- Bearded devil SE/SW: 5px tail-tip island at (2–4, 28–29) SE / (28–30, 28–29) SW. The NE/NW back view connects the tail into the body (no island).
- Chain devil, all four facings: 4px top loop (SE/NE at (13–15, 13–14); SW/NW at (17–19, 13–14)) and 2px right/left-hand chain (SE/NE at (28, 34–35); SW/NW at (4, 34–35)).
- Barbed devil SW/NW: a 7px tail-curl island at (26–30, 31–33) SW / (26–30, 32–34) NW. The SE/NE tail curl is connected; the smoke22 `mirror()` col-1 clear drops the 1px left-edge bridge and leaves the loop tip detached. 2 px lost SE→SW / NE→NW. Left as the standard mirror shift, not redrawn.
No cast shadow, plinth, or wings in any NE raw.

md5 guard (`scripts/smoke23_md5_guard.py check-4f` vs `.smoke23_md5_pre_4f.json`, 13076 frames before this pass): 0 changed + 12 added (smoke23 ne/sw/nw idle_00) + 0 removed. Smoke 1–22 frames and the four #23 SE idle_00 files: 0 modified/removed. Concurrent #22 anim added during this pass: 0. clips.json untouched.

### Soft notes (4f)
- Sea Hag NE: still a right-facing hunched profile with the reaching claw on the right — a modest camera shift, not a true back-three-quarter away up-right. The dripping hair mass and tattered hem still carry her at 32px; the face stays lost, as on SE. Likeness holds: same hunched crone, seaweed hair, webbed claw, fully dark palette (khaki 0.0→0.0). Slightly thinner (423→409, −14 px, ~3%), top unchanged at y14. No redesign. Same class as green-hag / gargoyle NE in #21–#22.
- Barbed Devil NE: still a right-facing lean stride with the chest/side visible and the curled tail on the left — also a modest turn, not a true back-three-quarter. Likeness holds: spiked outline, clawed hands, tail curl, no wings. Opaque almost unchanged (347→352) — no bulk change. Top y10→y12 (2 px shorter). Khaki drops 16.4→8.0 (front midtones recede a little). Biggest facing-consistency risk of the three devils, same class as the hag.
- Bearded Devil NE: best turn of the batch. True back-three-quarter / rear view: we see the back of the armor, horns from behind, head in profile toward the upper-right, glaive still on the right, spade tail on the left. The snake-beard khaki, already a face-mass on SE, nearly vanishes from behind (khaki 10.6→2.3). Slightly smaller 227→213 (−14 px, ~6%), top y20→y22; still the smallest of the four, same width-limited short stance AD already accepted on SE. The 5px SE tail-tip island is connected into the body on this facing. No wings. Not a redesign. Horns + glaive remain the 32px tells.
- Chain Devil NE: modest turn. Still a right-striding dark mass with the loopy chain silhouette; the front of the torso stays visible, so this is not a strong back-three-quarter. Likeness holds: irregular chain-loop tendrils, dangling bits, no wings, no tail, khaki 0.0→0.0. Opaque almost unchanged (418→412 after the (11,15) speck clean). Top stays y13. The 4px top loop and 2px hand-chain persist on NE (and their mirrors).
- Distinctness at 32px: the three devils still read apart on every facing (spikes + curl tail vs smaller horned glaive-wielder vs dark chain-looped mass with no tail). Sea hag stays the dripping hunched crone, distinct from all three.
- SW/NW mirrors lose 0–2 px from the col-31→clear shift (sea-hag loses none; barbed loses 2; bearded NE→NW loses 1; chain loses 2 SE→SW and 1 NE→NW). See the barbed tail-curl island note above.

### Paths
- frames/{slug}/medium/{se,sw,ne,nw}/mon_{slug}_medium_{f}_idle_00.png
- fal_raw/{slug}_ne_idle_00_{raw,cutout}.png
- previews/mon_{slug}_medium_idle_00_4facing_fal_x8.png (se|sw|ne|nw) · previews/mon_smoke23_4facing_idle_00_fal_x8.png (rows sea-hag|barbed-devil|bearded-devil|chain-devil, 1036×1560)

## Anim (2026-11-10) — built, awaiting AD audit
Medium 32×48, pivot (16,47); source = Art 4f soft-PASS fal idle_00 after the SE speck cleans and the chain-devil NE (11,15) speck clean (never written). Template = modest smoke #18–#22 (same bob/sway/scale amplitudes, no species flourishes — no seaweed drip, no barb twitch, no glaive sweep, no chain lash).
Regenerate (from repo root): `python3 scripts/build_smoke23_idle_clips.py && python3 scripts/build_smoke23_walk_clips.py && python3 scripts/build_smoke23_remaining_clips.py && python3 scripts/smoke23_md5_guard.py check` (config: `scripts/smoke23_common.py`; locked idle_00 in `.smoke23_idle00_locked.json`; baseline taken first with `smoke23_md5_guard.py baseline`). `smoke23_md5_guard.py check-se` and `check-4f` still compare frames to the Art snapshots.

split_y se/sw/ne/nw: sea-hag 41/41/41/41 · barbed-devil 38/38/39/39 · bearded-devil 41/41/41/41 · chain-devil 39/39/39/39 (all grounded; per-facing detect, never floater split=H-1=47). Baseline respected: all four plant on y45 with the contract row-46 pivot gap on every facing.
Floaters: none. Bridge: all `full_row` (no BODY_BRIDGE). Subtle: none (NE walk distinct frames: sea-hag 5, barbed-devil 4, bearded-devil 4, chain-devil 4; no hop fallback).

Counts: 608 frames / 144 clips tagged `"smoke": 23`: idle 64 (16), walk 96 (16), react 48, attack 96, cast 64, stun 64, death 80, fall 48, fall_impact 48 (16 each). Validation ALL PASS ×3 (exit 0).
Truncations: `truncations.smoke23` 231 notes (death 64, attack 46, cast 30, fall_impact 30, react 23, stun 19, fall 19; no lift truncations; no lift→0; no edge-col fallback), `truncations.smoke23_walk` 10 (walk sway dx -1→0 on f02/f03: barbed-devil se+ne, bearded-devil ne, chain-devil se+ne). Sea-hag kept the 1px walk sway on all facings.
md5 guard: 16230 entries (smoke 1–22 frames including #22 anim outputs + 16 locked smoke23 Art idle_00 + clips.json non-smoke23 entries/truncations); drift 0. Locked #23 idle_00 also verified against `.smoke23_idle00_locked.json`.
Contacts: 36 `previews/mon_{slug}_medium_{clip}_cycle_4facing_x8.png` (slug ∈ sea-hag, barbed-devil, bearded-devil, chain-devil; clip ∈ idle, walk, react, attack, cast, stun, death, fall, fall_impact) plus per-creature audit sheets `previews/mon_{slug}_medium_anim_all_clips_4facing_x8.png` and combined `previews/mon_smoke23_anim_all_clips_4facing_x8.png`. x4 downscales of the per-creature sheets: `/workspace/smoke23_{slug}_x4.png`.

Art soft notes carried into Anim (not compensated): Sea Hag NE stays a right-facing hunched profile (face lost; dripping silhouette carries her), same class as green-hag / gargoyle NE. Barbed Devil NE is also a modest right-facing stride, not a true back-three-quarter (khaki 16.4→8.0; biggest devil facing-consistency risk). Bearded Devil NE is the best turn (back-three-quarter); the snake-beard nearly vanishes from behind (khaki 10.6→2.3); the 5px SE/SW tail-tip island and the short width-limited stance (top y20/y22) are left as-gen. Chain Devil NE is a modest turn; the 4px top loop and 2px hand-chain persist on all facings, as AD accepted on SE. Barbed Devil SW/NW 7px tail-curl island from the mirror col-1 clear is also left as-gen. No compensating motion invented.
Caveats: generic torso motion only. Feet stay planted (torso bobs above detected split). Barbed / chain / bearded-NE plates are tight on the edge cols, so attacks mostly truncate toward dx 0 (weak lunges) and walk sway is one-sided on the listed facings. Sea-hag is a bit narrower (walk sway survived; death sw only partly capped). Fall_impact sw 1.2/1.08 often →1.02. Bearded devil stays the shortest of the four; no fill was forced. Detached tail-tip / chain-loop fragments ride along with the torso or stay put with the legs, depending on which side of split_y they sit on — they were not reattached.

**AD sign-off (records, 2026-11-24 CT): Art+Anim PASS (soft notes as recorded above). This batch was passed by AD in chat and reported Done to Producer at the time; this line only writes that verdict into the doc. Supersedes any "awaiting AD audit" status above.**

**PR [#449](https://github.com/legecliff2/isoforge-engine/pull/449) and the Group D follow-up:** This note is the production record for smoke #23. The tree now holds 37 medium creatures in Long Dusk Mapping B (26 from #449/#458, plus 11 Group D / couatl kits). From this smoke, the tree includes: `sea-hag`, `barbed-devil`, `bearded-devil`. Held back (kept local): `chain-devil`, `rakshasa`, `doppelganger`, `mimic`.
