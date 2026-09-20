#pragma once
#include <array>
namespace rush {
struct BossTest {
    const char* stage;
    const char* name;
    int save_table;
    int spawn;
    int layer;
    int room = 50;
};
// Entrances and layers verified against the disc's PLYR/ACT chunks.
inline constexpr std::array kBossTests{
    BossTest{"D_MN05A", "Diababa", 16, 0, 4},
    BossTest{"D_MN04A", "Fyrus", 17, 0, 0},
    BossTest{"D_MN01A", "Morpheel", 18, 0, 0},
    BossTest{"D_MN10A", "Stallord", 19, 0, 0},
    BossTest{"D_MN11A", "Blizzeta", 20, 1, 0},
    BossTest{"D_MN06A", "Armogohma", 21, 0, 0},
    BossTest{"D_MN07A", "Argorok", 22, 1, 0},
    BossTest{"D_MN08D", "Zant", 23, 0, 0},
    // Room 50 spawn 10 is the below-arena entrance (y=-1500, start mode 6).
    // A direct boss start needs the floor-level spawn and Puppet Zelda's layer.
    BossTest{"D_MN09A", "Ganondorf", 24, 0, 0},
};
inline std::array<int, 9> boss_finish_delay{7, 7, 7, 7, 7, 7, 7, 7, 7};
}  // namespace rush
