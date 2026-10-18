-- Engine script API, level 2 (docs/proposals/VERSION_BUMP_BATCH.md section 8.5, rows 16a/16b).
-- Level-N definitions are a SUPERSET of levels 1..N-1: api_definitions(2) is level 1 + "\n" + this
-- file, and a level-2 pack is type-checked against that concatenation, so it can call every level-1 API.
--
-- NOT FROZEN yet: unlike engine.level1.d.luau this file is intentionally absent from FROZEN.sha256
-- until row 16b ships the level-2 API content (reaction / resource / modifier queries). Row 16a only
-- adds the marker type below so the superset behaviour is testable.
--
-- Level-2 queries are added as free functions or NEW types, never as members of SimContext
-- (level 1's SimContext is frozen; extending it would change the level-1 surface).

declare extern type Level2Marker with
    level: number
end
