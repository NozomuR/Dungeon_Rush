#pragma once
#include <mods/service.hpp>
#include <string>
#include <cstddef>
#include <filesystem>
#include "rules/gauntlet.hpp"

namespace rush::run {
ModResult initialize();
bool available();
bool busy();
bool can_begin(bool new_file);
bool request(bool new_file, bool boss_test = false, std::size_t dungeon = 0);
bool start(bool new_file, Mode mode, std::size_t dungeon = 0);
// True from READY until the attempt finishes or is cancelled.
bool attempt_active();
// Restarts the current attempt from the dungeon entrance, like Retry on the results screen.
void restart();
std::string gauntlet_history();
ModResult save_loaded(void*, ModError*);
void update();
void cancel();
void reset();
// Native save-and-quit keeps the live attempt; mode changes still call reset().
void game_reset();
void forget_recording(const std::filesystem::path& path);
bool select_recording(const std::filesystem::path& path);
std::string summary_rml();
}  // namespace rush::run
