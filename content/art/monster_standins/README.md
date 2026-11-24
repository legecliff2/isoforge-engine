# Monster stand-ins (generic)

Repo path: `content/art/monster_standins/`. These are the stand-in PNGs (frames, strips, previews). Clip ids, frame counts, fps, modes, and aliases are Engine Core's registry at `content/packs/srd51.core/art/clips.json`. This directory is not a rules pack. The vertical slice loads frames from here when `ActorView.visual_kind` is `standin` and `standin_bin` is `tiny|small|medium|large|huge`. `paperdoll` still draws the procedural pawn. `size_short` is stored and not applied.

| bin | canvas | feet pivot |
|---|---|---|
| huge | 72×96 | (36,95) |

Huge is drawn 1:1 on the same 9-clip template. Bins stop at Huge. Gargantuan stays unbinned.

Regenerate: `python3 build_monster_standins.py` (Pillow). Deterministic; overwrites `frames/`, `strips/`, and `previews/`. The script may also write a local `clips.json`; the engine does not read that file.

Layout: `frames/{size}/{facing}/mon_*.png`, `strips/{size}/{facing}/*_strip.png`, `previews/*_x4.png` (4× nearest), `clips.json` manifest.

Per-monster kits, when a later art drop adds them, live at `srd/{slug}/{bin}/` under this directory. `slug` is the last segment of the monster id (`srd51.monster.wolf` → `wolf`). That directory is a kit root: the same `frames/{bin}/{facing}/mon_{bin}_{facing}_{clip}_{ff}.png` names as the shared bins. A missing directory keeps the shared bin kit. No per-monster frames ship in this tree.

Locked (change only via spec): size bins + pivots, facing names, clip names/frame counts/fps/modes, file naming, construction palette.
Free to change: silhouette shape and per-frame motion in `params()`/`draw()`.
Build validates: alpha 0/255 only, canvas size, pivot pixel opaque on grounded frames, no pixels on outer edge columns.
