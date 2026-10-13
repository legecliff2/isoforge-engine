-- Engine script API, level 1 (ARCHITECTURE.md §3.3).
-- FROZEN: this file is byte-frozen once released (script_host/api/FROZEN.sha256, checked by
-- the api_freeze ctest). Never edit it; add engine.level2.d.luau for new symbols.
--
-- Determinism (§3.3, §4.5): randomness only through ctx.rng; numbers are integers; lists
-- returned by the engine are in stable_id order. Handles are valid for the current call only.

declare extern type Rng with
    -- [0, bound), unbiased. 1 <= bound <= 2^32-1.
    function below(self, bound: number): number
    -- Inclusive range; lo <= hi, both within int32.
    function range(self, lo: number, hi: number): number
    -- True with probability num/den; 0 <= num <= den, den >= 1.
    function chance(self, num: number, den: number): boolean
    -- 1..sides.
    function d(self, sides: number): number
end

declare extern type EntityHandle with
    -- Persistent stable_id (< 2^53).
    function id(self): number
    function alive(self): boolean
    -- Component reads; nil when the entity lacks the component.
    function hp(self): number?
    function hp_max(self): number?
    function team(self): number?
    function tile(self): { x: number, y: number }?
end

declare extern type SimContext with
    rng: Rng
    -- All live entities, stable_id order.
    function entities(self): { EntityHandle }
    -- Live entities with a Team different from e's and hp > 0, stable_id order.
    function enemies_of(self, e: EntityHandle): { EntityHandle }
end

-- Appends a line to the host's script log.
declare function log(message: string): ()
