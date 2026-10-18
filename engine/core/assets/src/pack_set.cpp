#include "assets/pack_set.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <set>
#include <sstream>

#include "assets/content_migrations.hpp"

namespace eng::assets {

namespace {
// One "(0|[1-9][0-9]*)" component; advances s.
std::optional<uint32_t> take_uint(std::string_view& s) {
    std::size_t n = 0;
    while (n < s.size() && s[n] >= '0' && s[n] <= '9') ++n;
    if (n == 0 || (n > 1 && s[0] == '0')) return std::nullopt;
    uint32_t v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + n, v);
    if (ec != std::errc{} || p != s.data() + n) return std::nullopt;
    s.remove_prefix(n);
    return v;
}
bool take_dot(std::string_view& s) {
    if (s.empty() || s[0] != '.') return false;
    s.remove_prefix(1);
    return true;
}
std::optional<Json> read_json(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        return Json::parse(ss.str());
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
std::string str_of(const Json& j) { return j.is_string() ? j.get<std::string>() : j.dump(); }   // python str()
std::string repr_of(const Json& j) {                                                             // python repr()
    if (j.is_string()) return "'" + j.get<std::string>() + "'";
    if (j.is_null()) return "None";
    return j.dump();
}
std::string schema_type_of(const Json& d) {
    if (!d.is_object() || !d.contains("$schema") || !d["$schema"].is_string()) return "";
    const std::string s = d["$schema"].get<std::string>();
    return s.substr(0, s.find('@'));
}
std::vector<std::string> sorted_contents(const Json& man) {
    std::vector<std::string> out;
    if (auto c = man.find("contents"); c != man.end() && c->is_array())
        for (const auto& e : *c) if (e.is_string()) out.push_back(e.get<std::string>());
    std::sort(out.begin(), out.end());
    return out;
}
bool ends_with(const std::string& s, std::string_view suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
} // namespace

std::optional<SemVer> parse_version(std::string_view s) {
    SemVer v;
    auto a = take_uint(s);
    if (!a || !take_dot(s)) return std::nullopt;
    auto b = take_uint(s);
    if (!b || !take_dot(s)) return std::nullopt;
    auto c = take_uint(s);
    if (!c || !s.empty()) return std::nullopt;   // prerelease / trailing junk -> nullopt
    v.major = *a;
    v.minor = *b;
    v.patch = *c;
    return v;
}

std::optional<VersionRange> parse_range(std::string_view s) {
    enum { Exact, Caret, Tilde, AtLeast } op = Exact;
    if (s.starts_with("^")) { op = Caret; s.remove_prefix(1); }
    else if (s.starts_with("~")) { op = Tilde; s.remove_prefix(1); }
    else if (s.starts_with(">=")) { op = AtLeast; s.remove_prefix(2); }
    auto a = take_uint(s);
    if (!a || !take_dot(s)) return std::nullopt;
    auto b = take_uint(s);
    if (!b) return std::nullopt;
    std::optional<uint32_t> c;
    if (!s.empty()) {
        if (!take_dot(s)) return std::nullopt;
        c = take_uint(s);
        if (!c || !s.empty()) return std::nullopt;
    }
    if (!c && op != Caret) return std::nullopt;   // X.Y only as ^X.Y
    const uint32_t z = c.value_or(0);
    VersionRange r;
    r.lo = {*a, *b, z};
    switch (op) {
        case Exact: r.hi_exclusive = SemVer{*a, *b, z + 1}; break;
        case AtLeast: break;
        case Tilde: r.hi_exclusive = SemVer{*a, *b + 1, 0}; break;
        case Caret:   // bump the left-most non-zero component (npm)
            if (*a > 0) r.hi_exclusive = SemVer{*a + 1, 0, 0};
            else if (*b > 0 || !c) r.hi_exclusive = SemVer{0, *b + 1, 0};
            else r.hi_exclusive = SemVer{0, 0, z + 1};
            break;
    }
    return r;
}

bool satisfies(const SemVer& v, const VersionRange& r) { return v >= r.lo && (!r.hi_exclusive || v < *r.hi_exclusive); }

std::string to_string(const SemVer& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

std::string_view to_string(PackSetError::Kind k) {
    using K = PackSetError::Kind;
    switch (k) {
        case K::MissingDep: return "missing_dep";
        case K::VersionMismatch: return "version_mismatch";
        case K::BadRange: return "bad_range";
        case K::Cycle: return "cycle";
        case K::DuplicatePackId: return "duplicate_pack_id";
        case K::DuplicateContentId: return "duplicate_content_id";
        case K::BadManifest: return "bad_manifest";
    }
    return "unknown";
}

std::string to_string(const PackSetError& e) { return std::string(to_string(e.kind)) + ": " + e.detail; }

std::string make_script_key(std::string_view pack_id, std::string_view rel) {
    std::string s(pack_id);
    s += ':';
    s += rel;
    return s;
}

std::vector<std::string> PackSet::error_strings() const {
    std::vector<std::string> out;
    for (const auto& e : errors_) out.push_back(to_string(e));
    return out;
}

std::optional<std::size_t> PackSet::index_of(std::string_view id) const {
    for (std::size_t i = 0; i < packs_.size(); ++i) if (packs_[i].id == id) return i;
    return std::nullopt;
}
const PackInfo* PackSet::find(std::string_view id) const {
    auto i = index_of(id);
    return i ? &packs_[*i] : nullptr;
}
const PackInfo* PackSet::owner_of(std::string_view cid) const {
    auto it = owner_.find(cid);
    return it == owner_.end() ? nullptr : &packs_[it->second];
}
std::vector<std::string> PackSet::closure(std::string_view from) const {
    std::set<std::string, std::less<>> seen;
    std::vector<std::string> todo{std::string(from)};
    while (!todo.empty()) {
        std::string p = todo.back();
        todo.pop_back();
        if (!seen.insert(p).second) continue;
        if (auto it = deps_.find(p); it != deps_.end()) for (const auto& d : it->second) todo.push_back(d);
    }
    std::vector<std::string> out;
    for (const auto& p : packs_) if (seen.count(p.id)) out.push_back(p.id);
    return out;
}
const PackInfo* PackSet::resolve_ref(std::string_view from, std::string_view cid) const {
    const PackInfo* o = owner_of(cid);
    if (!o) return nullptr;
    const auto c = closure(from);
    return std::find(c.begin(), c.end(), o->id) != c.end() ? o : nullptr;
}
std::optional<std::filesystem::path> PackSet::script(std::string_view key) const {
    auto it = scripts_.find(key);
    if (it == scripts_.end()) return std::nullopt;
    return it->second;
}
std::string PackSet::script_key_for(std::string_view cid, std::string_view rel) const {
    const PackInfo* o = owner_of(cid);
    return make_script_key(o ? std::string_view(o->id) : std::string_view(), rel);
}

PackSet PackSet::from_pack_dir(const std::filesystem::path& pack_dir) {
    const auto dir = pack_dir.lexically_normal();
    std::filesystem::path root = dir.has_filename() ? dir.parent_path() : dir.parent_path().parent_path();
    std::vector<std::string> req;
    if (auto m = read_json(pack_dir / "manifest.json"); m && m->is_object() && m->contains("id")) req.push_back(str_of((*m)["id"]));
    const std::filesystem::path roots[] = {root};
    PackSet s = resolve(roots, req);
    if (req.empty())
        s.errors_.push_back({PackSetError::Kind::BadManifest, pack_dir.string(), "", pack_dir.string() + ": unreadable manifest.json"});
    return s;
}

PackSet PackSet::resolve(std::span<const std::filesystem::path> roots, std::span<const std::string> requested) {
    std::vector<std::filesystem::path> dirs;
    for (const auto& r : roots) {
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(r, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
            if (it->is_directory() && std::filesystem::exists(it->path() / "manifest.json")) dirs.push_back(it->path());
    }
    return resolve_dirs(dirs, requested);
}

std::vector<std::string> PackSet::reference_errors() const {
    // Mirrors tools/validate/validate_packs.py (validate_pack: script refs; check_chunk: spawn
    // archetypes; check_spell: blocks_spell / or_targeted_by). Per pack: manifest entry_script,
    // then script refs of every document, then the per-document content checks, documents sorted.
    std::vector<std::string> out;

    auto repr = [](const Json& j) {   // python repr() of a JSON scalar
        if (j.is_string()) return "'" + j.get<std::string>() + "'";
        if (j.is_null()) return std::string("None");
        if (j.is_boolean()) return std::string(j.get<bool>() ? "True" : "False");
        return j.dump();
    };
    // check_script_ref: ^scripts/.+\.luau$, then existence in the referencing pack only.
    auto script_ref = [&](const std::string& pid, const std::string& where, const Json& path) {
        bool shape = false;
        if (path.is_string()) {
            const std::string p = path.get<std::string>();
            shape = p.size() >= 14 && p.starts_with("scripts/") && ends_with(p, ".luau") &&
                    p.find('\n') == std::string::npos;
        }
        if (!shape)
            out.push_back(pid + ":" + where + ": script path " + repr(path) + " must match ^scripts/.+\\.luau$");
        else if (!script(make_script_key(pid, path.get<std::string>())))
            out.push_back(pid + ":" + where + ": script " + path.get<std::string>() + " does not exist in pack");
    };
    auto is_content_type = [](const std::string& t) { return t == "monster" || t == "character" || t == "spell"; };
    // validator spell_ids: spell@ documents of the pack itself; from each dependency, a content id
    // (monster/character/spell) whose file is under spells/ or is a spell@ document.
    auto spell_visible = [&](const std::string& pid, const std::string& ref) {
        for (const auto& pk : closure(pid)) {
            const auto idx = index_of(pk);
            if (!idx) continue;
            const bool own = pk == pid;
            for (const auto& d : docs_) {
                if (d.pack != *idx || !d.doc.ok || d.content_id != ref) continue;
                const std::string t = schema_type_of(d.doc.data);
                if (own ? t == "spell" : (is_content_type(t) && (t == "spell" || d.path.starts_with("spells/")))) return true;
                if (!own && is_content_type(t)) break;   // content_ids keeps the first such document per id
            }
        }
        return false;
    };

    for (std::size_t pi = 0; pi < packs_.size(); ++pi) {
        const std::string& pid = packs_[pi].id;
        if (auto e = packs_[pi].manifest.find("entry_script"); e != packs_[pi].manifest.end())
            script_ref(pid, "manifest.json: entry_script", *e);
        for (const auto& d : docs_) {
            if (d.pack != pi || !d.doc.ok || !d.doc.data.is_object()) continue;
            const Json& j = d.doc.data;
            if (auto b = j.find("behavior"); b != j.end() && b->is_object())
                if (auto ai = b->find("ai"); ai != b->end()) {
                    // monster@1 files carry it as presentation.ai (migrated to behavior.ai on load).
                    const bool v1 = d.doc.declared.type == "monster" && d.doc.declared.version == 1;
                    script_ref(pid, d.path + ": " + (v1 ? "presentation.ai" : "behavior.ai"), *ai);
                }
        }
        for (const auto& d : docs_) {
            if (d.pack != pi || !d.doc.ok) continue;
            const std::string type = schema_type_of(d.doc.data);
            if (type == "chunk") {
                const auto sp = d.doc.data.find("spawns");
                if (sp == d.doc.data.end() || !sp->is_array()) continue;
                for (std::size_t i = 0; i < sp->size(); ++i) {
                    const Json& s = (*sp)[i];
                    if (!s.is_object() || !s.contains("archetype") || !s["archetype"].is_string()) continue;
                    const std::string a = s["archetype"].get<std::string>();
                    if (!resolve_ref(pid, a))
                        out.push_back(pid + ":" + d.path + ": spawns[" + std::to_string(i) + "]: archetype '" + a +
                                      "' not defined in pack or its depends");
                }
            } else if (type == "spell") {
                const Json& j = d.doc.data;
                const auto effs = j.find("effects");
                // blocks_spell lives on the first effect (reaction effects stand alone).
                if (effs == j.end() || !effs->is_array() || effs->empty()) continue;
                std::vector<std::pair<std::string, std::string>> refs;   // (where, id), validator order
                const Json& eff = (*effs)[0];
                if (eff.is_object())
                    if (auto b = eff.find("blocks_spell"); b != eff.end() && b->is_string() && !b->get<std::string>().empty())
                        refs.emplace_back("effect blocks_spell", b->get<std::string>());
                if (auto ct = j.find("casting_time"); ct != j.end() && ct->is_object())
                    if (auto tr = ct->find("trigger"); tr != ct->end() && tr->is_object())
                        if (auto ot = tr->find("or_targeted_by"); ot != tr->end() && ot->is_array())
                            for (const auto& r : *ot)
                                if (r.is_string() && !r.get<std::string>().empty())
                                    refs.emplace_back("casting_time.trigger.or_targeted_by", r.get<std::string>());
                for (const auto& [where, ref] : refs)
                    if (!spell_visible(pid, ref))
                        out.push_back(pid + ":" + d.path + ": " + where + " '" + ref + "' is not a spell@1 in this pack or its depends");
            }
        }
    }
    return out;
}

PackSet PackSet::resolve_dirs(std::span<const std::filesystem::path> in_dirs, std::span<const std::string> requested) {
    using K = PackSetError::Kind;
    PackSet out;
    SchemaRegistry reg;
    register_content_schemas(reg);
    auto err = [&](K k, std::string pack, std::string id, std::string detail) {
        out.errors_.push_back({k, std::move(pack), std::move(id), std::move(detail)});
    };

    // 1. Index: <root>/<dir>/manifest.json, all dirs sorted by path.
    struct Pack { PackInfo info; Json raw; std::map<std::string, std::string> content_ids; };
    std::vector<std::filesystem::path> dirs(in_dirs.begin(), in_dirs.end());
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) { return a.string() < b.string(); });
    std::map<std::string, Pack> by_id;
    for (const auto& d : dirs) {
        auto man = read_json(d / "manifest.json");
        if (!man || !man->is_object()) { err(K::BadManifest, d.string(), "", d.string() + ": unreadable manifest.json"); continue; }
        Pack p;
        p.raw = *man;
        p.info.id = man->contains("id") ? str_of((*man)["id"]) : "";
        p.info.version_text = man->contains("version") ? str_of((*man)["version"]) : "";
        p.info.version = parse_version(p.info.version_text).value_or(SemVer{});
        p.info.dir = d;
        auto lr = reg.load(*man);
        p.info.manifest = lr.ok ? lr.data : *man;
        for (const auto& rel : sorted_contents(*man)) {
            if (!ends_with(rel, ".json")) continue;
            auto doc = read_json(d / rel);
            if (!doc) continue;
            const std::string t = schema_type_of(*doc);
            if ((t == "monster" || t == "character" || t == "spell" || t == "item") && (*doc).contains("id") && (*doc)["id"].is_string())
                p.content_ids.emplace((*doc)["id"].get<std::string>(), rel);   // setdefault
        }
        if (by_id.count(p.info.id)) {
            err(K::DuplicatePackId, p.info.id, p.info.id,
                "pack id '" + p.info.id + "' is defined by both " + by_id[p.info.id].info.dir.string() + " and " + d.string());
            continue;
        }
        by_id.emplace(p.info.id, std::move(p));
    }

    // 2. Closure (DFS from sorted, deduplicated requests; deps sorted by id).
    std::map<std::string, Pack*> needed;
    std::map<std::string, std::set<std::string>> edges;
    auto visit = [&](auto&& self, const std::string& pid, std::vector<std::string> stack) -> void {
        if (auto at = std::find(stack.begin(), stack.end(), pid); at != stack.end()) {
            std::vector<std::string> ring(at, stack.end());
            auto k = std::min_element(ring.begin(), ring.end());
            std::rotate(ring.begin(), k, ring.end());
            std::string chain;
            for (const auto& r : ring) chain += r + " -> ";
            chain += ring[0];
            err(K::Cycle, ring[0], ring[0], "dependency cycle: " + chain);
            return;
        }
        if (needed.count(pid)) return;
        Pack& p = by_id.at(pid);
        needed[pid] = &p;
        edges[pid];
        std::vector<Json> deps;
        if (auto d = p.raw.find("depends"); d != p.raw.end() && d->is_array())
            for (const auto& e : *d) if (e.is_object()) deps.push_back(e);
        std::stable_sort(deps.begin(), deps.end(), [](const Json& a, const Json& b) {
            return str_of(a.value("id", Json())) < str_of(b.value("id", Json()));
        });
        for (const Json& dep : deps) {
            const std::string did = str_of(dep.value("id", Json()));
            const Json rng = dep.value("version", Json());
            const std::string rtext = rng.is_string() ? rng.get<std::string>() : str_of(rng);
            std::optional<VersionRange> range = rng.is_string() ? parse_range(rtext) : std::nullopt;
            if (!range) {
                err(K::BadRange, pid, did, pid + ": depends '" + did + "' range " + repr_of(rng) +
                                               " is not X.Y.Z, ^X.Y, ^X.Y.Z, ~X.Y.Z or >=X.Y.Z");
                continue;
            }
            auto q = by_id.find(did);
            if (q == by_id.end()) {
                err(K::MissingDep, pid, did, pid + ": depends on '" + did + "' " + rtext + ", which is not in the pack set");
                continue;
            }
            const auto qv = parse_version(q->second.info.version_text);
            if (!qv || !satisfies(*qv, *range)) {
                err(K::VersionMismatch, pid, did, pid + ": depends on '" + did + "' " + rtext + ", but the set has " + did + " " +
                                                      q->second.info.version_text);
                continue;
            }
            edges[pid].insert(did);
            auto next = stack;
            next.push_back(pid);
            self(self, did, std::move(next));
        }
    };
    std::set<std::string> req(requested.begin(), requested.end());
    for (const auto& r : req) {
        if (!by_id.count(r)) err(K::MissingDep, r, r, "requested pack '" + r + "' is not in the pack set");
        else visit(visit, r, {});
    }

    // 3. Kahn, ready set by id.
    std::map<std::string, std::set<std::string>> remaining = edges;
    std::vector<std::string> order;
    while (!remaining.empty()) {
        auto it = std::find_if(remaining.begin(), remaining.end(), [](const auto& kv) { return kv.second.empty(); });
        if (it == remaining.end()) break;   // cycle (already reported)
        const std::string k = it->first;
        order.push_back(k);
        remaining.erase(it);
        for (auto& [_, v] : remaining) v.erase(k);
    }
    for (const auto& k : order) {
        out.packs_.push_back(needed.at(k)->info);
        out.deps_[k] = std::vector<std::string>(edges[k].begin(), edges[k].end());
    }

    // 4. Duplicate content ids across the resolved set.
    std::map<std::string, std::size_t> owner;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Pack& p = *needed.at(order[i]);
        for (const auto& [cid, rel] : p.content_ids) {
            auto o = owner.find(cid);
            if (o != owner.end() && o->second != i) {
                const Pack& op = *needed.at(order[o->second]);
                err(K::DuplicateContentId, p.info.id, cid, "content id '" + cid + "' is defined in both " + op.info.id + " (" +
                                                               op.content_ids.at(cid) + ") and " + p.info.id + " (" + rel + ")");
            } else {
                owner.emplace(cid, i);
            }
        }
    }
    for (const auto& [cid, i] : owner) out.owner_.emplace(cid, i);

    // 5. Documents (registry-loaded) and scripts of the resolved packs.
    for (std::size_t i = 0; i < out.packs_.size(); ++i) {
        const PackInfo& p = out.packs_[i];
        for (const auto& rel : sorted_contents(p.manifest)) {
            if (!ends_with(rel, ".json")) continue;
            PackDocument d;
            d.pack = i;
            d.path = rel;
            try {
                d.doc = reg.load_file(p.dir / rel);
            } catch (const std::exception& ex) {
                d.doc.ok = false;
                d.doc.error = ex.what();
            }
            if (!d.doc.ok) out.doc_errors_.push_back(p.id + ":" + rel + ": " + d.doc.error);
            else if (auto idv = d.doc.data.find("id"); idv != d.doc.data.end() && idv->is_string()) d.content_id = idv->get<std::string>();
            out.docs_.push_back(std::move(d));
        }
        std::vector<std::string> rels;
        std::error_code ec;
        const auto sdir = p.dir / "scripts";
        if (std::filesystem::is_directory(sdir, ec))
            for (auto it = std::filesystem::recursive_directory_iterator(sdir, ec); !ec && it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec))
                if (it->is_regular_file() && it->path().extension() == ".luau")
                    rels.push_back(std::filesystem::relative(it->path(), p.dir).generic_string());
        std::sort(rels.begin(), rels.end());
        for (const auto& r : rels) {
            const std::string key = make_script_key(p.id, r);
            out.script_keys_.push_back(key);
            out.scripts_[key] = p.dir / r;
        }
    }
    return out;
}

} // namespace eng::assets
