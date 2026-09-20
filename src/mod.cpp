// Entry points. Everything the menus do lives in the menu_* / *_ui modules;
// this file wires services, the game mode, and the per-tick update order.
#include <mods/service.hpp>
#include <mods/svc/actor.h>
#include <mods/svc/audio_res.h>
#include <mods/svc/game_mode.h>
#include <mods/svc/hook.h>
#include <mods/svc/host.h>
#include <mods/svc/item.h>
#include <mods/svc/log.h>
#include <mods/svc/overlay.h>
#include <mods/svc/resource.h>
#include <mods/svc/ui.h>

#include <filesystem>

#include "run/combos.hpp"
#include "menu/discord_ui.hpp"
#include "ghost/ghost.hpp"
#include "menu/leaderboard_ui.hpp"
#include "menu/menu_music.hpp"
#include "menu/menu_ui.hpp"
#include "online/online.hpp"
#include "replay/replay.hpp"
#include "run/run.hpp"
#include "menu/ui_common.hpp"
#include "menu/video.hpp"

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(UiService, svc_ui);
IMPORT_SERVICE(GameModeService, svc_game_mode);
IMPORT_SERVICE(AudioResService, svc_audio_res);
IMPORT_SERVICE(OverlayService, svc_overlay);
IMPORT_SERVICE(ResourceService, svc_resource);
IMPORT_SERVICE(HostService, svc_host);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(ItemService, svc_item);
IMPORT_SERVICE(ActorService, svc_actor);

namespace {
constexpr char kModeId[] = "local.dungeon_rush";
}

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    rush::menu::extract_previews();
    CHECK(rush::menu_music::initialize());
    CHECK(rush::menu::register_entry_points());
    GameModeDesc mode = {sizeof(GameModeDesc),
                         kModeId,
                         "Dungeon Rush",
                         "dungeon_rush_preview",
                         nullptr,
                         nullptr,
                         rush::menu::reset_mode,
                         nullptr,
                         rush::run::save_loaded,
                         rush::run::save_loaded,
                         rush::menu::on_new_save_select,
                         rush::menu::savewarp_reset,
                         nullptr};
    CHECK(svc_game_mode->register_game_mode(mod_ctx, &mode));
    const char* persistent = nullptr;
    CHECK(svc_host->data_dir(mod_ctx, &persistent));
    try {
        rush::online::initialize(std::filesystem::u8path(persistent));
    } catch (const std::exception& e) {
        svc_log->warn(mod_ctx, e.what());
        return MOD_ERROR;
    }
    CHECK(rush::run::initialize());
    svc_log->info(mod_ctx, "Dungeon Rush 0.13.62: menu plays the twilight theme instead of the LoFi track");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
#ifdef DUNGEON_RUSH_UI_SMOKE
    static unsigned frame = 0;
    rush::menu::smoke_test(++frame);
#endif
    if (rush::menu::window_open()) {
        rush::leaderboard_ui::update();
    }
    rush::menu::update_close_stack();
    rush::menu_music::update();
    rush::discord_ui::update(rush::menu::gallery != 0);
    if (!rush::menu::window_open()) {
        rush::combos::process();  // D-pad is menu navigation while a window is open
    }
    rush::run::update();
    rush::menu::update_preview();
    return rush::ui::deferred_error();
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    rush::discord_ui::close();
    rush::leaderboard_ui::close_run_options();
    rush::online::shutdown();
    rush::replay::close();
    rush::run::reset();
    CHECK(rush::ghost::shutdown());
    rush::video::shutdown();
    rush::menu_music::stop(true);
    rush::menu::shutdown();
    return MOD_OK;
}
}
