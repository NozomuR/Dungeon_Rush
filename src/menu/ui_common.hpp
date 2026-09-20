#pragma once
// Small helpers and constant tables shared by the menu modules.
#include <mods/service.hpp>
#include <mods/svc/log.h>
#include <mods/svc/ui.h>

#include <cstdint>
#include <string>

#include "rules/presets.hpp"

// Log a failed UI call and return its error from the enclosing ModResult function.
#define CHECK(call)                                           \
    do {                                                      \
        const auto result = (call);                           \
        if (result != MOD_OK) {                               \
            svc_log->warn(mod_ctx, "UI call failed: " #call); \
            return result;                                    \
        }                                                     \
    } while (false)

namespace rush::ui {

struct BossArt {
    const char* name;
    const char* file;
};
inline constexpr BossArt bosses[] = {
    {"Diababa", "diababa.png"},   {"Fyrus", "fyrus.png"},       {"Morpheel", "morpheel.png"},
    {"Stallord", "stallord.png"}, {"Blizzeta", "blizzeta.png"}, {"Armogohma", "armagohma.png"},
    {"Argorok", "argorok.png"},   {"Zant", "zant.png"},         {"Ganondorf", "ganondorf.png"},
};
static_assert(std::size(bosses) == kDungeons.size());

struct ItemIcon {
    uint32_t bit;
    const char* id;
    const char* label;
    int width, height;
};
inline constexpr ItemIcon icons[] = {
    {Lantern, "48", "Lantern", 32, 64},
    {Slingshot, "4b", "Slingshot", 40, 62},
    {FishingRod, "4a", "Fishing Rod", 32, 72},
    {Boomerang, "40", "Gale Boomerang", 32, 64},
    {IronBoots, "45", "Iron Boots", 48, 48},
    {Bow, "43", "Hero's Bow", 48, 48},
    {Bombs, "70", "Bombs", 48, 48},
    {WaterBombs, "71", "Water Bombs", 48, 48},
    {Clawshot, "44", "Clawshot", 40, 56},
    {Spinner, "41", "Spinner", 56, 40},
    {BallChain, "42", "Ball and Chain", 56, 47},
    {DominionRod, "46", "Dominion Rod", 40, 64},
    {DoubleClawshots, "47", "Double Clawshots", 56, 44},
    {ZoraArmor, "31", "Zora Armor", 48, 48},
    {LightSword, "49", "Light infusion", 48, 48},
    {WolfForm, "wolf", "Wolf transformation", 40, 41},
};

// UI callbacks cannot return errors, so they park the first one here; mod_update reports it.
void remember(ModResult result);
ModResult deferred_error();

// mod:// URL for a bundled resource, e.g. asset("items", "48.png").
std::string asset(const char* kind, const char* file);
// Escape text for inclusion in RML.
std::string rml_text(const std::string& text);
// Add a button control with a CSS class.
ModResult button(ModContext* ctx, UiElementHandle pane, const char* label, UiPressedFn pressed,
                 void* data, const char* css, UiPredicateFn disabled = nullptr);

}  // namespace rush::ui
