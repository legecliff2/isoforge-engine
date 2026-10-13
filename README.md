# isoforge-engine
Modular, lightweight 2D isometric RPG engine. C++20, SDL3, Luau, D&D 5e SRD 5.1 rules.
See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

This work includes material from the System Reference Document 5.1 ("SRD 5.1") by Wizards of the Coast LLC, licensed under CC-BY-4.0.

## Content validation
```sh
pip install -r tools/validate/requirements.txt
python3 tools/validate/validate_packs.py          # all packs + tests/compat corpus
ctest --test-dir build -R content_validate        # same, via CTest
```
Schemas live in `schemas/<type>/v<N>.schema.json` (JSON Schema 2020-12, immutable once released).
