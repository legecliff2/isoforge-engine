--!strict
-- Goblin target selection (API level 1; types from engine.level1.d.luau).
-- Referenced by monsters/goblin.json behavior.ai.
--
-- Default: focus the weakest foe (lowest hp, ties broken by stable id).
-- 1 time in 4 (ctx.rng, integer odds), it goes for the nearest foe instead
-- (Chebyshev distance: SRD default, every square costs 5 ft).
-- Deterministic: only ctx.rng, integer math, total-order sorts.

local function distance(a: EntityHandle, b: EntityHandle): number
    local pa, pb = a:tile(), b:tile()
    if pa == nil or pb == nil then
        return 0
    end
    return math.max(math.abs(pa.x - pb.x), math.abs(pa.y - pb.y))
end

local function choose_target(ctx: SimContext, self: EntityHandle): EntityHandle?
    local foes = ctx:enemies_of(self)                      -- stable_id order
    if #foes == 0 then
        return nil
    end
    if ctx.rng:chance(1, 4) then
        table.sort(foes, function(a: EntityHandle, b: EntityHandle): boolean
            local da, db = distance(self, a), distance(self, b)
            if da ~= db then return da < db end
            return a:id() < b:id()
        end)
        log("skirmisher: nearest " .. tostring(foes[1]:id()))
    else
        table.sort(foes, function(a: EntityHandle, b: EntityHandle): boolean
            local ha, hb = a:hp() or 0, b:hp() or 0
            if ha ~= hb then return ha < hb end
            return a:id() < b:id()
        end)
        log("skirmisher: weakest " .. tostring(foes[1]:id()))
    end
    return foes[1]
end

return { choose_target = choose_target }
