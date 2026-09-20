#pragma once
#include <mods/service.hpp>
#include <string>
namespace rush::timer_ui {
ModResult initialize();
// hold_label/hold_progress draw a "Hold to ..." bar under the split; progress 0 hides it.
void update(bool visible, const std::string& time, const std::string& split,
            const std::string& hold_label = "", float hold_progress = 0.0f);
bool begin_finish_blur();
void end_finish_blur();
void close();
}  // namespace rush::timer_ui
