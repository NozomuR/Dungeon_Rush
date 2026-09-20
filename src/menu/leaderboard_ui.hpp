#pragma once
// Leaderboard pane: public runs from the server or the player's own recordings,
// with category chips, paging, and the per-run options dialog.
#include <mods/service.hpp>
#include <mods/svc/ui.h>

#include <array>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "rules/categories.hpp"

namespace rush::leaderboard_ui {

struct Board;
struct CategoryTab {
    Board* board = nullptr;
    Category category = Category::Glitchless;
};
struct Board {
    std::array<CategoryTab, 3> category_tabs;
    bool bosses = false;
    Category category = selected_category;
    std::size_t dungeon = 0;
    UiElementHandle title = 0;
    UiListHandle list = 0;
    std::array<UiElementHandle, 25> remote_labels{}, remote_buttons{};
    std::array<std::pair<Board*, std::size_t>, 25> remote_actions{};
    std::vector<std::filesystem::path> files;
    bool public_runs = true, history = false;
    uint64_t revision = 0, local_revision = 0;
    UiElementHandle network_status = 0, pane = 0;
    std::vector<std::string> ids;
};

// The gallery's board (with dungeon tabs) and the dungeon page's board.
extern Board gallery_board, detail_board;

// Builds the pane. `tabs` adds the nine dungeon chips.
ModResult build_board(ModContext* ctx, UiElementHandle pane, Board& board, bool tabs);
// Re-renders a board from the online cache or local recordings.
ModResult refresh_board(Board& board);
// Closes any open run-options dialog.
void close_run_options();
// Both boards reset to defaults (when the gallery opens).
void reset();
// Per-tick: re-render boards whose data changed and show network status. Call while a menu is open.
void update();

}  // namespace rush::leaderboard_ui
