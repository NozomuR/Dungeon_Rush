#pragma once
// Menu music: rotating title tracks in the gallery, the dungeon or boss theme on its page.
#include <mods/service.hpp>
#include <mods/svc/ui.h>

namespace rush::menu_music {

// Registers the streamed tracks with the audio resource service. Call from mod_initialize.
ModResult initialize();
// Picks a different title track; the credit line updates if one is attached.
void choose_track();
// Stops everything the menu started and restores the game's own music unless `immediate`.
void stop(bool immediate = false);
// Advances loading, fades and track rotation. Call every tick.
void update();

// The gallery shows the current track's credit in this element (0 = none).
void attach_credit(UiElementHandle element);
const char* credit();

}  // namespace rush::menu_music
