# chunk@1 `surprise` fixtures (R-G16)

Valid chunk@1 documents using the reserved top-level `surprise` key (docs/CONTENT_CONVENTIONS.md section 8). Spawns use
`srd51.core` archetypes:

- `chunks/ambush_sides.json` (coord 1,0,0): two characters (default team `party`) and two goblins (default team `monsters`);
  `surprise: {"sides": ["party"]}`.
- `chunks/ambush_spawns.json` (coord 2,0,0): a fighter, two bandits on team `bandits`, a guard on team `party`;
  `surprise: {"spawns": [0, 3]}` (spawn indices).

`content_hash` is the real hash (the key is not hashed: the chunk decoder ignores it). `tools/validate/test_validate_packs.py`
`ChunkSurprise` validates them in a throwaway pack `test.surprise` (depends on `srd51.core`) and runs its table of negative edits.
Not part of any shipped pack and not under `tests/compat` (that corpus is frozen). The future loader can use them as positive fixtures.
