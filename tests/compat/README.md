# Content compatibility corpus

Frozen packs per released feature level (ARCHITECTURE.md §3.2, §7 M1 compat test).
Files here are **never edited**: a pack that validated when frozen must keep validating
(via migrations) on every later engine. `tools/validate/validate_packs.py` covers
`tests/compat/fl*/*/` on every run.

- `fl1/sample.vertical_slice/` — feature level 1, frozen from `content/packs/sample.vertical_slice` v1.0.0.

## Freeze check
`tests/compat/MANIFEST.sha256` records the SHA-256 of every file under `fl<N>/`. The `compat_freeze`
ctest (`tools/compat_freeze/compat_freeze.cmake`) fails if any file is added, removed or modified.
Line endings are pinned by `.gitattributes` (`-text`), so hashes match on every OS.
A refreeze, which only happens for a new feature level or a Director-approved regeneration, runs
`cmake -DUPDATE=ON -P tools/compat_freeze/compat_freeze.cmake`, and the manifest diff goes up for review.
