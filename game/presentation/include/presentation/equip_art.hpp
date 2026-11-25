#pragma once
// Equipment art kit (render side). Loose PNGs, loaded once, the same way a stand-in
// kit loads frames. kit.json (equip_kit@1) beside the files may name one pivot per
// sprite. A missing pivot is the bottom-centre pixel (w/2, h-1). A missing root,
// a missing file, or a kit.json this loader cannot read is not a checker. The
// caller falls through to the next key, and then to the placeholder quads.
//
// Layout under the kit root (unchanged):
//   {content_id}/{part}.{view}.png                         medium, the default bin
//   {content_id}/{bin}/{part}.{view}.png                   size override
//   _kind/{weapon|shield|light|other}/{part}.{view}.png    per-kind default (medium)
// part: back, hip, main_hand, off_hand, held_light, neck
// view: se (toward), ne (away). West is a mirror of that view, not its own file.
// neck is toward-only: neck.se is loaded, neck.ne is rejected. The hip part is the
// sheathed view and has no side; the renderer assigns hip-main or hip-off.
// bin: tiny, small, medium, large, huge. A `{bin}` file wins over the flat medium
// file for that same bin. Any other directory name is ignored.
//
// Resolution for one part and view:
//   1. content_id + the requested bin (empty bin means medium)
//   2. content_id + medium
//   3. the kind default
//   4. nothing (placeholder quads). A missing file is not a checker.
// Content ids are matched whole. A quiver file is not a bolt-case file.
// kit.json may set one pivot [x, y] per sprite that exists. Absent, or outside
// the cell, the pivot stays bottom-centre (w/2, h-1). H4 cells are 24x24 and
// H4's provisional pivot is [12, 20] where that sprite is on disk.
// Optional back_role (quiver | bolt_case | pack | other) is stored per content id.
#include "presentation/png.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eng::presentation {

enum class EquipArtView : uint8_t { Toward, Away };   // se, ne
enum class EquipArtPart : uint8_t { Back, Hip, MainHand, OffHand, HeldLight, Neck };

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
    // equip_kit@1 items[].back_role, matched on the whole content id. nullopt when
    // the field is absent, the manifest was not read, or the value is not the enum.
    std::optional<std::string_view> back_role(std::string_view content_id) const;

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
    std::vector<std::pair<std::string, std::string>> roles_;
    const EquipArtSprite* lookup(bool kind_default, std::string_view id, std::string_view bin, EquipArtPart part,
                                 EquipArtView view) const;
};

// Horizontal mirror about `pivot_x`. That column is copied onto itself. A column
// whose source would fall outside the image stays clear. For an even width and
// pivot w/2 this is the stand-in hflip_pivot (clear column 0, pivot stays).
ImageRgba mirror_sprite_around_pivot(const ImageRgba& src, int pivot_x);

} // namespace eng::presentation
