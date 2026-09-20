#pragma once
#include "rules/presets.hpp"
#include "menu/style.hpp"
#include <mods/svc/ui.h>
#include <vector>
#include <string>
#include <algorithm>

namespace rush::equipment_ui {
inline UiWindowHandle window = 0;
inline const UiService* ui = nullptr;
inline std::size_t dungeon = 0;
inline std::vector<int> slots;
inline std::vector<std::string> labels;
inline std::vector<const char*> options;
inline UiPressedFn proceed = nullptr;
inline void* proceed_data = nullptr;
inline void close(ModContext* ctx) {
    if (window) {
        auto w = window;
        window = 0;
        ui->window_close(ctx, w);
    }
}
inline ModResult build(ModContext* ctx, UiWindowHandle, UiElementHandle left, UiElementHandle right,
                       void*, ModError*) {
    ui->elem_set_class(ctx, left, "settings-pane", true);
    ui->elem_set_class(ctx, right, "settings-pane", true);
    auto title = std::string("<h1>Equip items</h1><p>") + kDungeons[dungeon].name + "</p>";
    auto result = ui->pane_add_rml(ctx, left, title.c_str(), nullptr);
    if (result != MOD_OK) {
        return result;
    }
    for (int i = 0; i < 2; ++i) {
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.kind = UI_CONTROL_SELECT;
        c.label = i == 0 ? "X item" : "Y item";
        c.options = options.data();
        c.option_count = options.size();
        c.user_data = reinterpret_cast<void*>(uintptr_t(i));
        c.get = [](ModContext*, void* data, UiControlValue* v) {
            auto value = loadouts[dungeon].equipped[reinterpret_cast<uintptr_t>(data)];
            auto it = std::find(slots.begin(), slots.end(), value);
            v->int_value = it == slots.end() ? 0 : it - slots.begin();
        };
        c.set = [](ModContext*, void* data, const UiControlValue* v) {
            if (v->int_value < 0 || size_t(v->int_value) >= slots.size()) {
                return;
            }
            auto side = reinterpret_cast<uintptr_t>(data);
            auto& equipped = loadouts[dungeon].equipped;
            int previous = equipped[side], chosen = slots[size_t(v->int_value)];
            if (chosen >= 0 && equipped[1 - side] == chosen) {
                equipped[1 - side] = previous;
            }
            equipped[side] = chosen;
        };
        result = ui->pane_add_control(ctx, left, &c, nullptr);
        if (result != MOD_OK) {
            return result;
        }
    }
    if (proceed) {
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.label = "Begin gauntlet";
        c.on_pressed = [](ModContext* ctx, void*) {
            auto fn = proceed;
            auto data = proceed_data;
            close(ctx);
            if (fn) {
                fn(ctx, data);
            }
        };
        result = ui->pane_add_control(ctx, left, &c, nullptr);
        if (result != MOD_OK) {
            return result;
        }
    }
    UiControlDesc back = UI_CONTROL_DESC_INIT;
    back.label = proceed ? "Back" : "Done";
    back.on_pressed = [](ModContext* ctx, void*) {
        close(ctx);
    };
    return ui->pane_add_control(ctx, left, &back, nullptr);
}
inline ModResult open(ModContext* ctx, const UiService* service, size_t target, bool bosses,
                      UiPressedFn start = nullptr, void* data = nullptr) {
    if (window) {
        return MOD_OK;
    }
    ui = service;
    dungeon = target;
    proceed = start;
    proceed_data = data;
    slots = {-2, -1};
    labels = {"Default", "Empty"};
    auto gear = normalize_gear(loadouts[target].gear | (bosses ? kDungeons[target].reward : 0u));
    for (const auto& item : kInventory) {
        if (gear & item.bit) {
            slots.push_back(item.slot);
            auto label = std::find_if(kGearLabels.begin(), kGearLabels.end(),
                                      [&](const auto& g) { return g.bit == item.bit; });
            labels.emplace_back(label->name);
        }
    }
    slots.push_back(11);
    labels.emplace_back("Empty bottle");
    for (auto& slot : loadouts[target].equipped) {
        if (std::find(slots.begin(), slots.end(), slot) == slots.end()) {
            slot = -2;
        }
    }
    options.clear();
    for (const auto& label : labels) {
        options.push_back(label.c_str());
    }
    UiTabDesc tab = UI_TAB_DESC_INIT;
    tab.title = "Equip items";
    tab.build = build;
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = &tab;
    desc.tab_count = 1;
    desc.rcss = kStyle;
    desc.on_closed = [](ModContext*, UiWindowHandle, void*) {
        window = 0;
    };
    return ui->window_push(ctx, &desc, &window);
}
}  // namespace rush::equipment_ui
