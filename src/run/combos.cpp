// Button combos for a live run, read straight from the GameCube pad.
//
// This follows the host's own combo table (dusklight-source/src/dusk/game_combos.cpp)
// with one difference: ours must be *held* for a while before they fire, so a slipped
// finger can never restart a run. The host processes its combos before mods tick and
// consumes the buttons it uses, so anything we see here is free for us to use.
// Every host combo is built around R; ours use L so the two can never overlap.

#include "run/combos.hpp"

#include <chrono>

#include "d/d_com_inf_game.h"
#include "m_Do/m_Do_controller_pad.h"
#include "run/run.hpp"

namespace rush::combos {
namespace {

using Clock = std::chrono::steady_clock;

struct HoldCombo {
    u16 hold_mask;        // every one of these buttons must stay held...
    int hold_ms;          // ...for this long before the action fires
    const char* label;    // shown on the timer while holding
    bool (*condition)();  // extra game-state guard
    void (*action)();     // runs once when the hold completes
    u16 consume_mask;     // cleared after firing so the game ignores the release
    u16 suppress_mask;    // hidden from the game on every tick of the hold
};

bool in_live_attempt() {
    return run::attempt_active() && !dComIfGp_isPauseFlag();
}

// Restart the current attempt: same reload as the results screen's Retry.
// D-pad Down also opens the item wheel, so it is hidden while L is held with it.
const HoldCombo kCombos[] = {
    {
        PAD_TRIGGER_L | PAD_BUTTON_DOWN,
        1500,
        "Hold to restart",
        in_live_attempt,
        [] { run::restart(); },
        PAD_TRIGGER_L | PAD_BUTTON_DOWN,
        PAD_BUTTON_DOWN,
    },
};

// Only one combo can be held at a time; the first table entry whose buttons are down wins.
const HoldCombo* holding = nullptr;
Clock::time_point hold_started;

void consume_buttons(u16 mask) {
    auto& pad = mDoCPd_c::getCpadInfo(PAD_1);
    pad.mPressedButtonFlags &= ~mask;
    pad.mButtonFlags &= ~mask;
}

}  // namespace

void process() {
    const auto held = static_cast<u16>(mDoCPd_c::getHold(PAD_1) & 0xFFFF);
    const auto now = Clock::now();

    for (const auto& combo : kCombos) {
        const bool buttons_down = (held & combo.hold_mask) == combo.hold_mask;
        if (!buttons_down || !combo.condition()) {
            if (holding == &combo) {
                holding = nullptr;  // released early: nothing happens
            }
            continue;
        }
        consume_buttons(combo.suppress_mask);
        if (holding != &combo) {
            holding = &combo;
            hold_started = now;
            return;
        }
        if (now - hold_started < std::chrono::milliseconds(combo.hold_ms)) {
            return;  // still counting
        }
        holding = nullptr;
        combo.action();
        consume_buttons(combo.consume_mask);
        return;
    }
}

float hold_progress() {
    if (!holding) {
        return 0.0f;
    }
    const auto held_for =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - hold_started);
    const float progress =
        static_cast<float>(held_for.count()) / static_cast<float>(holding->hold_ms);
    return progress > 1.0f ? 1.0f : progress;
}

const char* hold_label() {
    return holding ? holding->label : "";
}

}  // namespace rush::combos
