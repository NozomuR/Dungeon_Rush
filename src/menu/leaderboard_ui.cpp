#include "menu/leaderboard_ui.hpp"

#include <mods/svc/host.h>
#include <mods/svc/log.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "menu/loadout_ui.hpp"
#include "menu/menu_ui.hpp"
#include "online/online.hpp"
#include "replay/replay.hpp"
#include "menu/ui_common.hpp"
#include "vendor/json.hpp"

namespace rush::leaderboard_ui {

Board gallery_board, detail_board;

namespace {
using menu::detail;
using ui::remember;
using ui::rml_text;

// Dungeon name on the left, run count in quiet text on the right.
// The dungeon page already names the dungeon, so its board says what it is instead.
std::string board_name(const Board& board) {
    if (&board == &detail_board) {
        return board.public_runs ? "Leaderboard" : "My runs";
    }
    return kDungeons[board.dungeon].name;
}
std::string board_heading(const std::string& dungeon, int total) {
    return "<div class='board-heading'><h2>" + dungeon + "</h2><span class='board-count'>" +
           std::to_string(total) + (total == 1 ? " run" : " runs") + "</span></div>";
}
std::string board_row(std::string rank, std::string name, int64_t ms) {
    if (name.size() > 24) {
        name = name.substr(0, 21) + "...";
    }
    std::ostringstream time;
    time << ms / 60000 << ':' << std::setfill('0') << std::setw(2) << (ms / 1000) % 60 << '.'
         << std::setw(3) << ms % 1000;
    std::ostringstream row;
    row << std::left << std::setw(5) << rank << std::setw(26) << name << std::right << std::setw(10)
        << time.str();
    return row.str();
}
// Clicking a public run opens choices instead of downloading immediately.
struct RunOptions {
    std::string id, name;
    std::size_t dungeon = 0;
    bool mine = false;
    UiDialogHandle dialog = 0;
};
RunOptions run_options;
void confirm_delete_run(ModContext* ctx, UiDialogHandle, void*) {
    run_options.dialog = 0;
    UiDialogAction actions[2] = {UI_DIALOG_ACTION_INIT, UI_DIALOG_ACTION_INIT};
    actions[0].label = "Cancel";
    actions[1].label = "Delete";
    actions[1].on_pressed = [](ModContext*, UiDialogHandle, void*) {
        run_options.dialog = 0;
        online::withdraw(run_options.id);
    };
    const auto body =
        "<div style='display:block;padding:16dp 24dp;text-align:center;font-size:18dp;line-height:1.4;'>Remove <b>" +
        rml_text(run_options.name) +
        "</b> from the public leaderboard?<br/>This cannot be undone." +
        std::string(run_options.mine ? " Your local recording is kept."
                                     : " Removing another player's run is logged.") +
        "</div>";
    UiDialogDesc desc = UI_DIALOG_DESC_INIT;
    desc.title = "Delete run";
    desc.body_rml = body.c_str();
    desc.variant = UI_DIALOG_DANGER;
    desc.actions = actions;
    desc.action_count = 2;
    desc.on_dismiss = [](ModContext*, UiDialogHandle, void*) {
        run_options.dialog = 0;
    };
    if (svc_ui->dialog_push(ctx, &desc, &run_options.dialog) != MOD_OK) {
        run_options.dialog = 0;
    }
}
void open_run_options(ModContext* ctx, const Board& board, std::size_t index) {
    if (index >= board.ids.size() || run_options.dialog) {
        return;
    }
    const auto remote = online::board(board.dungeon);
    const auto entry =
        std::find_if(remote.entries.begin(), remote.entries.end(),
                     [&](const online::Entry& e) { return e.id == board.ids[index]; });
    if (entry == remote.entries.end()) {
        return;
    }
    run_options.id = entry->id;
    run_options.name = entry->name;
    run_options.dungeon = board.dungeon;
    const bool mine = online::mine(*entry);
    run_options.mine = mine;
    const bool moderator = !mine && online::moderator();
    UiDialogAction actions[3] = {UI_DIALOG_ACTION_INIT, UI_DIALOG_ACTION_INIT,
                                 UI_DIALOG_ACTION_INIT};
    size_t count = 0;
    actions[count].label = "Close";
    actions[count++].on_pressed = [](ModContext*, UiDialogHandle, void*) {
        run_options.dialog = 0;
    };
    if (mine || moderator) {
        actions[count].label = mine ? "Delete my run" : "Delete run";
        actions[count].is_disabled = [](ModContext*, void*) {
            return online::busy();
        };
        actions[count++].on_pressed = confirm_delete_run;
    }
    actions[count].label = "Download run";
    actions[count].is_disabled = [](ModContext*, void*) {
        return online::busy();
    };
    actions[count++].on_pressed = [](ModContext*, UiDialogHandle, void*) {
        run_options.dialog = 0;
        online::download(run_options.id);
    };
    std::ostringstream time;
    time << entry->ms / 60000 << ':' << std::setfill('0') << std::setw(2) << (entry->ms / 1000) % 60
         << '.' << std::setw(3) << entry->ms % 1000;
    const auto body =
        "<div style='display:block;padding:16dp 24dp;text-align:center;font-size:18dp;line-height:1.5;'><b>" +
        rml_text(entry->name) + "</b><br/>" + time.str() + " &nbsp;/&nbsp; rank " +
        std::to_string(entry->rank) +
        (mine ? "<br/><span style='color:#a7ddcd;'>This is your run.</span>" : "") +
        "<br/><span style='font-size:15dp;color:#c1d3c9;'>Download to watch the replay or race it as a ghost.</span></div>";
    UiDialogDesc desc = UI_DIALOG_DESC_INIT;
    desc.title = "Leaderboard run";
    desc.body_rml = body.c_str();
    desc.actions = actions;
    desc.action_count = count;
    desc.on_dismiss = [](ModContext*, UiDialogHandle, void*) {
        run_options.dialog = 0;
    };
    if (svc_ui->dialog_push(ctx, &desc, &run_options.dialog) != MOD_OK) {
        run_options.dialog = 0;
    }
}
struct BoardTab {
    Board* board;
    std::size_t dungeon;
};
std::array<BoardTab, 9> board_tabs;
}  // namespace

void close_run_options() {
    if (run_options.dialog) {
        auto d = run_options.dialog;
        run_options.dialog = 0;
        svc_ui->dialog_close(mod_ctx, d);
    }
}

ModResult refresh_board(Board& board) {
    if (board.bosses) {
        board.public_runs = false;
    }
    board.local_revision = replay::revision();
    CHECK(svc_ui->elem_set_class(mod_ctx, board.pane, "board-local", !board.public_runs));
    for (std::size_t i = 0; i < 25; ++i) {
        svc_ui->elem_set_class(mod_ctx, board.remote_labels[i], "board-card-hidden", true);
        svc_ui->elem_set_class(mod_ctx, board.remote_buttons[i], "board-card-hidden", true);
    }
    svc_ui->elem_set_class(mod_ctx, board.list, "board-card-hidden", board.public_runs);
    if (board.public_runs) {
        const auto remote = online::board(board.dungeon);
        board.revision = remote.revision;
        board.ids.clear();
        board.files.clear();
        std::vector<std::string> labels;
        for (const auto& e : remote.entries) {
            if (e.ruleset != category_ruleset(board.category, board.bosses, board.dungeon)) {
                continue;
            }
            labels.push_back(board_row(std::to_string(e.rank), e.name, e.ms));
            board.ids.push_back(e.id);
        }
        std::vector<UiListItem> items;
        for (size_t i = 0; i < labels.size(); ++i) {
            UiListItem row = UI_LIST_ITEM_INIT;
            row.key = i + 1;
            row.label = labels[i].c_str();
            items.push_back(row);
        }
        CHECK(svc_ui->list_set_items(mod_ctx, board.list, items.data(), items.size()));
        std::size_t row_index = 0;
        for (const auto& e : remote.entries) {
            if (e.ruleset != category_ruleset(board.category, board.bosses, board.dungeon) ||
                row_index >= 25) {
                continue;
            }
            auto image = e.avatar.empty()
                             ? std::string("mod://local.dungeon_rush/res/icons/discord.png")
                             : e.avatar;
            std::string safe;
            for (char c : image) {
                if (c == '&') {
                    safe += "&amp;";
                } else if (c == '\'') {
                    safe += "&#39;";
                } else {
                    safe += c;
                }
            }
            const auto rml = "<div class='board-card-content'><img src='" + safe + "'/><span>" +
                             labels[row_index] + "</span></div>";
            CHECK(svc_ui->elem_set_rml(mod_ctx, board.remote_labels[row_index], rml.c_str()));
            CHECK(svc_ui->elem_set_class(mod_ctx, board.remote_labels[row_index],
                                         "board-card-hidden", false));
            CHECK(svc_ui->elem_set_class(mod_ctx, board.remote_buttons[row_index],
                                         "board-card-hidden", false));
            ++row_index;
        }
        const int total =
            remote.ruleset == category_ruleset(board.category, board.bosses, board.dungeon)
                ? remote.total
                : 0;
        return svc_ui->elem_set_rml(mod_ctx, board.title,
                                    board_heading(board_name(board), total).c_str());
    }
    struct Entry {
        int64_t ms;
        bool finished, custom;
        std::string status;
        std::filesystem::path file;
    };
    std::vector<Entry> runs;
    try {
        const char* dir = nullptr;
        if (svc_host->data_dir(mod_ctx, &dir) != MOD_OK || !dir) {
            throw std::runtime_error("Run storage unavailable");
        }
        const auto folder = std::filesystem::u8path(dir) / "runs";
        if (std::filesystem::exists(folder)) {
            for (const auto& file : std::filesystem::directory_iterator(folder)) {
                if (file.path().extension() != ".json" || file.file_size() > 32 * 1024 * 1024) {
                    continue;
                }
                try {
                    std::ifstream stream(file.path());
                    const auto j = nlohmann::json::parse(stream);
                    if (j.value("dungeon", "") != kDungeons[board.dungeon].id) {
                        continue;
                    }
                    if (!j.value("gauntlet_id", "").empty()) {
                        continue;
                    }
                    const auto rule = j.value("ruleset", "");
                    const bool older = record_category(rule) == Category::Legacy;
                    if (older ? board.category != Category::Unrestricted
                              : rule !=
                                    category_ruleset(board.category, board.bosses, board.dungeon)) {
                        continue;
                    }
                    if ((j.value("mode", "dungeon") == "boss") != board.bosses) {
                        continue;
                    }
                    const auto ms = j.at("elapsed_ms").get<int64_t>();
                    if (ms <= 0 || ms > 86400000) {
                        continue;
                    }
                    const auto status = j.value("status", "");
                    const bool custom =
                        j.value("starting_gear", uint32_t(0)) != kDungeons[board.dungeon].gear ||
                        j.value("starting_hearts", 0) != standard_hearts(board.dungeon) ||
                        j.value("master_sword", false) != (board.dungeon >= 3) ||
                        j.value("starting_shield", "") !=
                            (board.dungeon == 0 ? "Ordon Shield" : "Hylian Shield");
                    runs.push_back({ms, status == "Finished", custom, status, file.path()});
                } catch (const std::exception&) {
                }
            }
        }
    } catch (const std::exception& e) {
        svc_log->warn(mod_ctx, e.what());
    }
    std::sort(runs.begin(), runs.end(), [](const auto& a, const auto& b) {
        if (a.finished != b.finished) {
            return a.finished > b.finished;
        }
        return a.finished ? a.ms < b.ms : a.file > b.file;
    });
    board.files.clear();
    std::vector<std::string> labels;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const auto& run = runs[i];
        std::string name = run.finished ? "You" : run.status;
        if (run.custom) {
            name += " / Custom";
        }
        labels.push_back(board_row(run.finished ? std::to_string(i + 1) : "--", name, run.ms));
        board.files.push_back(run.file);
    }
    std::vector<UiListItem> items;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        UiListItem item = UI_LIST_ITEM_INIT;
        item.key = i + 1;
        item.label = labels[i].c_str();
        items.push_back(item);
    }
    CHECK(svc_ui->list_set_items(mod_ctx, board.list, items.data(), items.size()));
    const std::string title = board_heading(board_name(board), int(runs.size())) +
                              (runs.empty() ? "<p>No saved runs yet</p>" : "");
    return svc_ui->elem_set_rml(mod_ctx, board.title, title.c_str());
}
ModResult build_board(ModContext* ctx, UiElementHandle pane, Board& board, bool tabs) {
    board.pane = pane;
    CHECK(svc_ui->elem_set_class(ctx, pane, "leaderboard", true));

    if (tabs) {
        constexpr const char* labels[] = {"FT", "GM", "LT", "AG", "SR", "ToT", "CitS", "PoT", "HC"};

        for (std::size_t i = 0; i < 9; ++i) {
            board_tabs[i] = {&board, i};
            UiControlDesc c = UI_CONTROL_DESC_INIT;
            c.label = labels[i];
            c.user_data = &board_tabs[i];
            c.is_disabled = [](ModContext*, void*) {
                return online::busy();
            };
            c.on_pressed = [](ModContext*, void* data) {
                auto& t = *static_cast<BoardTab*>(data);
                t.board->dungeon = t.dungeon;
                remember(refresh_board(*t.board));
                if (t.board->public_runs) {
                    online::refresh(
                        t.dungeon, 0,
                        category_ruleset(t.board->category, t.board->bosses, t.dungeon));
                }
            };
            c.is_selected = [](ModContext*, void* data) {
                auto& t = *static_cast<BoardTab*>(data);
                return t.board->dungeon == t.dungeon;
            };
            UiElementHandle e = 0;
            CHECK(svc_ui->pane_add_control(ctx, pane, &c, &e));
            CHECK(svc_ui->elem_set_class(ctx, e, "board-tab", true));
            CHECK(svc_ui->elem_set_class(ctx, e, ("board-tab-" + std::to_string(i)).c_str(), true));
        }
    }
    if (tabs) {
        CHECK(svc_ui->pane_add_rml(ctx, pane, "<div class='board-row-break'/>", nullptr));
    }
    for (size_t i = 0; i < board.category_tabs.size(); ++i) {
        board.category_tabs[i] = {&board, Category(i)};
        UiControlDesc category = UI_CONTROL_DESC_INIT;
        category.label = category_names[i];
        category.user_data = &board.category_tabs[i];
        category.is_disabled = [](ModContext*, void*) {
            return online::busy();
        };
        category.is_selected = [](ModContext*, void* data) {
            const auto& tab = *static_cast<CategoryTab*>(data);
            return tab.board->category == tab.category;
        };
        category.on_pressed = [](ModContext*, void* data) {
            const auto& tab = *static_cast<CategoryTab*>(data);
            auto& b = *tab.board;
            b.category = tab.category;
            choose_category(tab.category);
            if (detail) {
                loadout_ui::category_changed();
            }
            remember(refresh_board(b));
            if (b.public_runs) {
                online::refresh(b.dungeon, 0, category_ruleset(b.category, b.bosses, b.dungeon),
                                b.history);
            }
        };
        UiElementHandle element = 0;
        CHECK(svc_ui->pane_add_control(ctx, pane, &category, &element));
        CHECK(svc_ui->elem_set_class(ctx, element, "board-category", true));
    }
    CHECK(svc_ui->pane_add_rml(ctx, pane, "<div class='board-row-break'/>", nullptr));
    if (!board.bosses) {
        for (bool public_runs : {true, false}) {
            UiControlDesc c = UI_CONTROL_DESC_INIT;
            c.label = public_runs ? "Public" : "My runs";
            c.user_data = &board;
            c.on_pressed=public_runs?+[](ModContext*,void* data){auto& b=*static_cast<Board*>(data);b.public_runs=true;remember(refresh_board(b));online::refresh(b.dungeon,0,category_ruleset(b.category,b.bosses,b.dungeon),b.history);}
            :+[](ModContext*,void* data){auto& b=*static_cast<Board*>(data);b.public_runs=false;remember(refresh_board(b));};
            c.is_selected=public_runs?+[](ModContext*,void* data){return static_cast<Board*>(data)->public_runs;}
            :+[](ModContext*,void* data){return !static_cast<Board*>(data)->public_runs;};
            UiElementHandle e = 0;
            CHECK(svc_ui->pane_add_control(ctx, pane, &c, &e));
            CHECK(svc_ui->elem_set_class(ctx, e, "board-source", true));
        }
    }
    CHECK(svc_ui->pane_add_rml(ctx, pane, "<div/>", &board.title));
    CHECK(svc_ui->elem_set_class(ctx, board.title, "board-title", true));
    UiListDesc list = UI_LIST_DESC_INIT;
    list.user_data = &board;
    list.on_pressed = [](ModContext* ctx, UiListHandle, uint64_t key, void* data) {
        const auto& b = *static_cast<Board*>(data);
        if (b.public_runs) {
            if (key && key <= b.ids.size()) {
                open_run_options(ctx, b, key - 1);
            }
        } else if (key && key <= b.files.size()) {
            replay::open(b.files[key - 1]);
        }
    };
    CHECK(svc_ui->pane_add_list(ctx, pane, &list, &board.list));
    for (std::size_t i = 0; i < 25; ++i) {
        CHECK(svc_ui->pane_add_rml(ctx, pane, "<div/>", &board.remote_labels[i]));
        CHECK(svc_ui->elem_set_class(ctx, board.remote_labels[i], "board-card-label", true));
        board.remote_actions[i] = {&board, i};
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.label = "Run options";
        c.user_data = &board.remote_actions[i];
        c.on_pressed = [](ModContext* ctx, void* data) {
            auto& [b, index] = *static_cast<std::pair<Board*, std::size_t>*>(data);
            open_run_options(ctx, *b, index);
        };
        c.is_disabled = [](ModContext*, void*) {
            return online::busy();
        };
        CHECK(svc_ui->pane_add_control(ctx, pane, &c, &board.remote_buttons[i]));
        CHECK(svc_ui->elem_set_class(ctx, board.remote_buttons[i], "board-card-button", true));
    }
    for (int action = 0; action < 3; ++action) {
        UiControlDesc c = UI_CONTROL_DESC_INIT;
        c.user_data = &board;
        c.label = action == 0 ? "Refresh" : action == 1 ? "< Prev" : "Next >";
        c.is_disabled = [](ModContext*, void*) {
            return online::busy();
        };
        c.on_pressed=action==0?+[](ModContext*,void* data){auto& b=*static_cast<Board*>(data);if(b.public_runs){online::refresh(b.dungeon,0,category_ruleset(b.category,b.bosses,b.dungeon),b.history);
}else{ remember(refresh_board(b));
}}
            :action==1?+[](ModContext*,void* data){auto& b=*static_cast<Board*>(data);online::refresh(b.dungeon,std::max(0,online::board(b.dungeon).offset-25),category_ruleset(b.category,b.bosses,b.dungeon),b.history);}
            :+[](ModContext*,void* data){auto& b=*static_cast<Board*>(data);auto r=online::board(b.dungeon);if(r.offset+25<r.total){online::refresh(b.dungeon,r.offset+25,category_ruleset(b.category,b.bosses,b.dungeon),b.history);
}};
        UiElementHandle e = 0;
        CHECK(svc_ui->pane_add_control(ctx, pane, &c, &e));
        CHECK(svc_ui->elem_set_class(ctx, e, "board-tool", true));
        if (action) {
            CHECK(svc_ui->elem_set_class(ctx, e, "board-page", true));
        }
    }
    CHECK(svc_ui->pane_add_text(ctx, pane, "", &board.network_status));
    CHECK(svc_ui->elem_set_class(ctx, board.network_status, "board-status", true));
    if (board.public_runs) {
        online::refresh(board.dungeon, 0,
                        category_ruleset(board.category, board.bosses, board.dungeon));
    }
    return refresh_board(board);
}

void reset() {
    gallery_board = {};
    detail_board = {};
}

void update() {
    for (auto* b : {&gallery_board, &detail_board}) {
        if (!b->pane) {
            continue;
        }
        if ((b == &gallery_board && !menu::gallery) || (b == &detail_board && !menu::detail)) {
            continue;
        }
        if ((b->public_runs && b->revision != online::board(b->dungeon).revision) ||
            (!b->public_runs && b->local_revision != replay::revision())) {
            ui::remember(refresh_board(*b));
        }
        if (b->network_status) {
            svc_ui->elem_set_text(
                mod_ctx, b->network_status,
                (b->public_runs || online::busy() ? online::status() : "").c_str());
        }
    }
    const auto downloaded = online::take_download();
    if (!downloaded.empty()) {
        replay::open(downloaded);
    }
}

}  // namespace rush::leaderboard_ui
