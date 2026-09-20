#pragma once
// The dungeon page's loadout editor: gear tiles, X/Y equipment with its item drawer,
// hearts, and the ghost-racing controls. Tiles read their state through callbacks, so
// only the X/Y icons need explicit refreshing.
#include <mods/service.hpp>
#include <mods/svc/ui.h>

#include <string>

namespace rush::loadout_ui {

// Adds every section to the pane, in play order: gear, equipment, ghost.
ModResult build(ModContext* ctx, UiElementHandle pane);
// Forget element handles when the page closes.
void reset_handles();
// The run category changed: constrain the loadout and re-filter what is shown.
void category_changed();
// RCSS that maps item icons onto tiles and buttons; appended to the page's stylesheet.
std::string decorator_styles();

}  // namespace rush::loadout_ui
