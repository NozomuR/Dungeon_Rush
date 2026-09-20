#include "menu/menu_ui.hpp"

#include <mods/svc/host.h>
#include <mods/svc/log.h>
#include <mods/svc/resource.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "rules/boss_tests.hpp"
#include "rules/categories.hpp"
#include "menu/discord_ui.hpp"
#include "menu/equipment_ui.hpp"
#include "menu/leaderboard_ui.hpp"
#include "menu/loadout_ui.hpp"
#include "menu/menu_music.hpp"
#include "rules/presets.hpp"
#include "replay/replay.hpp"
#include "run/run.hpp"
#include "menu/style.hpp"
#include "menu/ui_common.hpp"
#include "menu/video.hpp"

extern "C" ModResult mod_update(ModError*);

namespace rush::menu {

UiWindowHandle gallery = 0, detail = 0;
std::size_t selected = 0;
int menu_section = 0;  // Dungeons, Boss Rush, Gauntlets.
GameModeNewSaveState* pending_selection = nullptr;

namespace {
std::array<UiWindowHandle, 2> closing_windows{};
UiWindowHandle closing_window = 0;
std::chrono::steady_clock::time_point close_after;
constexpr std::array<const char*, 10> video_files = {
    "forest_temple.mp4",  "goron_mines.mp4",    "lakebed_temple.mp4",  "arbiters_grounds.mp4",
    "snowpeak_ruins.mp4", "temple_of_time.mp4", "city_in_the_sky.mp4", "palace_of_twilight.mp4",
    "hyrule_castle.mp4",  "hyrule_field.mp4"};
std::array<std::filesystem::path, 10> video_paths;
std::array<bool, 10> video_available{};

using leaderboard_ui::build_board;
using leaderboard_ui::close_run_options;
using leaderboard_ui::detail_board;
using leaderboard_ui::gallery_board;
using ui::asset;
using ui::bosses;
using ui::button;
using ui::remember;

void close_pressed(ModContext* ctx, void* data) {
    const auto handle = *static_cast<UiWindowHandle*>(data);
    if (handle) {
        remember(svc_ui->window_close(ctx, handle));
    }
}
void close_menu_stack() {
    // Window::pop uncovers the next document. Closing it in the same UI frame
    // can cancel its opening transition before any style update, so no closing
    // transitionend is emitted and the host never destroys that document.
    close_run_options();
    closing_windows = {detail, gallery};
    detail = gallery = 0;
    closing_window = 0;
    close_after = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
}
void update_menu_close() {
    if (closing_window || std::chrono::steady_clock::now() < close_after) {
        return;
    }
    for (auto& handle : closing_windows) {
        if (!handle) {
            continue;
        }
        closing_window = handle;
        handle = 0;
        remember(svc_ui->window_close(mod_ctx, closing_window));
        return;
    }
}
void on_closed(ModContext*, UiWindowHandle handle, void*) {
    if (handle == closing_window) {
        closing_window = 0;
        // Let the newly uncovered window finish opening before closing it.
        close_after = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    }
    if (handle == detail) {
        loadout_ui::reset_handles();
        detail = 0;
        video::stop();
        if (gallery && video_available[9]) {
            video::start(video_paths[9]);
        }
    }
    if (handle == gallery) {
        close_run_options();
        menu_music::attach_credit(0);
        gallery = 0;
        video::stop();
        menu_music::stop();
        if (pending_selection) {
            *pending_selection = GAME_MODE_STATE_RETURN;
            pending_selection = nullptr;
        }
    }
}
ModResult push(ModContext* ctx, const char* title, UiTabBuildFn build, UiWindowHandle* handle) {
    UiTabDesc tab = UI_TAB_DESC_INIT;
    tab.title = title;
    tab.build = build;
    // Host menus can pause game ticks; keep the music lifecycle alive in UI frames.
    tab.update = [](ModContext*, void*, ModError* error) {
        return mod_update(error);
    };
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    std::array<UiTabDesc, 3> sections{tab, tab, tab};
    std::string style = kStyle;
    if (handle == &detail) {
        style += loadout_ui::decorator_styles();
    }
    if (handle == &gallery) {
        style += R"RCSS(
body window content { align-items:center; }
body window tab-bar { display:flex; flex:0 0 auto; height:auto; align-items:center;
 padding:16dp 5%; gap:12dp; background-color:transparent; border-width:0;
 font-family:Fira Sans; font-weight:normal; font-size:18dp; text-transform:none; }
body window tab-bar tab { box-sizing:border-box; height:44dp; line-height:42dp;
 padding:0 24dp; opacity:1; color:#b7c8c1; font-family:Fira Sans; font-weight:normal;
 font-size:18dp; text-transform:none; border-width:1dp; border-color:#b1dbcc38;
 border-radius:5dp; background-color:#07110e66; decorator:none; font-effect:none;
 transition:background-color color border-color .16s linear-in-out; }
body window tab-bar tab:selected { border-width:1dp; border-color:#a3e4cc;
 color:#f3fff9; background-color:#284b40bb; decorator:none; font-effect:none; }
body window tab-bar tab:hover,body window tab-bar tab:focus-visible {
 border-color:#d4ffed; color:#ffffff; background-color:#365c50cc;
 decorator:none; font-effect:none; }
body window tab-bar tab:active { background-color:#477665dd; decorator:none; }
)RCSS";
        if (video_available[9]) {
            style +=
                "\nbody { decorator: image(dungeon-rush-video://preview cover center center); }";
        } else {
            style +=
                "\nbody { decorator: radial-gradient(ellipse farthest-corner at 65% 30%, #182a25 0%, #080f12 100%); }";
        }
    } else {
        const auto source = video_available[selected] ? std::string("dungeon-rush-video://preview")
                                                      : asset("bosses", bosses[selected].file);
        style += "\nbody { decorator: image(" + source + " cover center center); }";
    }
    desc.tabs = &tab;
    desc.tab_count = 1;
    desc.on_closed = on_closed;
    desc.rcss = style.c_str();
    if (handle == &gallery) {
        constexpr const char* names[] = {"Dungeons", "Bosses", "Gauntlets"};
        for (size_t i = 0; i < sections.size(); ++i) {
            sections[i].title = names[i];
            sections[i].user_data = reinterpret_cast<void*>(i);
        }
        desc.tabs = sections.data();
        desc.tab_count = sections.size();
    }
    return svc_ui->window_push(ctx, &desc, handle);
}
ModResult category_control(ModContext* ctx, UiElementHandle pane) {
    UiControlDesc c = UI_CONTROL_DESC_INIT;
    c.kind = UI_CONTROL_SELECT;
    c.label = "Run category";
    c.options = category_names;
    c.option_count = 3;
    c.get = [](ModContext*, void*, UiControlValue* v) {
        v->int_value = int(selected_category);
    };
    c.set = [](ModContext*, void*, const UiControlValue* v) {
        if (v->int_value < 0 || v->int_value > 2) {
            return;
        }
        choose_category(Category(v->int_value));
    };
    return svc_ui->pane_add_control(ctx, pane, &c, nullptr);
}
bool cannot_begin(ModContext*, void*) {
    return !run::can_begin(pending_selection != nullptr);
}
void begin_run(ModContext* ctx, void* mode) {
    if (!run::start(pending_selection != nullptr,
                    static_cast<run::Mode>(reinterpret_cast<uintptr_t>(mode)), selected)) {
        return;
    }
    if (pending_selection) {
        *pending_selection = GAME_MODE_STATE_PROCEED;
        pending_selection = nullptr;
    }
    video::stop();
    menu_music::stop(true);
    close_menu_stack();
}
void begin_gauntlet(ModContext* ctx, void* mode) {
    remember(equipment_ui::open(ctx, svc_ui, 0, reinterpret_cast<uintptr_t>(mode) == 3, begin_run,
                                mode));
}
ModResult build_detail(ModContext* ctx, UiWindowHandle, UiElementHandle left, UiElementHandle right,
                       void*, ModError*) {
    CHECK(svc_ui->elem_set_class(ctx, left, "dungeon-main", true));
    CHECK(svc_ui->elem_set_class(ctx, right, "rankings", true));
    const auto& d = kDungeons[selected];

    CHECK(button(ctx, left, "\xe2\x86\x90  Return to Main Menu", close_pressed, &detail,
                 "return-link"));
    const std::string title = std::string("<div class='dungeon-title'><h1>") +
                              (menu_section == 1 ? bosses[selected].name : d.name) + "</h1></div>";
    CHECK(svc_ui->pane_add_rml(ctx, left, title.c_str(), nullptr));
    if (video_available[selected]) {
        video::start(video_paths[selected]);
    } else {
        video::stop();
    }
    const auto run_mode =
        static_cast<uintptr_t>(menu_section == 1 ? run::Mode::Boss : run::Mode::Dungeon);
    CHECK(button(ctx, left, menu_section == 1 ? "Begin boss run" : "Begin run", begin_run,
                 reinterpret_cast<void*>(run_mode), "begin", cannot_begin));
    const auto summary = run::summary_rml();
    if (!summary.empty()) {
        CHECK(svc_ui->pane_add_rml(ctx, left, summary.c_str(), nullptr));
    }
    if (!run::available()) {
        CHECK(svc_ui->pane_add_rml(ctx, left, "<p>Select Dungeon Rush at file select to play.</p>",
                                   nullptr));
    }
    CHECK(loadout_ui::build(ctx, left));

    detail_board.dungeon = selected;
    detail_board.bosses = menu_section == 1;
    if (detail_board.bosses) {
        detail_board.public_runs = false;
    }
    return build_board(ctx, right, detail_board, false);
}
void select_dungeon(ModContext* ctx, void* data) {
    if (detail) {
        return;
    }
    selected = reinterpret_cast<uintptr_t>(data);
    if (selected >= kDungeons.size()) {
        selected = 0;
        return;
    }
    remember(push(ctx, kDungeons[selected].name, build_detail, &detail));
}
ModResult build_gallery(ModContext* ctx, UiWindowHandle, UiElementHandle left,
                        UiElementHandle right, void* section, ModError*) {
    menu_section = static_cast<int>(reinterpret_cast<uintptr_t>(section));
    discord_ui::attach(left);
    leaderboard_ui::reset();
    selected = 0;
    menu_music::attach_credit(0);
    menu_music::choose_track();
    CHECK(svc_ui->elem_set_class(ctx, left, "gallery", true));
    CHECK(svc_ui->elem_set_class(ctx, right, "gallery-summary", true));
    const char* heading = menu_section == 0   ? "Dungeons"
                          : menu_section == 1 ? "Bosses"
                                              : "Gauntlets";
    CHECK(svc_ui->pane_add_rml(
        ctx, left,
        (std::string("<div class='gallery-heading'><h1>") + heading + "</h1></div>").c_str(),
        nullptr));
    if (video_available[9]) {
        video::start(video_paths[9]);
    } else {
        video::stop();
    }
    if (menu_section == 2) {
        CHECK(category_control(ctx, left));
        CHECK(button(ctx, left, "Boss gauntlet", begin_gauntlet, reinterpret_cast<void*>(3),
                     "begin", cannot_begin));
        CHECK(button(ctx, left, "Dungeon gauntlet", begin_gauntlet, reinterpret_cast<void*>(2),
                     "begin", cannot_begin));
        CHECK(svc_ui->pane_add_rml(ctx, right, run::gauntlet_history().c_str(), nullptr));
        return button(ctx, left, "Back", close_pressed, &gallery, "back");
    }
    gallery_board.bosses = menu_section == 1;
    if (gallery_board.bosses) {
        gallery_board.public_runs = false;
    }
    CHECK(build_board(ctx, right, gallery_board, true));
    for (std::size_t i = 0; i < kDungeons.size(); ++i) {
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        const auto label = "0" + std::to_string(i + 1) + "\xc2\xa0\xc2\xa0\xc2\xa0" +
                           (menu_section == 1 ? bosses[i].name : kDungeons[i].name);
        c.label = label.c_str();
        c.on_pressed = select_dungeon;
        c.user_data = reinterpret_cast<void*>(i);
        UiElementHandle element = 0;
        CHECK(svc_ui->pane_add_control(ctx, left, &c, &element));
        CHECK(svc_ui->elem_set_class(ctx, element, "dungeon-card", true));
        CHECK(svc_ui->elem_set_class(ctx, element, ("card-" + std::to_string(i)).c_str(), true));
    }
    UiElementHandle credit = 0;
    CHECK(svc_ui->pane_add_text(ctx, right, menu_music::credit(), &credit));
    CHECK(svc_ui->elem_set_class(ctx, credit, "music-credit", true));
    menu_music::attach_credit(credit);
    return button(ctx, right, "Back", close_pressed, &gallery, "back");
}
}  // namespace

ModResult open_gallery(ModContext* ctx) {
    return gallery ? MOD_OK : push(ctx, "Dungeon Rush", build_gallery, &gallery);
}

namespace {
void open_pressed(ModContext* ctx, void*) {
    remember(open_gallery(ctx));
}
ModResult build_panel(ModContext* ctx, UiElementHandle pane, void*, ModError*) {
    CHECK(svc_ui->pane_add_text(ctx, pane, "Dungeon selection and loadout preview.", nullptr));
    return button(ctx, pane, "Open Dungeon Rush", open_pressed, nullptr, "rush-launch");
}
}  // namespace

ModResult on_new_save_select(void*, GameModeNewSaveState* state, ModError*) {
    pending_selection = state;
    *state = GAME_MODE_STATE_PENDING;
    const auto result = open_gallery(mod_ctx);
    if (result != MOD_OK) {
        *state = GAME_MODE_STATE_RETURN;
        pending_selection = nullptr;
    }
    return result;
}
ModResult reset_mode(void*, ModError*) {
    replay::close();
    run::reset();
    video::stop();
    pending_selection = nullptr;
    menu_music::stop();
    if (detail || gallery) {
        close_menu_stack();
    }
    return MOD_OK;
}
ModResult savewarp_reset(void*, ModError*) {
    replay::close();
    run::game_reset();
    video::stop();
    pending_selection = nullptr;
    menu_music::stop();
    if (detail || gallery) {
        close_menu_stack();
    }
    return MOD_OK;
}

bool window_open() {
    return gallery || detail;
}

void extract_previews() {
    if (!video::initialize()) {
        return;
    }
    for (size_t i = 0; i < video_files.size(); ++i) {
        if (!video_files[i]) {
            continue;
        }
        ResourceBuffer buffer = RESOURCE_BUFFER_INIT;
        const auto resource = std::string("videos/") + video_files[i];
        if (svc_resource->load(mod_ctx, resource.c_str(), &buffer) != MOD_OK) {
            svc_log->warn(mod_ctx, ("Unable to load preview: " + resource).c_str());
            continue;
        }
        video_paths[i] = std::filesystem::u8path(svc_host->mod_dir(mod_ctx)) / video_files[i];
        std::ofstream out(video_paths[i], std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(buffer.data), buffer.size);
        out.close();
        video_available[i] = static_cast<bool>(out);
        svc_resource->free(mod_ctx, &buffer);
        if (!video_available[i]) {
            svc_log->warn(mod_ctx, ("Unable to extract preview: " + resource).c_str());
        }
    }
}

ModResult register_entry_points() {
    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_panel;
    CHECK(svc_ui->register_mods_panel(mod_ctx, &panel));
    UiMenuTabDesc tab = UI_MENU_TAB_DESC_INIT;
    tab.label = "Dungeon Rush";
    tab.on_selected = open_pressed;
    return svc_ui->register_menu_tab(mod_ctx, &tab, nullptr);
}

void update_close_stack() {
    update_menu_close();
}

void update_preview() {
    if ((detail && video_available[selected]) || (gallery && !detail && video_available[9])) {
        video::update(0);
    }
}

void shutdown() {
    close_run_options();
    pending_selection = nullptr;
    gallery = detail = 0;
}

void smoke_test([[maybe_unused]] unsigned frame) {
#ifdef DUNGEON_RUSH_UI_SMOKE
    if (frame == 180) {
        remember(open_gallery(mod_ctx));
        svc_log->info(mod_ctx, "UI smoke: gallery");
    }
    if (frame == 600) {
        select_dungeon(mod_ctx, reinterpret_cast<void*>(6));
        svc_log->info(mod_ctx, "UI smoke: City in the Sky");
    }
    if (frame == 1440) {
        close_pressed(mod_ctx, &detail);
    }
    if (frame == 1620) {
        close_pressed(mod_ctx, &gallery);
        svc_log->info(mod_ctx, "UI smoke: close");
    }
#endif
}

}  // namespace rush::menu
