#pragma once
#include <array>
#include <string_view>
namespace rush {
struct DungeonStart {
    const char* stage;
    int room, spawn, layer;
    const char* boss;
};
// Normal dungeon entrances from the disc's exterior SCLS records, verified
// against destination PLYR entries. Layers -1 use the game's story selection.
inline constexpr std::array kStarts{
    DungeonStart{"D_MN05", 22, 0, -1, "Diababa"},   DungeonStart{"D_MN04", 1, 1, -1, "Fyrus"},
    DungeonStart{"D_MN01", 0, 0, -1, "Morpheel"},   DungeonStart{"D_MN10", 0, 0, -1, "Stallord"},
    DungeonStart{"D_MN11", 0, 1, -1, "Blizzeta"},   DungeonStart{"D_MN06", 0, 0, -1, "Armogohma"},
    DungeonStart{"D_MN07", 0, 2, 0, "Argorok"},     DungeonStart{"D_MN08", 0, 10, 14, "Zant"},
    DungeonStart{"D_MN09", 11, 0, -1, "Ganondorf"},
};
inline bool dungeon_stage(std::size_t dungeon, std::string_view stage) {
    if (dungeon >= kStarts.size()) {
        return false;
    }
    auto main = std::string_view(kStarts[dungeon].stage);
    if (stage == main || (stage.size() == main.size() + 1 && stage.starts_with(main) &&
                          stage.back() >= 'A' && stage.back() <= 'D')) {
        return true;
    }
    // Castle's final encounter moves to the field; the cutscene transition
    // travels through R_SP300 before entering D_MN09B.
    return dungeon == 8 && (stage == "R_SP300" || stage == "F_SP121");
}
}  // namespace rush
