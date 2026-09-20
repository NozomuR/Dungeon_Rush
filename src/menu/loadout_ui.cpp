#include "menu/loadout_ui.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "rules/categories.hpp"
#include "ghost/ghost.hpp"
#include "menu/menu_ui.hpp"
#include "rules/presets.hpp"
#include "menu/ui_common.hpp"

namespace rush::loadout_ui {
namespace {
using menu::menu_section;
using menu::selected;
using ui::icons;
using ui::ItemIcon;

const ItemIcon* icon_for(uint32_t bit) {
    for (const auto& item : icons) {
        if (item.bit == bit) {
            return &item;
        }
    }
    return nullptr;
}

// Everything that can sit on X or Y. `slot` matches Loadout::equipped; -2 means
// "let the game decide", which is where a slot returns to when its item is un-equipped.
struct SlotChoice {
    int slot;
    uint32_t gear_bit;  // 0 = always available
    const char* icon;   // items/<icon>.png, or nullptr
    const char* label;
};
std::vector<SlotChoice> slot_choices;

int drawer_side = -1;  // -1 closed, 0 editing X, 1 editing Y
std::array<UiElementHandle, 2> slot_buttons{};
std::array<std::string, 2> slot_icon_class{};
UiElementHandle drawer_label = 0;
std::vector<UiElementHandle> drawer_tiles;

struct GearTile {
    UiElementHandle element;
    uint32_t bit;
};
std::vector<GearTile> gear_tiles;
UiElementHandle sword_tile = 0, shield_tile = 0;
std::string sword_icon_class, shield_icon_class;

int remembered_ghost_mode = 1;  // restored when the ghost toggle is switched back on
std::array<UiElementHandle, 3> ghost_chips{};

bool unrestricted() {
    return selected_category == Category::Unrestricted;
}

uint32_t permitted_gear() {
    return allowed_gear(selected, selected_category, menu_section == 1);
}

// Forest allows Ordon and Wooden shields; everywhere else all three. Unrestricted: all three.
std::size_t shield_choices() {
    return !unrestricted() && selected == 0 ? 2 : kShields.size();
}

// Only show what this dungeon and category let the player pick.
void refresh_gear_visibility() {
    const auto allowed = permitted_gear();
    for (const auto& tile : gear_tiles) {
        svc_ui->elem_set_class(mod_ctx, tile.element, "hidden", (allowed & tile.bit) == 0);
    }
    // The Master Sword is only a choice from Lakebed onwards, or in Unrestricted.
    svc_ui->elem_set_class(mod_ctx, sword_tile, "hidden", !unrestricted() && selected < 3);
}

uint32_t current_gear() {
    const auto reward = menu_section == 1 ? kDungeons[selected].reward : 0u;
    return normalize_gear(loadouts[selected].gear | reward);
}

void build_slot_choices() {
    slot_choices.clear();
    for (const auto& item : kInventory) {
        const auto* icon = icon_for(item.bit);
        slot_choices.push_back(
            {item.slot, item.bit, icon ? icon->id : nullptr, icon ? icon->label : "Item"});
    }
    slot_choices.push_back({11, 0, "60", "Empty Bottle"});
}

bool choice_available(const SlotChoice& choice) {
    return choice.gear_bit == 0 || (current_gear() & choice.gear_bit) != 0;
}

// An item that left the loadout cannot stay equipped.
void sanitize_equipped() {
    for (auto& slot : loadouts[selected].equipped) {
        const auto it = std::find_if(slot_choices.begin(), slot_choices.end(),
                                     [&](const SlotChoice& c) { return c.slot == slot; });
        if (it == slot_choices.end() || !choice_available(*it)) {
            slot = -2;
        }
    }
}

void swap_class(UiElementHandle element, std::string& current, const std::string& next) {
    if (!element || current == next) {
        return;
    }
    if (!current.empty()) {
        svc_ui->elem_set_class(mod_ctx, element, current.c_str(), false);
    }
    if (!next.empty()) {
        svc_ui->elem_set_class(mod_ctx, element, next.c_str(), true);
    }
    current = next;
}

void refresh_slot_icons() {
    for (int side = 0; side < 2; ++side) {
        const int slot = loadouts[selected].equipped[side];
        const auto it = std::find_if(slot_choices.begin(), slot_choices.end(),
                                     [&](const SlotChoice& c) { return c.slot == slot; });
        const char* icon = it != slot_choices.end() ? it->icon : nullptr;
        swap_class(slot_buttons[side], slot_icon_class[side],
                   icon ? "gear-icon-" + std::string(icon) : "");
    }
}

void refresh_sword_and_shield_icons() {
    swap_class(sword_tile, sword_icon_class,
               loadouts[selected].master ? "gear-icon-29" : "gear-icon-28");
    swap_class(shield_tile, shield_icon_class,
               "gear-icon-" + std::string(kShields[loadouts[selected].shield].icon));
}

void show_drawer(int side) {
    drawer_side = side;
    svc_ui->elem_set_class(mod_ctx, drawer_label, "hidden", side < 0);
    for (std::size_t i = 0; i < drawer_tiles.size(); ++i) {
        const bool hidden = side < 0 || !choice_available(slot_choices[i]);
        svc_ui->elem_set_class(mod_ctx, drawer_tiles[i], "hidden", hidden);
    }
}

void refresh_ghost_chips() {
    const bool hidden = ghost::race_mode == 0;
    for (auto chip : ghost_chips) {
        svc_ui->elem_set_class(mod_ctx, chip, "hidden", hidden);
    }
}

void gear_changed() {
    constrain_loadout(loadouts[selected], selected, selected_category);
    sanitize_equipped();
    refresh_slot_icons();
    refresh_sword_and_shield_icons();
    if (drawer_side >= 0) {
        show_drawer(drawer_side);
    }
}

void reset_handles_impl() {
    drawer_side = -1;
    slot_buttons = {};
    slot_icon_class = {};
    drawer_label = 0;
    drawer_tiles.clear();
    gear_tiles.clear();
    sword_tile = shield_tile = 0;
    sword_icon_class.clear();
    shield_icon_class.clear();
    ghost_chips = {};
}

void category_changed_impl() {
    if (gear_tiles.empty()) {
        return;  // page not built
    }
    gear_changed();
    refresh_gear_visibility();
}

ModResult add_label(ModContext* ctx, UiElementHandle pane, const char* text) {
    return svc_ui->pane_add_rml(
        ctx, pane, (std::string("<div class='label section'>") + text + "</div>").c_str(), nullptr);
}

// Compact stepper that shares the equipment row; the heart icon is its decorator (see push()).
ModResult add_hearts(ModContext* ctx, UiElementHandle pane) {
    UiControlDesc hearts = UI_CONTROL_DESC_INIT;
    hearts.kind = UI_CONTROL_NUMBER;
    hearts.label = "Hearts";
    hearts.min = 1;
    hearts.max = 20;
    hearts.step = 1;
    hearts.get = [](ModContext*, void*, UiControlValue* v) {
        v->int_value = loadouts[selected].hearts;
    };
    hearts.set = [](ModContext*, void*, const UiControlValue* v) {
        loadouts[selected].hearts = static_cast<int>(std::clamp<int64_t>(v->int_value, 1, 20));
        gear_changed();
    };
    UiElementHandle field = 0;
    CHECK(svc_ui->pane_add_control(ctx, pane, &hearts, &field));
    return svc_ui->elem_set_class(ctx, field, "hearts-field", true);
}

// X / Y buttons plus the item drawer that opens beneath them.
ModResult build_equipment(ModContext* ctx, UiElementHandle pane) {
    CHECK(add_label(ctx, pane, "EQUIPMENT"));
    for (int side = 0; side < 2; ++side) {
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.label = side == 0 ? "X item" : "Y item";  // read by the glyph class, not shown
        c.user_data = reinterpret_cast<void*>(static_cast<uintptr_t>(side));
        c.is_selected = [](ModContext*, void* data) {
            return drawer_side == static_cast<int>(reinterpret_cast<uintptr_t>(data));
        };
        c.on_pressed = [](ModContext*, void* data) {
            const int side = static_cast<int>(reinterpret_cast<uintptr_t>(data));
            show_drawer(drawer_side == side ? -1 : side);
        };
        CHECK(svc_ui->pane_add_control(ctx, pane, &c, &slot_buttons[side]));
        CHECK(svc_ui->elem_set_class(ctx, slot_buttons[side], "slot-button", true));
        CHECK(
            svc_ui->elem_set_class(ctx, slot_buttons[side], side == 0 ? "slot-x" : "slot-y", true));
    }
    CHECK(add_hearts(ctx, pane));
    CHECK(svc_ui->pane_add_rml(
        ctx, pane, "<div class='label drawer-label hidden'>CHOOSE AN ITEM</div>", &drawer_label));
    for (std::size_t i = 0; i < slot_choices.size(); ++i) {
        const auto& choice = slot_choices[i];
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.label = choice.label;
        c.user_data = reinterpret_cast<void*>(static_cast<uintptr_t>(i));
        c.is_selected = [](ModContext*, void* data) {
            const auto& choice = slot_choices[reinterpret_cast<uintptr_t>(data)];
            return drawer_side >= 0 && loadouts[selected].equipped[drawer_side] == choice.slot;
        };
        c.on_pressed = [](ModContext*, void* data) {
            if (drawer_side < 0) {
                return;
            }
            const auto& choice = slot_choices[reinterpret_cast<uintptr_t>(data)];
            if (!choice_available(choice)) {
                return;
            }
            auto& equipped = loadouts[selected].equipped;
            const int side = drawer_side, other = 1 - side;
            if (equipped[side] == choice.slot) {
                equipped[side] = -2;  // clicking the equipped item un-equips it
            } else {
                // Moving an item that the other button already holds swaps them.
                if (equipped[other] == choice.slot) {
                    equipped[other] = equipped[side];
                }
                equipped[side] = choice.slot;
            }
            refresh_slot_icons();
            show_drawer(-1);
        };
        UiElementHandle tile = 0;
        CHECK(svc_ui->pane_add_control(ctx, pane, &c, &tile));
        CHECK(svc_ui->elem_set_class(ctx, tile, "gear-tile", true));
        CHECK(svc_ui->elem_set_class(ctx, tile, "drawer-tile", true));
        CHECK(svc_ui->elem_set_class(ctx, tile, "hidden", true));
        CHECK(svc_ui->elem_set_class(ctx, tile, ("gear-icon-" + std::string(choice.icon)).c_str(),
                                     true));
        drawer_tiles.push_back(tile);
    }
    return MOD_OK;
}

// Sword, shield and item tiles; the tiles are the picker.
ModResult build_starting_gear(ModContext* ctx, UiElementHandle pane) {
    CHECK(add_label(ctx, pane, "ALLOWED STARTING GEAR"));
    {
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.label = "Sword";
        // A sword is always part of the loadout; the tile cycles which one and stays lit.
        c.is_selected = [](ModContext*, void*) {
            return true;
        };
        c.on_pressed = [](ModContext*, void*) {
            loadouts[selected].master = !loadouts[selected].master;
            gear_changed();
        };
        CHECK(svc_ui->pane_add_control(ctx, pane, &c, &sword_tile));
        CHECK(svc_ui->elem_set_class(ctx, sword_tile, "gear-tile", true));
    }
    {
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.label = "Shield";
        c.is_selected = [](ModContext*, void*) {
            return true;
        };  // likewise always equipped
        c.on_pressed = [](ModContext*, void*) {
            auto& shield = loadouts[selected].shield;
            shield = (shield + 1) % shield_choices();
            gear_changed();
        };
        CHECK(svc_ui->pane_add_control(ctx, pane, &c, &shield_tile));
        CHECK(svc_ui->elem_set_class(ctx, shield_tile, "gear-tile", true));
    }
    refresh_sword_and_shield_icons();
    for (const auto& item : icons) {
        if (item.bit == LightSword) {
            continue;
        }
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.label = item.label;
        c.user_data = reinterpret_cast<void*>(static_cast<uintptr_t>(item.bit));
        c.is_selected = [](ModContext*, void* data) {
            return (current_gear() & static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data))) != 0;
        };
        c.is_disabled = [](ModContext*, void* data) {
            const auto bit = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
            return menu_section == 1 && (kDungeons[selected].reward & bit);  // boss runs need it
        };
        c.on_pressed = [](ModContext*, void* data) {
            const auto bit = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
            if (!(permitted_gear() & bit)) {
                return;
            }
            auto& mask = loadouts[selected].gear;
            mask ^= bit;
            if ((mask & bit) && bit == Clawshot) {
                mask &= ~DoubleClawshots;
            }
            if ((mask & bit) && bit == DoubleClawshots) {
                mask &= ~Clawshot;
            }
            gear_changed();
        };
        UiElementHandle tile = 0;
        CHECK(svc_ui->pane_add_control(ctx, pane, &c, &tile));
        CHECK(svc_ui->elem_set_class(ctx, tile, "gear-tile", true));
        CHECK(svc_ui->elem_set_class(ctx, tile, ("gear-icon-" + std::to_string(item.bit)).c_str(),
                                     true));
        gear_tiles.push_back({tile, item.bit});
    }
    refresh_gear_visibility();
    return MOD_OK;
}

// On/off toggle; the mode chips appear only while on.
ModResult build_ghost(ModContext* ctx, UiElementHandle pane) {
    UiControlDesc toggle = UI_CONTROL_DESC_INIT;
    toggle.label = "Race a ghost";
    toggle.is_selected = [](ModContext*, void*) {
        return ghost::race_mode != 0;
    };
    toggle.on_pressed = [](ModContext*, void*) {
        if (ghost::race_mode == 0) {
            ghost::race_mode = remembered_ghost_mode;
        } else {
            remembered_ghost_mode = ghost::race_mode;
            ghost::race_mode = 0;
        }
        refresh_ghost_chips();
    };
    UiElementHandle toggle_element = 0;
    CHECK(svc_ui->pane_add_control(ctx, pane, &toggle, &toggle_element));
    CHECK(svc_ui->elem_set_class(ctx, toggle_element, "ghost-toggle", true));
    static const char* labels[] = {"Latest attempt", "Personal best", "Selected run"};
    for (int mode = 1; mode <= 3; ++mode) {
        UiControlDesc chip = UI_CONTROL_DESC_INIT;
        chip.label = labels[mode - 1];
        chip.user_data = reinterpret_cast<void*>(static_cast<uintptr_t>(mode));
        chip.is_selected = [](ModContext*, void* data) {
            return ghost::race_mode == static_cast<int>(reinterpret_cast<uintptr_t>(data));
        };
        chip.on_pressed = [](ModContext*, void* data) {
            ghost::race_mode = static_cast<int>(reinterpret_cast<uintptr_t>(data));
            remembered_ghost_mode = ghost::race_mode;
        };
        CHECK(svc_ui->pane_add_control(ctx, pane, &chip, &ghost_chips[mode - 1]));
        CHECK(svc_ui->elem_set_class(ctx, ghost_chips[mode - 1], "ghost-chip", true));
    }
    refresh_ghost_chips();
    return MOD_OK;
}

}  // namespace

// ---- public API -------------------------------------------------------------

void reset_handles() {
    reset_handles_impl();
}

void category_changed() {
    category_changed_impl();
}

ModResult build(ModContext* ctx, UiElementHandle pane) {
    reset_handles_impl();
    build_slot_choices();
    sanitize_equipped();
    if (ghost::race_mode != 0) {
        remembered_ghost_mode = ghost::race_mode;
    }
    // Gear first, then what to put on X and Y from it, then ghost racing.
    CHECK(build_starting_gear(ctx, pane));
    CHECK(build_equipment(ctx, pane));
    refresh_slot_icons();
    return build_ghost(ctx, pane);
}

std::string decorator_styles() {
    std::string style;
    // One decorator class per item image. The equipment boxes also carry a faded
    // X or Y letter behind the item so the pairing is obvious at a glance.
    auto image = [](const std::string& id) {
        return ui::asset("items", (id + ".png").c_str());
    };
    const std::string letter_x = ui::asset("icons", "slot_x.png"),
                      letter_y = ui::asset("icons", "slot_y.png");
    auto add_item = [&](const std::string& id) {
        style += "\nbody window button.gear-icon-" + id + " { decorator:image(" + image(id) +
                 " contain center center); }";
        style += "\nbody window button.slot-x.gear-icon-" + id + " { decorator:image(" + image(id) +
                 " contain center center),image(" + letter_x + " contain center center); }";
        style += "\nbody window button.slot-y.gear-icon-" + id + " { decorator:image(" + image(id) +
                 " contain center center),image(" + letter_y + " contain center center); }";
    };
    for (const auto& item : icons) {
        style += "\nbody window button.gear-icon-" + std::to_string(item.bit) +
                 " { decorator:image(" + image(item.id) + " contain center center); }";
        add_item(item.id);
    }
    for (const char* id : {"28", "29", "60"}) {
        add_item(id);
    }
    for (const auto& shield : kShields) {
        add_item(shield.icon);
    }
    style +=
        "\nbody window button.slot-x { decorator:image(" + letter_x + " contain center center); }";
    style +=
        "\nbody window button.slot-y { decorator:image(" + letter_y + " contain center center); }";
    style += "\nbody window .dungeon-main .hearts-field { decorator:image(" +
             ui::asset("icons", "heart_small.png") + " scale-none 22dp center); }";
    return style;
}

}  // namespace rush::loadout_ui
