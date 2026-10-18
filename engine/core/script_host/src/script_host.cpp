#include "script_host/script_host.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include "assets/content_migrations.hpp"
#include "assets/pack_set.hpp"
#include "assets/schema_registry.hpp"

namespace eng::script::embedded {
extern const unsigned char kEngineLevel1Defs[];
extern const std::size_t kEngineLevel1Defs_size;
extern const unsigned char kEngineLevel2Defs[];
extern const std::size_t kEngineLevel2Defs_size;
}

namespace eng::script {

namespace {

constexpr int kTagRng = 1, kTagEntity = 2, kTagCtx = 3;
constexpr double kMaxSafeInt = 9007199254740992.0;   // 2^53

struct EntityUd { uint32_t index, generation; uint64_t epoch; };
struct CtxUd { uint64_t epoch; };
struct RngUd { uint64_t epoch; };

bool is_int(double v) { return v >= -kMaxSafeInt && v <= kMaxSafeInt && double(int64_t(v)) == v; }

} // namespace

struct ScriptHost::Impl {
    ScriptHost* owner = nullptr;
    lua_State* L = nullptr;
    Limits limits;

    // Memory accounting.
    std::size_t bytes = 0;
    std::size_t call_base = 0;
    bool in_call = false;
    bool mem_tripped = false;

    // CPU accounting.
    uint64_t safepoints = 0;
    bool cpu_tripped = false;

    // Current call frame (null outside calls).
    uint64_t epoch = 1;
    ecs::Registry* reg = nullptr;
    rng::Pcg32* rng = nullptr;

    std::vector<int> script_refs;   // registry refs to {thread, module table}
    std::vector<int> thread_refs;

    static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
        auto* self = static_cast<Impl*>(ud);
        if (nsize == 0) {
            self->bytes -= osize;
            std::free(ptr);
            return nullptr;
        }
        const std::size_t next = self->bytes - osize + nsize;
        if (nsize > osize) {
            const bool over_total = next > self->limits.max_bytes_total;
            const bool over_call = self->in_call && next > self->call_base + self->limits.max_bytes_per_call;
            if (self->mem_tripped || over_total || over_call) {
                if (self->in_call) self->mem_tripped = true;
                return nullptr;   // VM raises LUA_ERRMEM
            }
        }
        void* p = std::realloc(ptr, nsize);
        if (p) self->bytes = next;
        return p;
    }

    static Impl* of(lua_State* L) { return static_cast<Impl*>(lua_callbacks(L)->userdata); }

    static void interrupt(lua_State* L, int gc) {
        if (gc >= 0) return;   // GC steps depend on heap shape only; not counted
        Impl* self = of(L);
        if (!self->in_call) return;
        if (self->mem_tripped) luaL_error(L, "memory limit exceeded");
        if (++self->safepoints > self->limits.max_safepoints_per_call) {
            self->cpu_tripped = true;
            luaL_error(L, "cpu budget exceeded");
        }
    }

    // ---- frame checks -------------------------------------------------------
    static Impl* frame(lua_State* L, uint64_t epoch) {
        Impl* self = of(L);
        if (!self->reg || epoch != self->epoch) luaL_error(L, "engine handle used outside its call");
        return self;
    }
    static ecs::Entity check_entity(lua_State* L, int idx) {
        auto* ud = static_cast<EntityUd*>(lua_touserdatatagged(L, idx, kTagEntity));
        if (!ud) luaL_typeerror(L, idx, "EntityHandle");
        frame(L, ud->epoch);
        return ecs::Entity{ud->index, ud->generation};
    }
    static void push_entity(lua_State* L, ecs::Entity e) {
        auto* ud = static_cast<EntityUd*>(lua_newuserdatataggedwithmetatable(L, sizeof(EntityUd), kTagEntity));
        *ud = EntityUd{e.index, e.generation, of(L)->epoch};
    }
    static int64_t check_int(lua_State* L, int idx, int64_t lo, int64_t hi) {
        const double v = luaL_checknumber(L, idx);
        if (!is_int(v) || int64_t(v) < lo || int64_t(v) > hi) luaL_argerror(L, idx, "integer out of range");
        return int64_t(v);
    }

    // ---- Rng ----------------------------------------------------------------
    static rng::Pcg32& check_rng(lua_State* L) {
        auto* ud = static_cast<RngUd*>(lua_touserdatatagged(L, 1, kTagRng));
        if (!ud) luaL_typeerror(L, 1, "Rng");
        return *frame(L, ud->epoch)->rng;
    }
    static int rng_below(lua_State* L) {
        auto& r = check_rng(L);
        lua_pushnumber(L, double(r.below(uint32_t(check_int(L, 2, 1, 0xFFFF'FFFFll)))));
        return 1;
    }
    static int rng_range(lua_State* L) {
        auto& r = check_rng(L);
        const int64_t lo = check_int(L, 2, INT32_MIN, INT32_MAX), hi = check_int(L, 3, INT32_MIN, INT32_MAX);
        if (lo > hi) luaL_argerror(L, 3, "hi < lo");
        lua_pushnumber(L, double(r.range(int32_t(lo), int32_t(hi))));
        return 1;
    }
    static int rng_chance(lua_State* L) {
        auto& r = check_rng(L);
        const int64_t num = check_int(L, 2, 0, 0xFFFF'FFFFll), den = check_int(L, 3, 1, 0xFFFF'FFFFll);
        if (num > den) luaL_argerror(L, 2, "num > den");
        lua_pushboolean(L, r.chance(uint32_t(num), uint32_t(den)));
        return 1;
    }
    static int rng_d(lua_State* L) {
        auto& r = check_rng(L);
        lua_pushnumber(L, double(r.d(int(check_int(L, 2, 1, 1'000'000)))));
        return 1;
    }

    // ---- EntityHandle -------------------------------------------------------
    static int ent_id(lua_State* L) {
        ecs::Entity e = check_entity(L, 1);
        auto id = of(L)->reg->stable_id(e);
        if (!id) luaL_error(L, "stale entity handle");
        if (*id >= (uint64_t(1) << 53)) luaL_error(L, "stable_id exceeds 2^53");
        lua_pushnumber(L, double(*id));
        return 1;
    }
    static int ent_alive(lua_State* L) {
        ecs::Entity e = check_entity(L, 1);
        lua_pushboolean(L, of(L)->reg->alive(e));
        return 1;
    }
    template <class C, class F> static int ent_read(lua_State* L, F&& push) {
        ecs::Entity e = check_entity(L, 1);
        C* c = of(L)->reg->get<C>(e);
        if (!c) { lua_pushnil(L); return 1; }
        return push(*c);
    }
    static int ent_hp(lua_State* L) { return ent_read<Health>(L, [L](Health& h) { lua_pushnumber(L, h.hp); return 1; }); }
    static int ent_hp_max(lua_State* L) { return ent_read<Health>(L, [L](Health& h) { lua_pushnumber(L, h.hp_max); return 1; }); }
    static int ent_team(lua_State* L) { return ent_read<Team>(L, [L](Team& t) { lua_pushnumber(L, t.team); return 1; }); }
    static int ent_tile(lua_State* L) {
        return ent_read<TilePos>(L, [L](TilePos& p) {
            lua_createtable(L, 0, 2);
            lua_pushnumber(L, p.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, p.y); lua_setfield(L, -2, "y");
            return 1;
        });
    }
    static int ent_eq(lua_State* L) {
        auto* a = static_cast<EntityUd*>(lua_touserdatatagged(L, 1, kTagEntity));
        auto* b = static_cast<EntityUd*>(lua_touserdatatagged(L, 2, kTagEntity));
        lua_pushboolean(L, a && b && a->index == b->index && a->generation == b->generation && a->epoch == b->epoch);
        return 1;
    }

    // ---- SimContext ---------------------------------------------------------
    static Impl* check_ctx(lua_State* L) {
        auto* ud = static_cast<CtxUd*>(lua_touserdatatagged(L, 1, kTagCtx));
        if (!ud) luaL_typeerror(L, 1, "SimContext");
        return frame(L, ud->epoch);
    }
    static void push_list(lua_State* L, const std::vector<ecs::Entity>& es) {
        lua_createtable(L, int(es.size()), 0);
        for (std::size_t i = 0; i < es.size(); ++i) {
            push_entity(L, es[i]);
            lua_rawseti(L, -2, int(i + 1));
        }
    }
    static int ctx_entities(lua_State* L) {
        Impl* self = check_ctx(L);
        push_list(L, self->reg->entities());
        return 1;
    }
    static int ctx_enemies_of(lua_State* L) {
        Impl* self = check_ctx(L);
        ecs::Entity me = check_entity(L, 2);
        Team* mine = self->reg->get<Team>(me);
        std::vector<ecs::Entity> out;
        for (ecs::Entity e : self->reg->entities()) {   // stable_id order
            if (e == me) continue;
            Team* t = self->reg->get<Team>(e);
            Health* h = self->reg->get<Health>(e);
            if (!t || !h || h->hp <= 0) continue;
            if (mine && t->team == mine->team) continue;
            out.push_back(e);
        }
        push_list(L, out);
        return 1;
    }
    static int ctx_index(lua_State* L) {
        check_ctx(L);
        const char* k = luaL_checkstring(L, 2);
        if (std::strcmp(k, "rng") == 0) {
            auto* ud = static_cast<RngUd*>(lua_newuserdatataggedwithmetatable(L, sizeof(RngUd), kTagRng));
            ud->epoch = of(L)->epoch;
        } else if (std::strcmp(k, "entities") == 0) {
            lua_pushcfunction(L, ctx_entities, "entities");
        } else if (std::strcmp(k, "enemies_of") == 0) {
            lua_pushcfunction(L, ctx_enemies_of, "enemies_of");
        } else {
            luaL_error(L, "SimContext has no member '%s'", k);
        }
        return 1;
    }

    // ---- globals ------------------------------------------------------------
    static int g_log(lua_State* L) {
        size_t n = 0;
        const char* s = luaL_checklstring(L, 1, &n);
        of(L)->owner->log_.emplace_back(s, n);
        return 0;
    }
    static int g_collectgarbage(lua_State* L) {
        const char* opt = luaL_optstring(L, 1, "count");
        if (std::strcmp(opt, "count") != 0) luaL_error(L, "collectgarbage: only \"count\" is allowed");
        lua_pushnumber(L, double(lua_gc(L, LUA_GCCOUNT, 0)));
        return 1;
    }

    static void make_metatable(lua_State* L, int tag, const char* type, const luaL_Reg* methods, lua_CFunction index_fn) {
        lua_createtable(L, 0, 3);
        if (index_fn) {
            lua_pushcfunction(L, index_fn, "__index");
        } else {
            lua_createtable(L, 0, 8);
            for (const luaL_Reg* m = methods; m->name; ++m) {
                lua_pushcfunction(L, m->func, m->name);
                lua_setfield(L, -2, m->name);
            }
            lua_setreadonly(L, -1, true);
        }
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, type);
        lua_setfield(L, -2, "__type");
        lua_pushstring(L, "locked");
        lua_setfield(L, -2, "__metatable");
        if (tag == kTagEntity) {
            lua_pushcfunction(L, ent_eq, "__eq");
            lua_setfield(L, -2, "__eq");
        }
        lua_setreadonly(L, -1, true);
        lua_setuserdatametatable(L, tag);
    }

    void setup() {
        lua_callbacks(L)->userdata = this;
        lua_callbacks(L)->interrupt = &Impl::interrupt;

        // Safe libraries only (§3.3). os is never opened.
        const luaL_Reg libs[] = {
            {"", luaopen_base}, {LUA_COLIBNAME, luaopen_coroutine}, {LUA_TABLIBNAME, luaopen_table},
            {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math}, {LUA_DBLIBNAME, luaopen_debug},
            {LUA_UTF8LIBNAME, luaopen_utf8}, {LUA_BITLIBNAME, luaopen_bit32}, {LUA_BUFFERLIBNAME, luaopen_buffer},
        };
        for (const luaL_Reg& lib : libs) {
            lua_pushcfunction(L, lib.func, nullptr);
            lua_pushstring(L, lib.name);
            lua_call(L, 1, 0);
        }
        for (const char* g : {"loadstring", "getfenv", "setfenv", "require", "gcinfo", "newproxy"}) {
            lua_pushnil(L);
            lua_setglobal(L, g);
        }
        lua_pushcfunction(L, g_collectgarbage, "collectgarbage");
        lua_setglobal(L, "collectgarbage");
        // debug -> {traceback}
        lua_getglobal(L, "debug");
        lua_getfield(L, -1, "traceback");
        lua_createtable(L, 0, 1);
        lua_insert(L, -2);
        lua_setfield(L, -2, "traceback");
        lua_setglobal(L, "debug");
        lua_pop(L, 1);
        // math: sim context drops randomness and libm-backed functions (§3.3, §4.5).
        lua_getglobal(L, "math");
        for (const char* f : {"random", "randomseed", "noise", "sin", "cos", "tan", "asin", "acos", "atan",
                              "atan2", "sinh", "cosh", "tanh", "exp", "log", "log10", "pow", "sqrt", "fmod",
                              "frexp", "ldexp", "modf"}) {
            lua_pushnil(L);
            lua_setfield(L, -2, f);
        }
        lua_pop(L, 1);

        static const luaL_Reg rng_methods[] = {{"below", rng_below}, {"range", rng_range}, {"chance", rng_chance},
                                               {"d", rng_d}, {nullptr, nullptr}};
        static const luaL_Reg ent_methods[] = {{"id", ent_id}, {"alive", ent_alive}, {"hp", ent_hp},
                                               {"hp_max", ent_hp_max}, {"team", ent_team}, {"tile", ent_tile},
                                               {nullptr, nullptr}};
        make_metatable(L, kTagRng, "Rng", rng_methods, nullptr);
        make_metatable(L, kTagEntity, "EntityHandle", ent_methods, nullptr);
        make_metatable(L, kTagCtx, "SimContext", nullptr, ctx_index);

        lua_pushcfunction(L, g_log, "log");
        lua_setglobal(L, "log");

        luaL_sandbox(L);
    }

    void begin_call() {
        in_call = true;
        call_base = bytes;
        mem_tripped = cpu_tripped = false;
        safepoints = 0;
    }
    ScriptError end_call(int status, lua_State* T) {
        ScriptError e;
        if (status != LUA_OK) {
            const char* msg = lua_tostring(T, -1);
            e.message = msg ? msg : "error";
            e.kind = cpu_tripped ? ErrorKind::CpuBudget
                   : (mem_tripped || status == LUA_ERRMEM) ? ErrorKind::Memory
                   : ErrorKind::Runtime;
            if (e.kind == ErrorKind::Memory && !msg) e.message = "memory limit exceeded";
            lua_settop(T, 0);
        }
        in_call = false;
        reg = nullptr;
        rng = nullptr;
        ++epoch;   // invalidates every handle handed out during the call
        return e;
    }
};

ScriptHost::ScriptHost(uint32_t api_level, Limits limits) : impl_(std::make_unique<Impl>()), api_level_(api_level) {
    impl_->owner = this;
    impl_->limits = limits;
    impl_->L = lua_newstate(&Impl::alloc, impl_.get());
    impl_->setup();
}

ScriptHost::~ScriptHost() {
    if (impl_ && impl_->L) lua_close(impl_->L);
}

std::size_t ScriptHost::bytes_in_use() const noexcept { return impl_->bytes; }

std::optional<ScriptId> ScriptHost::load_script(std::string_view name, std::string_view source, ScriptError* err) {
    auto fail = [&](ErrorKind k, std::string m) -> std::optional<ScriptId> {
        if (err) *err = ScriptError{k, std::move(m)};
        return std::nullopt;
    };
    lua_State* L = impl_->L;
    lua_CompileOptions opts{};
    opts.optimizationLevel = 1;
    opts.debugLevel = 1;
    size_t size = 0;
    char* bc = luau_compile(source.data(), source.size(), &opts, &size);
    if (!bc) return fail(ErrorKind::Compile, "compile failed");
    const std::string bytecode(bc, size);
    std::free(bc);

    lua_State* T = lua_newthread(L);          // [thread]
    const int tref = lua_ref(L, -1);
    lua_pop(L, 1);
    luaL_sandboxthread(T);

    const std::string chunk = "=" + std::string(name);
    if (luau_load(T, chunk.c_str(), bytecode.data(), bytecode.size(), 0) != 0) {
        std::string m = lua_tostring(T, -1) ? lua_tostring(T, -1) : "load failed";
        lua_settop(T, 0);
        lua_unref(L, tref);
        return fail(ErrorKind::Compile, m);
    }
    impl_->begin_call();
    impl_->reg = nullptr;
    const int st = lua_pcall(T, 0, 1, 0);
    ScriptError e = impl_->end_call(st, T);
    if (e.kind != ErrorKind::None) {
        lua_unref(L, tref);
        return fail(e.kind, e.message);
    }
    if (!lua_istable(T, -1)) {
        lua_settop(T, 0);
        lua_unref(L, tref);
        return fail(ErrorKind::BadResult, std::string(name) + ": module must return a table");
    }
    const int mref = lua_ref(T, -1);
    lua_settop(T, 0);
    impl_->thread_refs.push_back(tref);
    impl_->script_refs.push_back(mref);
    return ScriptId{uint32_t(impl_->script_refs.size() - 1)};
}

CallResult ScriptHost::call_entity_fn(ScriptId id, std::string_view fn, ecs::Registry& reg, ecs::Entity self,
                                      rng::Pcg32& rng) {
    CallResult r;
    if (id.value >= impl_->script_refs.size()) { r.error = {ErrorKind::Runtime, "unknown script"}; return r; }
    lua_State* L = impl_->L;
    lua_getref(L, impl_->thread_refs[id.value]);
    lua_State* T = lua_tothread(L, -1);
    lua_pop(L, 1);
    lua_settop(T, 0);

    impl_->begin_call();
    impl_->reg = &reg;
    impl_->rng = &rng;
    lua_getref(T, impl_->script_refs[id.value]);
    lua_getfield(T, -1, std::string(fn).c_str());
    lua_remove(T, -2);
    if (!lua_isfunction(T, -1)) {
        impl_->end_call(LUA_OK, T);
        lua_settop(T, 0);
        r.error = {ErrorKind::BadResult, "module has no function '" + std::string(fn) + "'"};
        return r;
    }
    auto* ctx = static_cast<CtxUd*>(lua_newuserdatataggedwithmetatable(T, sizeof(CtxUd), kTagCtx));
    ctx->epoch = impl_->epoch;
    Impl::push_entity(T, self);
    const int st = lua_pcall(T, 2, 1, 0);
    r.safepoints = impl_->safepoints;
    if (st == LUA_OK) {
        if (lua_isnil(T, -1)) {
            r.target = std::nullopt;
        } else if (auto* ud = static_cast<EntityUd*>(lua_touserdatatagged(T, -1, kTagEntity));
                   ud && ud->epoch == impl_->epoch && reg.alive(ecs::Entity{ud->index, ud->generation})) {
            r.target = ecs::Entity{ud->index, ud->generation};
        } else {
            r.error = {ErrorKind::BadResult, std::string(fn) + " must return an EntityHandle or nil"};
        }
        lua_settop(T, 0);
    }
    ScriptError e = impl_->end_call(st, T);
    if (e.kind != ErrorKind::None) r.error = e;
    return r;
}

std::string api_definitions(uint32_t api_level) {
    // Level N = levels 1..N concatenated in order (a level-N pack sees every lower-level API).
    struct Part { const unsigned char* data; std::size_t size; };
    const Part parts[kMaxApiLevel] = {
        {reinterpret_cast<const unsigned char*>(embedded::kEngineLevel1Defs), embedded::kEngineLevel1Defs_size},
        {reinterpret_cast<const unsigned char*>(embedded::kEngineLevel2Defs), embedded::kEngineLevel2Defs_size},
    };
    if (api_level < 1 || api_level > kMaxApiLevel) return {};
    std::string out;
    for (uint32_t i = 0; i < api_level; ++i) {
        if (i) out += '\n';
        out.append(reinterpret_cast<const char*>(parts[i].data), parts[i].size);
    }
    return out;
}

std::optional<uint32_t> read_pack_api_level(const std::filesystem::path& pack_dir, std::string* err) {
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    auto lr = reg.load_file(pack_dir / "manifest.json");
    if (!lr.ok) { if (err) *err = lr.error; return std::nullopt; }
    auto it = lr.data.find("api_level");
    if (it == lr.data.end() || !it->is_number_unsigned() || it->get<uint64_t>() < 1 || it->get<uint64_t>() > 0xFFFF'FFFFull) {
        if (err) *err = "manifest.json: api_level must be a positive integer";
        return std::nullopt;
    }
    return uint32_t(it->get<uint64_t>());
}

namespace {
std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Shared by load and check: api_level + sorted scripts/**/*.luau sources.
bool collect(const std::filesystem::path& pack_dir, uint32_t& level, std::map<std::string, std::string>& sources,
             std::vector<std::string>& errors) {
    std::string err;
    auto lv = read_pack_api_level(pack_dir, &err);
    if (!lv) { errors.push_back(err); return false; }
    level = *lv;
    if (level > kMaxApiLevel) {
        errors.push_back("manifest.json: api_level " + std::to_string(level) + " > engine max " + std::to_string(kMaxApiLevel));
        return false;
    }
    const auto dir = pack_dir / "scripts";
    if (std::filesystem::is_directory(dir))
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
            if (entry.is_regular_file() && entry.path().extension() == ".luau")
                sources[std::filesystem::relative(entry.path(), pack_dir).generic_string()] = read_file(entry.path());
    return true;
}
} // namespace

PackCheck check_pack_scripts(const std::filesystem::path& pack_dir) {
    PackCheck out;
    std::map<std::string, std::string> sources;
    if (!collect(pack_dir, out.api_level, sources, out.errors)) return out;
    out.scripts = sources.size();
    out.errors = typecheck(out.api_level, sources);
    return out;
}

PackScripts load_pack_scripts(ScriptHost& host, const std::filesystem::path& pack_dir) {
    return load_pack_scripts(host, assets::PackSet::from_pack_dir(pack_dir));
}

PackScripts load_pack_scripts(ScriptHost& host, const assets::PackSet& packs) {
    PackScripts out;
    if (!packs.ok()) {
        out.errors = packs.error_strings();
        return out;
    }
    out.api_level = host.api_level();
    // Per pack (resolved order): api_level check, strict typecheck, then load in script-id order.
    for (const assets::PackInfo& p : packs.packs()) {
        uint32_t level = 0;
        std::map<std::string, std::string> sources;   // script id -> source
        std::vector<std::string> errs;
        if (!collect(p.dir, level, sources, errs)) {
            for (auto& e : errs) out.errors.push_back(p.id + ": " + e);
            return out;
        }
        if (level > host.api_level()) {   // a pack may run below the host's maximum, never above it
            out.errors.push_back(p.id + ": pack api_level " + std::to_string(level) + " > host api_level " +
                                 std::to_string(host.api_level()));
            return out;
        }
        out.pack_levels[p.id] = level;
        std::map<std::string, std::string> by_id;
        for (auto& [rel, src] : sources) by_id.emplace(assets::make_script_key(p.id, rel), std::move(src));
        auto terrs = typecheck(level, by_id);
        if (!terrs.empty()) {
            out.errors.insert(out.errors.end(), terrs.begin(), terrs.end());
            return out;
        }
        for (const auto& [sid, src] : by_id) {
            ScriptError e;
            auto id = host.load_script(sid, src, &e);
            if (!id) { out.errors.push_back(sid + ": " + e.message); return out; }
            out.scripts.emplace(sid, *id);
        }
    }
    // behavior.ai bindings resolve within the archetype's own pack.
    for (const assets::PackDocument& d : packs.documents()) {
        if (!d.doc.ok) {   // stat blocks must load; other unknown types are not ours to judge
            if (d.doc.declared.type == "monster" || d.doc.declared.type == "character" ||
                d.doc.error.find("unknown schema type") == std::string::npos)
                out.errors.push_back(d.doc.error);
            continue;
        }
        if (d.doc.current.type != "monster" && d.doc.current.type != "character") continue;
        auto ai = assets::behavior_ai_script(d.doc.data);
        if (!ai) continue;
        const std::string& pack_id = packs.packs()[d.pack].id;
        const std::string sid = assets::make_script_key(pack_id, *ai);
        auto it = out.scripts.find(sid);
        if (it == out.scripts.end()) {
            out.errors.push_back(pack_id + ":" + d.path + ": behavior.ai '" + *ai + "' is not a script of pack " + pack_id);
            continue;
        }
        out.ai_by_archetype[d.content_id.empty() ? d.path : d.content_id] = it->second;
    }
    out.ok = out.errors.empty();
    return out;
}

} // namespace eng::script
