#pragma once
// The mod's windows: the gallery (Dungeons / Bosses / Gauntlets) and the dungeon page.
// Also owns the small amount of state the other menu modules read.
#include <mods/service.hpp>
#include <mods/svc/game_mode.h>
#include <mods/svc/ui.h>

#include <cstddef>

namespace rush::menu {

// State shared with menu_music, loadout_ui and leaderboard_ui. Owned here.
extern UiWindowHandle gallery, detail;           // 0 when closed
extern std::size_t selected;                     // dungeon shown on the dungeon page
extern int menu_section;                         // 0 Dungeons, 1 Bosses, 2 Gauntlets
extern GameModeNewSaveState* pending_selection;  // set while file select waits on us

bool window_open();

// Copies the bundled preview videos next to the mod so the decoder can open them.
void extract_previews();
// Registers the Mods-window panel and menu-bar tab.
ModResult register_entry_points();

ModResult open_gallery(ModContext* ctx);
// Game-mode callbacks.
ModResult on_new_save_select(void*, GameModeNewSaveState* state, ModError*);
ModResult reset_mode(void*, ModError*);
ModResult savewarp_reset(void*, ModError*);

// Per-tick: steps the staggered window closing and pushes the next preview frame.
void update_close_stack();
void update_preview();
void shutdown();
// DUNGEON_RUSH_UI_SMOKE only: walks the menus on a frame schedule.
void smoke_test(unsigned frame);

}  // namespace rush::menu
