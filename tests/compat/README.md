# Content compatibility corpus

Frozen packs per released feature level (ARCHITECTURE.md §3.2, §7 M1 compat test).
Files here are **never edited**: a pack that validated when frozen must keep validating
(via migrations) on every later engine. `tools/validate/validate_packs.py` covers
`tests/compat/fl*/*/` on every run.

- `fl1/sample.vertical_slice/` — feature level 1, frozen from `content/packs/sample.vertical_slice` v1.0.0.
