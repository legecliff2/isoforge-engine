#include "presentation/equip_art.hpp"

#include "presentation/standin.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
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
        // Seam: PR B's optional pivot [x, y] per part per view. Absent, bottom-centre.
        // H4's provisional pivot is [12, 20]. Not read until that manifest is wired.
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
    return table;
}

} // namespace eng::presentation
