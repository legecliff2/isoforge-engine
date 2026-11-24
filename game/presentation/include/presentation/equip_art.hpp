#pragma once
// Equipment art kit (render side). Loose PNGs, loaded once, the same way a stand-in
// kit loads frames. There is no manifest here: the content pack and its schema are
// a later change. A missing root, or a missing file, is not a checker. The caller
// falls through to the next key, and then to the placeholder quads.
//
// Layout under the kit root:
//   {content_id}/{part}.{view}.png                         medium, the default bin
//   {content_id}/{bin}/{part}.{view}.png                   size override
//   _kind/{weapon|shield|light|other}/{part}.{view}.png    per-kind default (medium)
// part: back, hip, main_hand, off_hand, held_light
// view: se (toward), ne (away). West is a mirror of that view, not its own file.
// bin: tiny, small, medium, large, huge. A `{bin}` file wins over the flat medium
// file for that same bin. Any other directory name is ignored.
//
// Resolution for one part and view:
//   1. content_id + the requested bin (empty bin means medium)
//   2. content_id + medium
//   3. the kind default
//   4. nothing (placeholder quads)
// Pivot is the bottom-centre pixel (w/2, h-1) until a manifest names one. It is
// the point that lands on the slot's attach point.
#include "presentation/png.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace eng::presentation {

enum class EquipArtView : uint8_t { Toward, Away };   // se, ne
enum class EquipArtPart : uint8_t { Back, Hip, MainHand, OffHand, HeldLight };

struct EquipArtSprite {
    ImageRgba image;
    int pivot_x = 0;     // column that sits on the attach point
    int pivot_y = 0;     // row that sits on the attach point
    uint32_t key = 0;    // index in the table; stable after load
};

struct EquipArtQuery {
    std::string_view content_id;
    std::string_view kind;          // "weapon", "shield", "light", "other"
    EquipArtPart part = EquipArtPart::MainHand;
    EquipArtView view = EquipArtView::Toward;
    std::string_view size_bin;      // empty, or tiny|small|medium|large|huge
};

class EquipArtTable {
public:
    // Missing root, or a root with no readable sprites, is an empty table.
    static EquipArtTable load(const std::filesystem::path& root);

    const EquipArtSprite* find(const EquipArtQuery& query) const;
    const EquipArtSprite* sprite_at(std::size_t index) const;
    std::size_t size() const { return sprites_.size(); }

private:
    struct Entry {
        bool kind_default = false;
        std::string id;
        std::string bin;
        EquipArtPart part = EquipArtPart::MainHand;
        EquipArtView view = EquipArtView::Toward;
        uint32_t sprite = 0;
    };
    std::vector<EquipArtSprite> sprites_;
    std::vector<Entry> entries_;
    const EquipArtSprite* lookup(bool kind_default, std::string_view id, std::string_view bin, EquipArtPart part,
                                 EquipArtView view) const;
};

// Horizontal mirror about `pivot_x`. That column is copied onto itself. A column
// whose source would fall outside the image stays clear. For an even width and
// pivot w/2 this is the stand-in hflip_pivot (clear column 0, pivot stays).
ImageRgba mirror_sprite_around_pivot(const ImageRgba& src, int pivot_x);

} // namespace eng::presentation
