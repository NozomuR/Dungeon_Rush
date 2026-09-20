#pragma once
#include "rules/presets.hpp"
#include <algorithm>
#include <string_view>
namespace rush {
enum class Category { Glitchless, Glitched, Unrestricted, Legacy };
inline Category selected_category = Category::Glitchless;
inline constexpr const char* category_names[] = {"Glitchless", "Glitched", "Unrestricted",
                                                 "Legacy"};
inline constexpr const char* normal_rulesets[] = {"glitchless-v1", "glitched-v1",
                                                  "unrestricted-v1"};
inline constexpr const char* boss_rulesets[] = {"boss-glitchless-v1", "boss-glitched-v1",
                                                "boss-unrestricted-v1"};
constexpr const char* category_ruleset(Category c, bool boss, size_t dungeon) {
    if (c == Category::Legacy) {
        return boss ? "boss-local-v1" : dungeon == 0 ? "forest-local-v2" : "dungeon-local-v1";
    }
    return boss ? boss_rulesets[int(c)] : normal_rulesets[int(c)];
}
constexpr Category record_category(std::string_view rule) {
    for (int i = 0; i < 3; ++i) {
        if (rule == category_ruleset(Category(i), false, 0) ||
            rule == category_ruleset(Category(i), true, 0)) {
            return Category(i);
        }
    }
    return Category::Legacy;
}
constexpr uint32_t allowed_gear(size_t dungeon, Category category, bool boss = false) {
    return category == Category::Unrestricted
               ? 0xFFFFu
               : normalize_gear(kDungeons[dungeon].gear | (boss ? kDungeons[dungeon].reward : 0u));
}
inline void constrain_loadout(Loadout& loadout, size_t dungeon, Category category) {
    loadout.gear = normalize_gear(loadout.gear & allowed_gear(dungeon, category));
    if (category != Category::Unrestricted) {
        if (dungeon < 3) {
            loadout.master = false;
        }
        loadout.shield = std::min(loadout.shield, dungeon == 0 ? size_t(1) : size_t(2));
        loadout.hearts = std::clamp(loadout.hearts, 3, standard_hearts(dungeon));
    }
}
inline void choose_category(Category category) {
    selected_category = category;
    for (size_t i = 0; i < loadouts.size(); ++i) {
        constrain_loadout(loadouts[i], i, category);
    }
}
}  // namespace rush
