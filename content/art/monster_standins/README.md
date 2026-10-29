# Monster stand-ins (generic)

Repo path: `content/art/monster_standins/`. This is presentation art packed into the repo, not a rules pack. `content_validate` only walks `content/packs/*/manifest.json`, and `clips.json` has no `$schema`, so it must stay out of a pack until Engine Core gives that file one. The vertical slice loads this directory. `visual.standin_bin` (`tiny|small|medium|large`) or `presentation.sprite` of the form `standin.<bin>` picks the canvas. M0 ships this one kit; a `paperdoll` kind is recorded and still draws the procedural pawn.

Regenerate: `python3 build_monster_standins.py` (Pillow). Deterministic; overwrites `frames/`, `strips/`, `previews/`, `clips.json`.

Layout: `frames/{size}/{facing}/mon_*.png`, `strips/{size}/{facing}/*_strip.png`, `previews/*_x4.png` (4× nearest), `clips.json` manifest.

Locked (change only via spec): size bins + pivots, facing names, clip names/frame counts/fps/modes, file naming, construction palette.
Free to change: silhouette shape and per-frame motion in `params()`/`draw()`.
Build validates: alpha 0/255 only, canvas size, pivot pixel opaque on grounded frames, no pixels on outer edge columns.
