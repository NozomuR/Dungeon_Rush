#pragma once
#include <array>
#include <cstdint>

namespace rush {
// Ruleset choices, not an assertion that every campaign owns optional items.
// Native item names are misleading: 0x2a is Ordon, 0x2b is the shop Wooden Shield.
struct ShieldPreset {
    const char* name;
    const char* icon;
    uint8_t item;
    uint8_t collect;
};
inline constexpr std::array kShields{
    ShieldPreset{"Ordon Shield", "2a", 0x2a, 0},
    ShieldPreset{"Wooden Shield", "2b", 0x2b, 1},
    ShieldPreset{"Hylian Shield", "2c", 0x2c, 2},
};
inline std::size_t forest_shield = 0;
inline constexpr char kRuleset[] = "standard-preview-v1";
enum Gear : uint32_t {
    Lantern = 1u << 0,
    Slingshot = 1u << 1,
    FishingRod = 1u << 2,
    Boomerang = 1u << 3,
    IronBoots = 1u << 4,
    Bow = 1u << 5,
    Bombs = 1u << 6,
    WaterBombs = 1u << 7,
    Clawshot = 1u << 8,
    Spinner = 1u << 9,
    BallChain = 1u << 10,
    DominionRod = 1u << 11,
    DoubleClawshots = 1u << 12,
    ZoraArmor = 1u << 13,
    WolfForm = 1u << 14,
    LightSword = 1u << 15,
};
constexpr uint32_t normalize_gear(uint32_t gear) {
    return (gear & DoubleClawshots) ? gear & ~Clawshot : gear;
}
static_assert(normalize_gear(Clawshot | DoubleClawshots | Bow) == (DoubleClawshots | Bow));
static_assert(normalize_gear(Clawshot | Bow) == (Clawshot | Bow));
struct GearLabel {
    Gear bit;
    const char* name;
};
inline constexpr std::array kGearLabels{
    GearLabel{Lantern, "Lantern"},
    GearLabel{Slingshot, "Slingshot"},
    GearLabel{FishingRod, "Fishing Rod"},
    GearLabel{Boomerang, "Gale Boomerang"},
    GearLabel{IronBoots, "Iron Boots"},
    GearLabel{Bow, "Hero's Bow"},
    GearLabel{Bombs, "Bomb bag + bombs"},
    GearLabel{WaterBombs, "Water bombs"},
    GearLabel{Clawshot, "Clawshot (single)"},
    GearLabel{Spinner, "Spinner"},
    GearLabel{BallChain, "Ball and Chain"},
    GearLabel{DominionRod, "Dominion Rod (restored)"},
    GearLabel{DoubleClawshots, "Double Clawshots"},
    GearLabel{ZoraArmor, "Zora Armor"},
    GearLabel{WolfForm, "Wolf transformation"},
};
struct Dungeon {
    const char* id;
    const char* name;
    const char* sword;
    const char* shield;
    uint32_t gear;
    uint32_t reward;
    const char* reward_name;
    const char* note;
};
inline constexpr uint32_t forest = Lantern | Slingshot | FishingRod;
inline uint32_t forest_gear = forest;
inline int forest_hearts = 3;
inline bool forest_master_sword = false;
struct InventoryPreset {
    Gear bit;
    uint8_t item, slot;
};
inline constexpr std::array kInventory{
    InventoryPreset{Lantern, 0x48, 1},
    InventoryPreset{Slingshot, 0x4b, 23},
    InventoryPreset{FishingRod, 0x4a, 20},
    InventoryPreset{Boomerang, 0x40, 0},
    InventoryPreset{Spinner, 0x41, 2},
    InventoryPreset{IronBoots, 0x45, 3},
    InventoryPreset{Bow, 0x43, 4},
    InventoryPreset{BallChain, 0x42, 6},
    InventoryPreset{DominionRod, 0x46, 8},
    InventoryPreset{Clawshot, 0x44, 9},
    InventoryPreset{DoubleClawshots, 0x47, 10},
    InventoryPreset{Bombs, 0x70, 15},
    InventoryPreset{WaterBombs, 0x71, 16},
};
constexpr std::array<int, 2> resolve_equipment(uint32_t gear, std::array<int, 2> preferred,
                                               std::array<int, 2> defaults) {
    gear = normalize_gear(gear);
    auto valid = [&](int slot) {
        if (slot == 11 || slot == 255) {
            return true;
        }
        for (const auto& item : kInventory) {
            if (item.slot == slot && (gear & item.bit)) {
                return true;
            }
        }
        return false;
    };
    auto result = defaults;
    for (int i = 0; i < 2; ++i) {
        int slot = preferred[i];
        if (slot == 9 && (gear & DoubleClawshots)) {
            slot = 10;
        }
        if (slot == -1) {
            result[i] = 255;
        } else if (slot >= 0 && valid(slot)) {
            result[i] = slot;
        }
        if (!valid(result[i])) {
            result[i] = 255;
        }
    }
    if (result[0] != 255 && result[0] == result[1]) {
        result[preferred[0] == -2 && preferred[1] >= 0 ? 0 : 1] = 255;
    }
    return result;
}
static_assert(resolve_equipment(DoubleClawshots, {9, -1}, {23, 1}) == std::array<int, 2>{10, 255});
static_assert(resolve_equipment(Lantern, {-2, 1}, {1, 255}) == std::array<int, 2>{255, 1});
static_assert(resolve_equipment(Bow, {4, 4}, {255, 255}) == std::array<int, 2>{4, 255});
static_assert(resolve_equipment(Lantern, {4, -2}, {1, 255}) == std::array<int, 2>{1, 255});
inline constexpr uint32_t mines = forest | Boomerang | IronBoots;
inline constexpr uint32_t lakebed = mines | Bow | Bombs | WaterBombs | ZoraArmor;
inline constexpr uint32_t arbiter = lakebed | Clawshot | WolfForm;
inline constexpr uint32_t snowpeak = arbiter | Spinner;
inline constexpr uint32_t time = snowpeak | BallChain;
inline constexpr uint32_t sky = time | DominionRod;
inline constexpr uint32_t palace = (sky & ~Clawshot) | DoubleClawshots;
inline constexpr std::array kDungeons{
    Dungeon{"forest", "Forest Temple", "Ordon Sword", "Ordon Shield", forest, Boomerang,
            "Gale Boomerang", "The boomerang is earned inside this dungeon."},
    Dungeon{"mines", "Goron Mines", "Ordon Sword", "Hylian Shield", mines, Bow, "Hero's Bow",
            "Iron Boots are available before entry. Hylian Shield is a ruleset allowance."},
    Dungeon{
        "lakebed", "Lakebed Temple", "Ordon Sword", "Hylian Shield", lakebed, Clawshot, "Clawshot",
        "Zora Armor and Iron Boots are available before entry. Bomb bag counts remain to be tuned."},
    Dungeon{"arbiter", "Arbiter's Grounds", "Master Sword", "Hylian Shield", arbiter, Spinner,
            "Spinner", "Wolf transformation and senses are required for the Poe sequence."},
    Dungeon{"snowpeak", "Snowpeak Ruins", "Master Sword", "Hylian Shield", snowpeak, BallChain,
            "Ball and Chain",
            "Ingredient, cannon and room state must start fresh in a playable run."},
    Dungeon{"time", "Temple of Time", "Master Sword", "Hylian Shield", time, DominionRod,
            "Dominion Rod",
            "The Dominion Rod is earned here; do not grant the statue-control reward early."},
    Dungeon{"sky", "City in the Sky", "Master Sword", "Hylian Shield", sky, DoubleClawshots,
            "Double Clawshots",
            "Start with one Clawshot. The restored Dominion Rod is part of prior progression."},
    Dungeon{"palace", "Palace of Twilight", "Master Sword", "Hylian Shield", palace, LightSword,
            "Light infusion for the Master Sword",
            "Recover both Sols to earn the light infusion inside this dungeon."},
    Dungeon{
        "castle", "Hyrule Castle", "Master Sword", "Hylian Shield", palace, 0, "None",
        "The Palace light infusion is local to that dungeon. Final boss boundaries need separate validation."},
};
constexpr int standard_hearts(std::size_t dungeon) {
    return 3 + static_cast<int>(dungeon);
}

struct Loadout {
    uint32_t gear;
    int hearts;
    bool master;
    std::size_t shield;
    std::array<int, 2> equipped{-2, -2};
};
inline Loadout default_loadout(std::size_t d) {
    return {kDungeons[d].gear, standard_hearts(d), d >= 3, d == 0 ? 0u : 2u};
}
inline std::array<Loadout, 9> loadouts = [] {
    std::array<Loadout, 9> a{};
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = default_loadout(i);
    }
    return a;
}();
// Preserve the defining rush rules when editing presets.
constexpr bool valid_presets() {
    for (std::size_t i = 0; i < kDungeons.size(); ++i) {
        const auto& d = kDungeons[i];
        if ((d.gear & d.reward) != 0 || (d.gear & LightSword) != 0) {
            return false;
        }
        if ((d.gear & Clawshot) && (d.gear & DoubleClawshots)) {
            return false;
        }
        if (standard_hearts(i) < 3 || standard_hearts(i) > 20) {
            return false;
        }
    }
    return (kDungeons[6].gear & Clawshot) && !(kDungeons[6].gear & DoubleClawshots) &&
           (kDungeons[7].gear & DoubleClawshots);
}
static_assert(valid_presets());
}  // namespace rush
