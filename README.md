# isoforge-engine
Modular, lightweight 2D isometric RPG engine. C++20, SDL3, Luau, D&D 5e SRD 5.1 rules.
See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

This work includes material from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, licensed under CC-BY-4.0.

## Content validation
CTest runs `content_validate`, which checks every pack in `content/packs/` and the frozen
corpus in `tests/compat/fl*/` against `schemas/<type>/v<N>.schema.json` (JSON Schema 2020-12,
immutable once released), plus cross-checks (CR/XP, saves/skills, chunk sizes, `content_hash`).

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build -R content_validate
```
At configure time CMake creates a private venv at `build/content-venv` (`Scripts/` on Windows,
`bin/` elsewhere) and installs the pinned `tools/validate/requirements.txt` into it; system Python
is never modified. That needs network on the first configure. If it fails, the test is disabled
with a warning. Pass `-DENGINE_CONTENT_VALIDATE=OFF` to skip it entirely.

Without CMake: `python3 -m venv .venv && .venv/bin/pip install -r tools/validate/requirements.txt && .venv/bin/python tools/validate/validate_packs.py`.

After editing a chunk, refresh its hash with `validate_packs.py --rehash` (it never touches `tests/compat/`).
Chunk `content_hash` = xxHash3-64 (seed 0) of canonical JSON: the document without `content_hash`,
keys sorted, separators `,`/`:` with no whitespace, UTF-8 with non-ASCII written raw, as 16 lowercase hex digits.
