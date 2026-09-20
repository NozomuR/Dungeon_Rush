#pragma once

namespace rush::combos {

// Reads the pad once per game tick and fires any hold-to-activate combo whose
// buttons have been held long enough. Call from the mod's update, while no menu is open.
void process();

// 0 when nothing is being held, rising to 1 as a combo is about to fire.
// The timer shows this so the player can see the hold registering.
float hold_progress();

// What the held combo will do, e.g. "Hold to restart". Empty when nothing is held.
const char* hold_label();

}  // namespace rush::combos
