#include "presentation/equip_art.hpp"

#include "presentation/standin.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fstream>
#include <optional>
#include <utility>

namespace eng::presentation {

namespace {

std::optional<ImageRgba> read_png(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n <= 0) return std::nullopt;
    std::vector<char> bytes(static_cast<size_t>(n));
    in.seekg(0);
    in.read(bytes.data(), n);
    if (!in) return std::nullopt;
    return decode_png(std::as_bytes(std::span<const char>(bytes.data(), bytes.size())));
}

bool known_bin(std::string_view bin) { return standin_canvas(bin).w != 0; }

std::optional<EquipArtPart> parse_part(std::string_view s) {
    if (s == "back") return EquipArtPart::Back;
    if (s == "hip") return EquipArtPart::Hip;
    if (s == "main_hand") return EquipArtPart::MainHand;
    if (s == "off_hand") return EquipArtPart::OffHand;
    if (s == "held_light") return EquipArtPart::HeldLight;
    if (s == "neck") return EquipArtPart::Neck;
    return std::nullopt;
}

std::optional<EquipArtView> parse_view(std::string_view s) {
    if (s == "se") return EquipArtView::Toward;
    if (s == "ne") return EquipArtView::Away;
    return std::nullopt;
}

bool parse_sprite_name(const std::filesystem::path& file, EquipArtPart& part, EquipArtView& view) {
    if (file.extension() != ".png") return false;
    const std::string stem = file.stem().string();
    const auto dot = stem.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= stem.size()) return false;
    const auto p = parse_part(std::string_view(stem).substr(0, dot));
    const auto v = parse_view(std::string_view(stem).substr(dot + 1));
    if (!p || !v) return false;
    // neck is toward-only. The schema accepts neck.se and rejects neck.ne.
    if (*p == EquipArtPart::Neck && *v == EquipArtView::Away) return false;
    part = *p;
    view = *v;
    return true;
}

std::vector<std::filesystem::path> sorted_children(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (ec) break;
        out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool is_dir(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_directory(p, ec);
}

bool is_file(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

bool known_kind(std::string_view kind) {
    return kind == "weapon" || kind == "shield" || kind == "light" || kind == "other";
}

// kit.json pivot list. A file this parser cannot read yields no overrides; the
// validator is what rejects a bad manifest. Sprites still load.
enum class JsonTag : uint8_t { Null, Bool, Number, String, Array, Object };

struct Json {
    JsonTag tag = JsonTag::Null;
    bool b = false;
    int64_t n = 0;
    std::string s;
    std::vector<Json> a;
    // Parallel arrays, not pair<string, Json>: libstdc++ 14 rejects a
    // std::pair member while Json is still incomplete (Clang -Werror).
    std::vector<std::string> keys;
    std::vector<Json> vals;
};

const Json* field(const Json& j, std::string_view key) {
    if (j.tag != JsonTag::Object) return nullptr;
    for (std::size_t n = 0; n < j.keys.size(); ++n)
        if (j.keys[n] == key) return &j.vals[n];
    return nullptr;
}

class KitParser {
public:
    explicit KitParser(std::string_view text) : text_(text) {}

    std::optional<Json> parse() {
        Json root;
        if (!value(root) || !ok_) return std::nullopt;
        skip();
        if (i_ != text_.size()) return std::nullopt;
        return root;
    }

private:
    std::string_view text_;
    std::size_t i_ = 0;
    bool ok_ = true;

    bool fail() { ok_ = false; return false; }
    void skip() {
        while (i_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[i_]))) ++i_;
    }
    bool eat(char c) {
        skip();
        if (i_ >= text_.size() || text_[i_] != c) return fail();
        ++i_;
        return true;
    }
    bool value(Json& out) {
        skip();
        if (i_ >= text_.size()) return fail();
        const char c = text_[i_];
        if (c == '{') return object(out);
        if (c == '[') return array(out);
        if (c == '"') return string(out);
        if (c == 't' || c == 'f') return boolean(out);
        if (c == 'n') return null_value(out);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return number(out);
        return fail();
    }
    bool object(Json& out) {
        if (!eat('{')) return false;
        out.tag = JsonTag::Object;
        skip();
        if (i_ < text_.size() && text_[i_] == '}') { ++i_; return true; }
        while (ok_) {
            Json key;
            if (!string(key)) return false;
            if (!eat(':')) return false;
            Json child;
            if (!value(child)) return false;
            out.keys.push_back(std::move(key.s));
            out.vals.push_back(std::move(child));
            skip();
            if (i_ < text_.size() && text_[i_] == ',') { ++i_; continue; }
            return eat('}');
        }
        return false;
    }
    bool array(Json& out) {
        if (!eat('[')) return false;
        out.tag = JsonTag::Array;
        skip();
        if (i_ < text_.size() && text_[i_] == ']') { ++i_; return true; }
        while (ok_) {
            Json child;
            if (!value(child)) return false;
            out.a.push_back(std::move(child));
            skip();
            if (i_ < text_.size() && text_[i_] == ',') { ++i_; continue; }
            return eat(']');
        }
        return false;
    }
    bool string(Json& out) {
        skip();
        if (i_ >= text_.size() || text_[i_] != '"') return fail();
        ++i_;
        out.tag = JsonTag::String;
        while (i_ < text_.size()) {
            const char c = text_[i_++];
            if (c == '"') return true;
            if (c != '\\') { out.s.push_back(c); continue; }
            if (i_ >= text_.size()) return fail();
            const char e = text_[i_++];
            if (e == '"' || e == '\\' || e == '/') out.s.push_back(e);
            else if (e == 'n') out.s.push_back('\n');
            else if (e == 't') out.s.push_back('\t');
            else if (e == 'r') out.s.push_back('\r');
            else return fail();
        }
        return fail();
    }
    bool take(std::string_view lit) {
        skip();
        if (i_ > text_.size() || text_.size() - i_ < lit.size()) return false;
        if (text_.substr(i_, lit.size()) != lit) return false;
        i_ += lit.size();
        return true;
    }
    bool boolean(Json& out) {
        if (take("true")) { out.tag = JsonTag::Bool; out.b = true; return true; }
        if (take("false")) { out.tag = JsonTag::Bool; out.b = false; return true; }
        return fail();
    }
    bool null_value(Json& out) {
        if (!take("null")) return fail();
        out.tag = JsonTag::Null;
        return true;
    }
    bool number(Json& out) {
        skip();
        const std::size_t start = i_;
        if (i_ < text_.size() && text_[i_] == '-') ++i_;
        if (i_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[i_]))) return fail();
        while (i_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[i_]))) ++i_;
        if (i_ < text_.size() && (text_[i_] == '.' || text_[i_] == 'e' || text_[i_] == 'E')) return fail();
        int64_t n = 0;
        const auto res = std::from_chars(text_.data() + start, text_.data() + i_, n);
        if (res.ec != std::errc()) return fail();
        out.tag = JsonTag::Number;
        out.n = n;
        return true;
    }
};

struct PivotSpec {
    bool kind_default = false;
    std::string id;
    std::string bin = "medium";
    EquipArtPart part = EquipArtPart::MainHand;
    EquipArtView view = EquipArtView::Toward;
    int x = 0;
    int y = 0;
};

void collect_parts(bool kind_default, const std::string& id, const Json& parts, std::vector<PivotSpec>& out) {
    if (parts.tag != JsonTag::Array) return;
    for (const Json& part : parts.a) {
        const Json* part_name = field(part, "part");
        const Json* views = field(part, "views");
        if (!part_name || part_name->tag != JsonTag::String || !views || views->tag != JsonTag::Array) continue;
        const auto parsed_part = parse_part(part_name->s);
        if (!parsed_part) continue;   // a part name this table does not draw
        std::string bin = "medium";
        if (const Json* named = field(part, "bin")) {
            if (named->tag == JsonTag::String && !named->s.empty()) bin = named->s;
        }
        for (const Json& view : views->a) {
            const Json* view_name = field(view, "view");
            const Json* pivot = field(view, "pivot");
            if (!view_name || view_name->tag != JsonTag::String || !pivot || pivot->tag != JsonTag::Array) continue;
            if (pivot->a.size() != 2 || pivot->a[0].tag != JsonTag::Number || pivot->a[1].tag != JsonTag::Number) continue;
            const auto parsed_view = parse_view(view_name->s);
            if (!parsed_view) continue;
            if (*parsed_part == EquipArtPart::Neck && *parsed_view == EquipArtView::Away) continue;
            if (pivot->a[0].n < 0 || pivot->a[1].n < 0 || pivot->a[0].n > 100000 || pivot->a[1].n > 100000) continue;
            PivotSpec spec;
            spec.kind_default = kind_default;
            spec.id = id;
            spec.bin = bin;
            spec.part = *parsed_part;
            spec.view = *parsed_view;
            spec.x = static_cast<int>(pivot->a[0].n);
            spec.y = static_cast<int>(pivot->a[1].n);
            out.push_back(std::move(spec));
        }
    }
}

bool known_back_role(std::string_view role) {
    return role == "quiver" || role == "bolt_case" || role == "pack" || role == "other";
}

struct KitNotes {
    std::vector<PivotSpec> pivots;
    std::vector<std::pair<std::string, std::string>> roles;
};

KitNotes notes_in(const Json& root) {
    KitNotes notes;
    if (const Json* items = field(root, "items")) {
        if (items->tag == JsonTag::Array) {
            for (const Json& item : items->a) {
                const Json* cid = field(item, "content_id");
                const Json* parts = field(item, "parts");
                if (!cid || cid->tag != JsonTag::String) continue;
                if (const Json* role = field(item, "back_role")) {
                    if (role->tag == JsonTag::String && known_back_role(role->s))
                        notes.roles.emplace_back(cid->s, role->s);
                }
                if (parts) collect_parts(false, cid->s, *parts, notes.pivots);
            }
        }
    }
    if (const Json* kinds = field(root, "kinds")) {
        if (kinds->tag == JsonTag::Array) {
            for (const Json& kind : kinds->a) {
                const Json* name = field(kind, "kind");
                const Json* parts = field(kind, "parts");
                if (!name || name->tag != JsonTag::String || !parts) continue;
                collect_parts(true, name->s, *parts, notes.pivots);
            }
        }
    }
    return notes;
}

std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(0, std::ios::end);
    const auto n = in.tellg();
    if (n < 0) return std::nullopt;
    std::string text(static_cast<std::size_t>(n), '\0');
    in.seekg(0);
    if (n > 0) in.read(text.data(), n);
    if (!in) return std::nullopt;
    return text;
}

KitNotes load_notes(const std::filesystem::path& root) {
    const auto text = read_text(root / "kit.json");
    if (!text) return {};
    KitParser parser(*text);
    const auto root_json = parser.parse();
    if (!root_json) return {};
    return notes_in(*root_json);
}

} // namespace

ImageRgba mirror_sprite_around_pivot(const ImageRgba& src, int pivot_x) {
    ImageRgba d;
    d.w = src.w;
    d.h = src.h;
    if (src.empty() || pivot_x < 0 || uint32_t(pivot_x) >= src.w) return d;
    d.rgba.assign(src.rgba.size(), 0);
    for (uint32_t y = 0; y < src.h; ++y) {
        for (uint32_t x = 0; x < src.w; ++x) {
            const int from = pivot_x - (int(x) - pivot_x);
            if (from < 0 || uint32_t(from) >= src.w) continue;
            std::memcpy(d.px(x, y), src.px(uint32_t(from), y), 4);
        }
    }
    return d;
}

std::optional<std::string_view> EquipArtTable::back_role(std::string_view content_id) const {
    for (const auto& role : roles_)
        if (role.first == content_id) return std::string_view(role.second);
    return std::nullopt;
}

const EquipArtSprite* EquipArtTable::sprite_at(std::size_t index) const {
    return index < sprites_.size() ? &sprites_[index] : nullptr;
}

const EquipArtSprite* EquipArtTable::lookup(bool kind_default, std::string_view id, std::string_view bin,
                                            EquipArtPart part, EquipArtView view) const {
    const EquipArtSprite* found = nullptr;
    for (const Entry& e : entries_) {
        if (e.kind_default != kind_default || e.bin != bin || e.part != part || e.view != view) continue;
        if (e.id != id) continue;
        if (e.sprite < sprites_.size()) found = &sprites_[e.sprite];
    }
    return found;
}

const EquipArtSprite* EquipArtTable::find(const EquipArtQuery& query) const {
    std::string_view bin = query.size_bin.empty() ? std::string_view("medium") : query.size_bin;
    if (!known_bin(bin)) bin = "medium";
    if (bin != "medium") {
        if (const EquipArtSprite* s = lookup(false, query.content_id, bin, query.part, query.view)) return s;
    }
    if (const EquipArtSprite* s = lookup(false, query.content_id, "medium", query.part, query.view)) return s;
    if (!query.kind.empty()) {
        if (const EquipArtSprite* s = lookup(true, query.kind, "medium", query.part, query.view)) return s;
    }
    return nullptr;
}

EquipArtTable EquipArtTable::load(const std::filesystem::path& root) {
    if (!is_dir(root)) return {};
    EquipArtTable table;
    auto add = [&](bool kind_default, std::string id, std::string bin, const std::filesystem::path& file) {
        EquipArtPart part;
        EquipArtView view;
        if (!parse_sprite_name(file, part, view)) return;
        auto image = read_png(file);
        if (!image || image->empty()) return;
        EquipArtSprite sprite;
        // Absent kit.json pivot: bottom-centre. H4 will name [12, 12] in the manifest.
        sprite.pivot_x = int(image->w / 2);
        sprite.pivot_y = int(image->h - 1);
        sprite.image = std::move(*image);
        sprite.key = uint32_t(table.sprites_.size());
        table.entries_.push_back(Entry{kind_default, std::move(id), std::move(bin), part, view, sprite.key});
        table.sprites_.push_back(std::move(sprite));
    };
    for (const std::filesystem::path& child : sorted_children(root)) {
        if (!is_dir(child)) continue;
        const std::string name = child.filename().string();
        if (name == "_kind") {
            for (const std::filesystem::path& kind_dir : sorted_children(child)) {
                if (!is_dir(kind_dir)) continue;
                const std::string kind = kind_dir.filename().string();
                if (!known_kind(kind)) continue;
                for (const std::filesystem::path& file : sorted_children(kind_dir)) {
                    if (is_file(file)) add(true, kind, "medium", file);
                }
            }
            continue;
        }
        // Flat files are the medium bin. Bin directories are applied after them, so
        // {content_id}/medium/ overrides a flat file of the same part and view, and
        // the result does not depend on directory sort order.
        const std::vector<std::filesystem::path> item_children = sorted_children(child);
        for (const std::filesystem::path& item_child : item_children) {
            if (is_file(item_child)) add(false, name, "medium", item_child);
        }
        for (const std::filesystem::path& item_child : item_children) {
            if (!is_dir(item_child)) continue;
            const std::string bin = item_child.filename().string();
            if (!known_bin(bin)) continue;
            for (const std::filesystem::path& file : sorted_children(item_child)) {
                if (is_file(file)) add(false, name, bin, file);
            }
        }
    }
    const KitNotes notes = load_notes(root);
    table.roles_ = notes.roles;
    for (const PivotSpec& spec : notes.pivots) {
        for (Entry& entry : table.entries_) {
            if (entry.kind_default != spec.kind_default || entry.id != spec.id || entry.bin != spec.bin) continue;
            if (entry.part != spec.part || entry.view != spec.view) continue;
            if (entry.sprite >= table.sprites_.size()) continue;
            EquipArtSprite& sprite = table.sprites_[entry.sprite];
            if (spec.x < 0 || spec.y < 0) continue;
            if (uint32_t(spec.x) >= sprite.image.w || uint32_t(spec.y) >= sprite.image.h) continue;
            sprite.pivot_x = spec.x;
            sprite.pivot_y = spec.y;
        }
    }
    return table;
}

} // namespace eng::presentation
