// encounter_runner [--log] [--root DIR] <scenario.json>
//
// Loads a scenario@1 file (schemas/scenario/v1.schema.json), the pack it names, and
// either that pack's chunk or an open 32x32 map. Party then monsters replace chunk
// spawns. Both sides use sim::NearestEnemyPolicy. The same seed and content always
// print the same bytes. Stdout without --log is the checked-in golden.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/pack_set.hpp"
#include "assets/schema_registry.hpp"
#include "ecs/registry.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

namespace {

struct Fail : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class EndKind { Teams, NoPc, NoPcOrFoes };

struct Actor {
    std::string name;
    std::string archetype;
    std::string team;
    int x = 0;
    int y = 0;
};

const char* kUsage =
    "usage: encounter_runner [--log] [--root DIR] <scenario.json>\n"
    "\n"
    "Load the scenario pack and run the built-in nearest-enemy policy on every\n"
    "creature until the encounter ends. Output depends only on the scenario seed\n"
    "and the pack contents.\n"
    "\n"
    "Stdout: scenario, seed, end condition, round cap, outcome, round, ended,\n"
    "teams down, PC states, hit points, surprise, Combat::hash digest (16 hex),\n"
    "and the command count. --log appends one line per sim event:\n"
    "  <index> <sim kind> <rules kind> <rules actor> <rules target> <a> <b> <c>\n"
    "  <depth> <sim actor> <tile x> <tile y>\n"
    "\n"
    "--root is the repository root used to find content/packs (default: the\n"
    "current directory). Exit 0 when the encounter ends, 1 on a load or run\n"
    "error, 2 on usage.\n";

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Fail("cannot open " + path.generic_string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int need_int(const eng::assets::Json& j, const char* key, int lo, int hi) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) throw Fail(std::string(key) + " must be an integer");
    const auto v = it->get<int64_t>();
    if (v < lo || v > hi) throw Fail(std::string(key) + " is out of range");
    return static_cast<int>(v);
}

std::string need_str(const eng::assets::Json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string() || it->get_ref<const std::string&>().empty())
        throw Fail(std::string(key) + " must be a non-empty string");
    return it->get<std::string>();
}

std::vector<Actor> read_actors(const eng::assets::Json& doc, const char* side, const char* default_team) {
    const auto it = doc.find(side);
    if (it == doc.end() || !it->is_array() || it->empty()) throw Fail(std::string(side) + " must be a non-empty array");
    std::vector<Actor> out;
    for (const auto& row : *it) {
        if (!row.is_object()) throw Fail(std::string(side) + " entries must be objects");
        Actor a;
        a.name = need_str(row, "id");
        a.archetype = need_str(row, "archetype");
        const auto tile = row.find("tile");
        if (tile == row.end() || !tile->is_object()) throw Fail(a.name + " needs a tile");
        a.x = need_int(*tile, "x", 0, 31);
        a.y = need_int(*tile, "y", 0, 31);
        a.team = row.contains("team") ? need_str(row, "team") : default_team;
        out.push_back(std::move(a));
    }
    return out;
}

EndKind parse_end(const std::string& s) {
    if (s == "teams") return EndKind::Teams;
    if (s == "no_pc_conscious") return EndKind::NoPc;
    if (s == "no_pc_or_no_opponent") return EndKind::NoPcOrFoes;
    throw Fail("end_condition must be teams, no_pc_conscious, or no_pc_or_no_opponent");
}

const char* end_name(EndKind k) {
    switch (k) {
    case EndKind::Teams: return "teams";
    case EndKind::NoPc: return "no_pc_conscious";
    case EndKind::NoPcOrFoes: return "no_pc_or_no_opponent";
    }
    return "teams";
}

const char* reason_name(rules5e::EndReason r) {
    switch (r) {
    case rules5e::EndReason::NoPcConscious: return "no_pc_conscious";
    case rules5e::EndReason::NoOpponentConscious: return "no_opponent_conscious";
    case rules5e::EndReason::Custom: return "custom";
    case rules5e::EndReason::TeamNoConscious: return "team_no_conscious";
    case rules5e::EndReason::InvalidSetup: return "invalid_setup";
    }
    return "unknown";
}

const char* pc_name(rules5e::PcState s) {
    switch (s) {
    case rules5e::PcState::Conscious: return "conscious";
    case rules5e::PcState::Dying: return "dying";
    case rules5e::PcState::Stable: return "stable";
    case rules5e::PcState::Dead: return "dead";
    case rules5e::PcState::Unconscious: return "unconscious";
    }
    return "unknown";
}

std::string name_of(const std::vector<std::pair<std::string, std::uint32_t>>& roster, std::uint32_t id) {
    for (const auto& row : roster)
        if (row.second == id) return row.first;
    return std::to_string(id);
}

std::uint32_t id_of(const std::vector<std::pair<std::string, std::uint32_t>>& roster, const std::string& name) {
    for (const auto& row : roster)
        if (row.first == name) return row.second;
    throw Fail("unknown roster id " + name);
}

std::filesystem::path find_pack(const std::filesystem::path& root, const std::string& id) {
    const auto packs = root / "content" / "packs";
    if (!std::filesystem::is_directory(packs)) throw Fail("no content/packs under " + root.generic_string());
    std::vector<std::filesystem::path> dirs;
    for (const auto& entry : std::filesystem::directory_iterator(packs))
        if (entry.is_directory()) dirs.push_back(entry.path());
    std::sort(dirs.begin(), dirs.end());
    for (const auto& dir : dirs) {
        const auto man_path = dir / "manifest.json";
        if (!std::filesystem::is_regular_file(man_path)) continue;
        const auto man = eng::assets::Json::parse(read_text(man_path), nullptr, false);
        if (man.is_object() && man.value("id", std::string{}) == id) return dir;
    }
    throw Fail("pack '" + id + "' was not found under content/packs");
}

bool path_escapes(std::string_view rel) {
    if (rel.empty() || rel.front() == '/' || rel.find('\\') != std::string_view::npos) return true;
    std::size_t i = 0;
    while (i < rel.size()) {
        const auto slash = rel.find('/', i);
        const auto part = rel.substr(i, slash == std::string_view::npos ? std::string_view::npos : slash - i);
        if (part.empty() || part == "." || part == "..") return true;
        if (slash == std::string_view::npos) break;
        i = slash + 1;
    }
    return false;
}

eng::world::Chunk load_chunk(const std::filesystem::path& pack_dir, const std::string& rel) {
    if (path_escapes(rel)) throw Fail("board.chunk must be a relative path inside the pack");
    const auto file = pack_dir / rel;
    if (!std::filesystem::is_regular_file(file)) throw Fail("board.chunk does not exist: " + rel);
    eng::assets::SchemaRegistry reg;
    eng::world::register_chunk_schema(reg);
    const auto loaded = reg.load_file(file);
    if (!loaded.ok) throw Fail(loaded.error);
    const auto decoded = eng::world::chunk_from_json(loaded.data);
    if (decoded.status != eng::world::ChunkStatus::Ready || !decoded.chunk)
        throw Fail(decoded.error.empty() ? "chunk decode failed" : decoded.error);
    return *decoded.chunk;
}

eng::world::Chunk open_map(const eng::assets::Json& map) {
    eng::world::Chunk chunk{};
    chunk.format_version = eng::world::kChunkFormatVersion;
    chunk.coord = {0, 0, 0};
    chunk.origin = eng::world::Chunk::Origin::Handcrafted;
    chunk.palette = {"", "tiles.dungeon.floor_01", "tiles.dungeon.wall_01"};
    for (auto& cell : chunk.layers[0]) cell.tile = 1;
    const auto blocked = map.find("blocked");
    if (blocked == map.end()) return chunk;
    if (!blocked->is_array()) throw Fail("board.map.blocked must be an array");
    for (const auto& cell : *blocked) {
        if (!cell.is_object()) throw Fail("blocked tile must be an object");
        const int x = need_int(cell, "x", 0, 31);
        const int y = need_int(cell, "y", 0, 31);
        auto& wall = chunk.layers[2][static_cast<std::size_t>(eng::world::cell_index(x, y))];
        if ((wall.flags & eng::world::tile_flags::kBlocksMove) != 0) throw Fail("duplicate blocked tile");
        wall.tile = 2;
        wall.flags = static_cast<std::uint8_t>(eng::world::tile_flags::kBlocksMove | eng::world::tile_flags::kBlocksSight);
    }
    return chunk;
}

void place_roster(eng::world::Chunk& chunk, const std::vector<Actor>& actors,
                  std::vector<std::pair<std::string, std::uint32_t>>& roster) {
    chunk.spawns.clear();
    for (const auto& actor : actors) {
        for (const auto& prev : roster)
            if (prev.first == actor.name) throw Fail("duplicate roster id " + actor.name);
        for (const auto& spawn : chunk.spawns)
            if (spawn.tile.x == actor.x && spawn.tile.y == actor.y)
                throw Fail("two creatures on the same tile");
        eng::world::EntitySpawn spawn;
        spawn.archetype = actor.archetype;
        spawn.tile = {actor.x, actor.y};
        spawn.stable_id = eng::world::spawn_stable_id(chunk.coord, static_cast<std::uint32_t>(chunk.spawns.size()));
        if (spawn.stable_id == 0) throw Fail("spawn stable_id 0 is reserved");
        eng::assets::Json overrides = eng::assets::Json::object();
        overrides["team"] = actor.team;
        spawn.overrides_json = overrides.dump();
        roster.emplace_back(actor.name, spawn.stable_id);
        chunk.spawns.push_back(std::move(spawn));
    }
}

void require_archetype(const eng::sim::ContentLibrary& lib, const Actor& actor, const char* expect) {
    const auto* arch = lib.find(actor.archetype);
    if (!arch) throw Fail("unknown archetype " + actor.archetype);
    if (arch->schema_type != expect) throw Fail(actor.archetype + " is not a " + expect);
}

rules5e::EndConditionFn make_end(EndKind kind, std::uint16_t cap) {
    return [kind, cap](const rules5e::EncounterState& state) -> std::optional<rules5e::EncounterOutcome> {
        std::optional<rules5e::EncounterOutcome> outcome;
        switch (kind) {
        case EndKind::Teams: outcome = rules5e::default_end_condition(state); break;
        case EndKind::NoPc: outcome = rules5e::end_when_no_pc_conscious(state); break;
        case EndKind::NoPcOrFoes: outcome = rules5e::end_when_no_pc_or_no_opponent(state); break;
        }
        if (outcome) return outcome;
        if (state.round > cap)
            return rules5e::EncounterOutcome{rules5e::EndReason::Custom, rules5e::pc_statuses(state), {}};
        return std::nullopt;
    };
}

void print_list_prefix(const char* key, bool empty) {
    std::fputs(key, stdout);
    if (empty) std::fputs(" -", stdout);
}

void print_report(const std::string& scenario_id, std::uint64_t seed, EndKind kind, std::uint16_t cap,
                  const eng::sim::Combat& combat, const rules5e::EncounterOutcome& outcome,
                  const std::vector<std::pair<std::string, std::uint32_t>>& roster, std::uint32_t commands) {
    std::printf("scenario %s\n", scenario_id.c_str());
    std::printf("seed %llu\n", static_cast<unsigned long long>(seed));
    std::printf("end_condition %s\n", end_name(kind));
    std::printf("round_cap %u\n", static_cast<unsigned>(cap));
    std::printf("outcome %s\n", reason_name(outcome.reason));
    std::printf("round %u\n", static_cast<unsigned>(combat.state().round));
    std::printf("ended %d\n", combat.ended() ? 1 : 0);

    print_list_prefix("teams_down", outcome.teams_without_conscious.empty());
    for (const std::uint8_t team : outcome.teams_without_conscious) {
        if (team < combat.team_names().size()) std::printf(" %s", combat.team_names()[team].c_str());
        else std::printf(" %u", static_cast<unsigned>(team));
    }
    std::fputc('\n', stdout);

    print_list_prefix("pcs", outcome.pcs.empty());
    for (const auto& pc : outcome.pcs) {
        const std::string name = name_of(roster, pc.id);
        std::printf(" %s:%s", name.c_str(), pc_name(pc.state));
    }
    std::fputc('\n', stdout);

    print_list_prefix("hp", combat.state().combatants.empty());
    for (const auto& combatant : combat.state().combatants) {
        const std::string name = name_of(roster, combatant.id);
        std::printf(" %s=%d", name.c_str(), static_cast<int>(combatant.hp));
    }
    std::fputc('\n', stdout);

    print_list_prefix("surprised", combat.surprised_at_start().empty());
    for (const auto id : combat.surprised_at_start()) {
        const std::string name = name_of(roster, id);
        std::printf(" %s", name.c_str());
    }
    std::fputc('\n', stdout);

    std::printf("digest %016llx\n", static_cast<unsigned long long>(combat.hash()));
    std::printf("commands %u\n", commands);
}

void print_log(const eng::sim::Combat& combat) {
    const auto& events = combat.events();
    std::printf("log %zu\n", events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i];
        std::printf("%zu %u %u %u %u %d %d %d %u %u %d %d\n", i, static_cast<unsigned>(event.kind),
                    static_cast<unsigned>(event.rules.kind), event.rules.actor, event.rules.target, event.rules.a,
                    event.rules.b, event.rules.c, static_cast<unsigned>(event.rules.reaction_depth), event.actor,
                    event.tile.x, event.tile.y);
    }
}

int run(int argc, char** argv) {
    bool log = false;
    std::filesystem::path root = std::filesystem::current_path();
    std::filesystem::path scenario_path;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        }
        if (arg == "--log") {
            log = true;
            continue;
        }
        if (arg == "--root") {
            if (i + 1 >= argc) {
                std::fputs(kUsage, stderr);
                return 2;
            }
            root = argv[++i];
            continue;
        }
        if (arg.starts_with('-')) {
            std::fputs(kUsage, stderr);
            return 2;
        }
        if (!scenario_path.empty()) {
            std::fputs(kUsage, stderr);
            return 2;
        }
        scenario_path = arg;
    }
    if (scenario_path.empty()) {
        std::fputs(kUsage, stderr);
        return 2;
    }

    const auto doc = eng::assets::Json::parse(read_text(scenario_path));
    if (!doc.is_object()) throw Fail("scenario must be a JSON object");
    if (eng::assets::contains_float(doc)) throw Fail("floating-point numbers are not allowed");
    const std::string scenario_id = need_str(doc, "id");
    const auto seed_it = doc.find("seed");
    if (seed_it == doc.end() || !seed_it->is_number_unsigned()) throw Fail("seed must be an integer >= 0");
    const auto seed = seed_it->get<std::uint64_t>();
    const std::string pack_id = need_str(doc, "pack");
    const auto end = parse_end(need_str(doc, "end_condition"));
    const auto cap_it = doc.find("round_cap");
    if (cap_it == doc.end() || !cap_it->is_number_integer()) throw Fail("round_cap must be an integer");
    const auto cap64 = cap_it->get<int64_t>();
    if (cap64 < 1 || cap64 > 10000) throw Fail("round_cap must be 1..10000");
    const auto cap = static_cast<std::uint16_t>(cap64);

    const auto board_it = doc.find("board");
    if (board_it == doc.end() || !board_it->is_object()) throw Fail("board is required");
    const bool has_chunk = board_it->contains("chunk");
    const bool has_map = board_it->contains("map");
    if (has_chunk == has_map) throw Fail("board needs exactly one of chunk or map");

    auto party = read_actors(doc, "party", "party");
    auto monsters = read_actors(doc, "monsters", "monsters");
    std::vector<std::string> teams;
    for (const auto& actor : party) teams.push_back(actor.team);
    for (const auto& actor : monsters) teams.push_back(actor.team);
    std::sort(teams.begin(), teams.end());
    teams.erase(std::unique(teams.begin(), teams.end()), teams.end());
    if (teams.size() < 2) throw Fail("party and monsters must cover at least two teams");

    const auto pack_dir = find_pack(root, pack_id);
    eng::sim::ContentLibrary library;
    const auto errors = library.load_pack(pack_dir.string());
    if (!errors.empty()) {
        std::string message;
        for (const auto& error : errors) message += error + "\n";
        throw Fail(message);
    }
    for (const auto& actor : party) require_archetype(library, actor, "character");
    for (const auto& actor : monsters) require_archetype(library, actor, "monster");

    eng::world::Chunk chunk = [&] {
        if (has_chunk) return load_chunk(pack_dir, need_str(*board_it, "chunk"));
        const auto& map = board_it->at("map");
        if (!map.is_object()) throw Fail("board.map must be an object");
        return open_map(map);
    }();

    std::vector<Actor> roster_actors = party;
    roster_actors.insert(roster_actors.end(), monsters.begin(), monsters.end());
    std::vector<std::pair<std::string, std::uint32_t>> roster;
    place_roster(chunk, roster_actors, roster);

    eng::ecs::Registry entities;
    std::string create_error;
    auto combat = eng::sim::Combat::create(entities, chunk, library, seed, &create_error);
    if (!combat) throw Fail(create_error.empty() ? "failed to create the encounter" : create_error);
    for (const auto& actor : roster_actors)
        if (!combat->grid().walkable({actor.x, actor.y}))
            throw Fail(actor.name + " is not standing on walkable ground");

    const auto surprise = doc.find("surprise");
    if (surprise == doc.end()) throw Fail("surprise is required");
    std::vector<rules5e::CombatantId> surprised;
    if (surprise->is_array()) {
        for (const auto& who : *surprise) {
            if (!who.is_string()) throw Fail("surprise entries must be roster ids");
            surprised.push_back(id_of(roster, who.get<std::string>()));
        }
    } else if (surprise->is_object()) {
        const auto hiders_it = surprise->find("hiders");
        if (hiders_it == surprise->end() || !hiders_it->is_array()) throw Fail("surprise.hiders must be an array");
        std::vector<rules5e::StealthTotal> hiders;
        for (const auto& hider : *hiders_it) {
            if (!hider.is_object()) throw Fail("surprise hider must be an object");
            rules5e::StealthTotal total;
            total.id = id_of(roster, need_str(hider, "who"));
            total.total = static_cast<std::int16_t>(need_int(hider, "stealth", -32768, 32767));
            hiders.push_back(total);
        }
        surprised = combat->surprised_by_stealth(hiders);
    } else {
        throw Fail("surprise must be a list of roster ids or {\"hiders\": [...]}");
    }
    if (!surprised.empty()) {
        std::string surprise_error;
        if (!combat->apply_surprise(surprised, &surprise_error))
            throw Fail(surprise_error.empty() ? "apply_surprise failed" : surprise_error);
    }

    combat->set_end_condition(make_end(end, cap));
    eng::sim::NearestEnemyPolicy policy;
    const auto result = eng::sim::run_to_end(*combat, [&](rules5e::CombatantId) { return &policy; });
    if (!result.error.empty()) throw Fail(result.error);
    if (!combat->ended() || !combat->state().outcome) throw Fail("the encounter did not end");

    print_report(scenario_id, seed, end, cap, *combat, *combat->state().outcome, roster, result.commands);
    if (log) print_log(*combat);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const Fail& error) {
        std::fprintf(stderr, "encounter_runner: %s\n", error.what());
        return 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "encounter_runner: %s\n", error.what());
        return 1;
    }
}
