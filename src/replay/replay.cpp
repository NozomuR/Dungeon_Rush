#include "replay/replay.hpp"
#include "replay/replay_files.hpp"
#include "replay/replay_model.hpp"
#include "menu/style.hpp"
#include "online/online.hpp"
#include "menu/discord_ui.hpp"
#include "run/run.hpp"
#include "rules/presets.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mods/svc/host.h>
#include <mods/svc/log.h>
#include <mods/svc/resource.h>
#include <mods/svc/ui.h>
#include <sstream>
#include <set>
namespace rush::replay {
namespace {
using Json = nlohmann::json;
std::vector<Point> route;
std::vector<Event> events;
UiWindowHandle window = 0;
UiElementHandle map = 0, cursor = 0, status = 0;
Json bounds;
std::set<std::string> route_only;
Json metadata;
std::filesystem::path record_path;
UiElementHandle network_status = 0;
std::string username;
uint64_t files_revision = 0;
int64_t close_after_delete = 0;
replay_files::Removal removal;
struct Icon {
    std::string file;
    float width, height;
};
std::map<int, Icon> item_icons;
int64_t duration = 0, position = 0, last = 0, painted = 0;
bool playing = true;
int speed = 8;
std::string stage, outcome;
int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
std::string clock(int64_t t) {
    std::ostringstream s;
    s << t / 60000 << ':' << std::setfill('0') << std::setw(2) << (t / 1000) % 60 << '.'
      << std::setw(3) << t % 1000;
    return s.str();
}
std::string escape(std::string s) {
    std::string out;
    for (char c : s) {
        if (c == '&') {
            out += "&amp;";
        } else if (c == '<') {
            out += "&lt;";
        } else if (c == '>') {
            out += "&gt;";
        } else {
            out += c;
        }
    }
    return out;
}
std::string asset(const std::string& name) {
    return "mod://local.dungeon_rush/res/maps/" + name;
}
std::pair<float, float> xy(const Point& p) {
    const auto& b = bounds.at(p.stage);
    return {(p.x - b[0].get<float>()) / b[2].get<float>() * 420,
            (p.z - b[1].get<float>()) / b[2].get<float>() * 420};
}
const Point& at(int64_t t) {
    auto it = std::upper_bound(route.begin(), route.end(), t,
                               [](int64_t t, const Point& p) { return t < p.ms; });
    return it == route.begin() ? route.front() : *std::prev(it);
}
void draw_map(const std::string& next) {
    stage = next;
    std::string html =
        "<div style='position:relative;width:420dp;height:420dp;overflow:hidden;'><img "
        "style='width:420dp;height:420dp;' src='" +
        asset(stage + ".png") + "'/>";
    if (route_only.contains(stage)) {
        html =
            "<div style='position:relative;width:420dp;height:420dp;overflow:hidden;background-color:#06110e;'><p>Route view - no native dungeon map</p>";
    }
    // Keep separate segments across room/stage transitions; never draw a teleport.
    const Point* prev = nullptr;
    unsigned lines = 0;
    for (const auto& p : route) {
        if (p.stage != stage || p.room < 0) {
            prev = nullptr;
            continue;
        }
        if (prev && prev->room == p.room && p.ms - prev->ms <= 2000) {
            auto [x, z] = xy(*prev);
            auto [xx, zz] = xy(p);
            float dx = xx - x, dz = zz - z, len = std::hypot(dx, dz);
            if (len < 2 && p.ms - prev->ms < 1500) {
                continue;
            }
            if (len < 100 && lines++ < 1500) {
                html +=
                    "<div style='position:absolute;left:" + std::to_string(x) +
                    "dp;top:" + std::to_string(z) + "dp;width:" + std::to_string(len) +
                    "dp;height:2dp;background-color:#f5d94ee6;transform-origin:0dp 0dp;transform:rotate(" +
                    std::to_string(std::atan2(dz, dx) * 180 / 3.14159265f) + "deg);'/>";
            }
        }
        prev = &p;
    }
    html += "</div>";
    svc_ui->elem_set_rml(mod_ctx, map, html.c_str());
}
void render() {
    if (route.empty() || !cursor) {
        return;
    }
    const auto& p = at(position);
    if (stage != p.stage) {
        draw_map(p.stage);
    }
    std::string markers;
    for (const auto& e : events) {
        if (e.ms > position) {
            break;
        }
        const auto pickup = position_at(route, e.ms);
        if (pickup.stage != stage || pickup.room < 0) {
            continue;
        }
        auto [ex, ez] = xy(pickup);
        std::string source = asset(e.label.starts_with("Monkey") ? "monkey.png"
                                   : e.label == "Diababa"        ? "boss.png"
                                                                 : "chest.png");
        float w = 20, h = 20;
        if (auto icon = item_icons.find(event_item_id(e)); icon != item_icons.end()) {
            source = "mod://local.dungeon_rush/res/items/" + icon->second.file;
            const float scale = 24.f / std::max(icon->second.width, icon->second.height);
            w = icon->second.width * scale;
            h = icon->second.height * scale;
        }
        markers += "<img style='position:absolute;left:" + std::to_string(ex - w / 2) +
                   "dp;top:" + std::to_string(ez - h / 2) + "dp;width:" + std::to_string(w) +
                   "dp;height:" + std::to_string(h) + "dp;' src='" + source + "'/>";
    }
    if (p.room >= 0) {
        auto [x, z] = xy(position_at(route, position));
        markers += "<img style='position:absolute;left:" + std::to_string(x - 14) +
                   "dp;top:" + std::to_string(z - 14.32f) + "dp;width:28dp;height:28.64dp;' src='" +
                   asset("link.png") + "'/>";
    }
    svc_ui->elem_set_rml(mod_ctx, cursor, markers.c_str());
    std::string event;
    for (const auto& e : events) {
        if (e.ms <= position) {
            event = e.label;
        } else {
            break;
        }
    }
    svc_ui->elem_set_rml(mod_ctx, status,
                         ("<p>" + clock(position) + " / " + clock(duration) + " &nbsp; " +
                          std::to_string(speed) + "x" + (playing ? "" : " - Paused") + "</p><p>" +
                          escape(outcome == "Finished" ? "Completed run" : "Attempt - " + outcome) +
                          "</p><p>" + escape(event) + "</p>")
                             .c_str());
}
void toggle(ModContext*, void*) {
    playing = !playing;
    last = now();
    render();
}
void faster(ModContext*, void*) {
    speed = speed == 1 ? 4 : speed == 4 ? 8 : speed == 8 ? 16 : 1;
    render();
}
void restart(ModContext*, void*) {
    position = 0;
    last = now();
    playing = true;
    render();
}
void seek(ModContext*, void* data) {
    position = events.at(reinterpret_cast<uintptr_t>(data)).ms;
    last = now();
    render();
}
ModResult button(UiElementHandle pane, const char* label, UiPressedFn fn, void* data = nullptr) {
    UiControlDesc c = UI_CONTROL_DESC_INIT;
    c.label = label;
    c.on_pressed = fn;
    c.user_data = data;
    return svc_ui->pane_add_control(mod_ctx, pane, &c, nullptr);
}
void delete_pressed(ModContext*, void*) {
    try {
        const char* root = nullptr;
        if (svc_host->data_dir(mod_ctx, &root) != MOD_OK || !root) {
            throw std::runtime_error("Replay storage unavailable");
        }
        removal = replay_files::plan(std::filesystem::u8path(root), record_path);
        UiDialogAction actions[2] = {UI_DIALOG_ACTION_INIT, UI_DIALOG_ACTION_INIT};
        actions[0].label = "Cancel";
        actions[1].label = "Delete files";
        actions[1].is_disabled = [](ModContext*, void*) {
            return online::busy();
        };
        actions[1].on_pressed = [](ModContext*, UiDialogHandle, void*) {
            try {
                replay_files::erase(removal);
                run::forget_recording(record_path);
                ++files_revision;
                close_after_delete = now() + 600;
                playing = false;
                svc_ui->elem_set_text(mod_ctx, network_status,
                                      "Replay deleted from this computer.");
            } catch (const std::exception& e) {
                svc_ui->elem_set_text(mod_ctx, network_status, e.what());
            }
        };
        UiDialogDesc d = UI_DIALOG_DESC_INIT;
        d.title = "Delete replay?";
        const auto text =
            std::string("<p>Permanently remove this replay and its local ghost/animation files (") +
            std::to_string(removal.files.size()) +
            " files)?</p><p>Published leaderboard entries stay online.</p>";
        d.body_rml = text.c_str();
        d.actions = actions;
        d.action_count = 2;
        svc_ui->dialog_push(mod_ctx, &d, nullptr);
    } catch (const std::exception& e) {
        svc_ui->elem_set_text(mod_ctx, network_status, e.what());
    }
}
ModResult build(ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*,
                ModError*) {
    username = online::display_name();
    button(right, "Discord account", [](ModContext*, void*) { discord_ui::open(); });
    if (!record_path.empty()) {
        if (!metadata.contains("online_id") && metadata.value("status", "") == "Finished" &&
            metadata.value("mode", "dungeon") == "dungeon" &&
            metadata.value("gauntlet_id", "").empty()) {
            UiControlDesc name = UI_CONTROL_DESC_INIT;
            name.kind = UI_CONTROL_STRING;
            name.label = "Leaderboard username";
            name.max_length = 24;
            name.get = [](ModContext*, void*, UiControlValue* v) {
                v->string_value = username.c_str();
            };
            name.set = [](ModContext*, void*, const UiControlValue* v) {
                username = v->string_value ? v->string_value : "";
                online::display_name(username);
            };
            svc_ui->pane_add_control(mod_ctx, right, &name, nullptr);
            UiControlDesc submit = UI_CONTROL_DESC_INIT;
            submit.label = "Submit run to public leaderboard";
            submit.is_disabled = [](ModContext*, void*) {
                return online::busy();
            };
            submit.on_pressed = [](ModContext*, void*) {
                if (online::discord_link().status != "linked") {
                    discord_ui::open();
                } else {
                    online::submit(record_path, username);
                }
            };
            svc_ui->pane_add_control(mod_ctx, right, &submit, nullptr);
        }
        if (metadata.value("gauntlet_id", "").empty()) {
            button(right, "Use this run's loadout and ghost", [](ModContext*, void*) {
                const bool selected = run::select_recording(record_path);
                svc_ui->elem_set_text(
                    mod_ctx, network_status,
                    selected ? "Ghost selected. Begin this dungeon to race it with matching gear."
                             : "This run has no compatible ghost.");
            });
        }
        svc_ui->pane_add_text(mod_ctx, right, "", &network_status);
        UiControlDesc del = UI_CONTROL_DESC_INIT;
        del.label = "Delete replay";
        del.on_pressed = delete_pressed;
        del.is_disabled = [](ModContext*, void*) {
            return online::busy() || close_after_delete != 0;
        };
        UiElementHandle delete_button = 0;
        svc_ui->pane_add_control(mod_ctx, right, &del, &delete_button);
        svc_ui->elem_set_class(mod_ctx, delete_button, "replay-delete", true);
        const auto summary = std::string(metadata.value("online_name", "You")) + " / " +
                             metadata.value("starting_shield", "") + " / " +
                             std::to_string(metadata.value("starting_hearts", 0)) + " hearts";
        svc_ui->pane_add_text(mod_ctx, right, summary.c_str(), nullptr);
        const auto gear = metadata.value("starting_gear", uint32_t(0));
        std::string equipment =
            metadata.value("master_sword", false) ? "Master Sword" : "Ordon Sword";
        for (const auto& item : kGearLabels) {
            if (gear & item.bit) {
                equipment += std::string(" / ") + item.name;
            }
        }
        svc_ui->pane_add_text(mod_ctx, right, equipment.c_str(), nullptr);
    }
    if (route.empty()) {
        svc_ui->pane_add_rml(
            mod_ctx, left, "<h1>Run preview</h1><p>This recording has no supported map replay.</p>",
            nullptr);
        return button(left, "Back", [](ModContext*, void*) { close(); });
    }
    auto r = svc_ui->pane_add_rml(mod_ctx, left, "<div/>", &map);
    if (r != MOD_OK) {
        return r;
    }
    svc_ui->elem_set_class(mod_ctx, map, "replay-map", true);
    r = svc_ui->pane_add_rml(mod_ctx, left, "<div/>", &cursor);
    if (r != MOD_OK) {
        return r;
    }
    svc_ui->elem_set_class(mod_ctx, cursor, "replay-cursor", true);
    svc_ui->pane_add_rml(mod_ctx, left, "<p/>", &status);
    button(left, "Play / Pause", toggle);
    button(left, "Speed", faster);
    button(left, "Restart", restart);
    button(left, "Back", [](ModContext*, void*) { close(); });
    svc_ui->pane_add_rml(mod_ctx, right, "<h2>Splits</h2>", nullptr);
    for (size_t i = 0; i < events.size(); ++i) {
        button(right, (clock(events[i].ms) + "  " + events[i].label).c_str(), seek,
               reinterpret_cast<void*>(i));
    }
    stage.clear();
    render();
    return MOD_OK;
}
}  // namespace
uint64_t revision() {
    return files_revision;
}
void close() {
    close_after_delete = 0;
    if (window) {
        auto w = window;
        window = 0;
        svc_ui->window_close(mod_ctx, w);
    }
    map = cursor = status = 0;
}
void open(const std::filesystem::path& recording) {
    if (window) {
        return;
    }
    close_after_delete = 0;
    speed = 8;
    record_path.clear();
    metadata = Json::object();
    network_status = 0;
    item_icons.clear();
    route.clear();
    events.clear();
    duration = position = 0;
    last = now();
    playing = true;
    stage.clear();
    route_only.clear();
    try {
        ResourceBuffer b = RESOURCE_BUFFER_INIT;
        if (svc_resource->load(mod_ctx, "maps/bounds.json", &b) != MOD_OK) {
            throw std::runtime_error("Map bounds missing");
        }
        std::string text(static_cast<const char*>(b.data), b.size);
        svc_resource->free(mod_ctx, &b);
        bounds = Json::parse(text);
        if (svc_resource->load(mod_ctx, "items/manifest.json", &b) == MOD_OK) {
            std::string manifest(static_cast<const char*>(b.data), b.size);
            svc_resource->free(mod_ctx, &b);
            for (const auto& icon : Json::parse(manifest)) {
                int id = std::stoi(icon.at("item").get<std::string>(), nullptr, 16);
                // The raw rupee texture needs the game's per-item colour tint.
                if (id <= 7) {
                    continue;
                }
                item_icons.emplace(id, Icon{icon.at("file"), icon.at("width"), icon.at("height")});
            }
        }
        const char* dir = nullptr;
        if (svc_host->data_dir(mod_ctx, &dir) != MOD_OK) {
            throw std::runtime_error("Run storage unavailable");
        }
        const auto folder = std::filesystem::u8path(dir) / "runs";
        std::vector<std::filesystem::path> files;
        if (!recording.empty()) {
            files.push_back(recording);
        } else if (std::filesystem::exists(folder)) {
            for (const auto& f : std::filesystem::directory_iterator(folder)) {
                if (f.path().extension() == ".json" && f.file_size() < 32 * 1024 * 1024) {
                    files.push_back(f.path());
                }
            }
        }
        std::sort(files.rbegin(), files.rend());
        for (const auto& path : files) {
            try {
                std::ifstream in(path);
                auto j = Json::parse(in);
                record_path = path;
                metadata = j;
                // Some Castle boss transitions have no MPAT map. Still show
                // their recorded route in its own bounded coordinate view.
                if (j.at("route").size() > 72000) {
                    throw std::runtime_error("Route too large");
                }
                std::map<std::string, std::array<float, 4>> extents;
                for (const auto& p : j.at("route")) {
                    const std::string st = p.at("stage");
                    if (bounds.contains(st)) {
                        continue;
                    }
                    const float x = p.at("map_x"), z = p.at("map_z");
                    if (!std::isfinite(x) || !std::isfinite(z)) {
                        throw std::runtime_error("Invalid map position");
                    }
                    if (!extents.contains(st)) {
                        extents[st] = {x, z, x, z};
                    } else {
                        auto& e = extents[st];
                        e = {std::min(e[0], x), std::min(e[1], z), std::max(e[2], x),
                             std::max(e[3], z)};
                    }
                }
                for (const auto& [st, e] : extents) {
                    const float side = std::max(1000.f, std::max(e[2] - e[0], e[3] - e[1]) * 1.1f);
                    bounds[st] = {(e[0] + e[2] - side) / 2, (e[1] + e[3] - side) / 2, side};
                    route_only.insert(st);
                }
                auto parsed = parse_record(j, bounds);
                route = std::move(parsed.route);
                events = std::move(parsed.events);
                duration = parsed.duration;
                outcome = parsed.outcome;
                break;
            } catch (const std::exception& e) {
                svc_log->warn(mod_ctx, (std::string("Replay skipped: ") + e.what()).c_str());
            }
        }
    } catch (const std::exception& e) {
        svc_log->warn(mod_ctx, e.what());
    }
    static constexpr char css[] =
        R"(body {font-family:Fira Sans;} window {width:1050dp;max-height:90%;} .replay-map {width:420dp;height:420dp;flex:0 0 420dp;} .replay-cursor {position:absolute;left:26dp;top:26dp;width:420dp;height:420dp;pointer-events:none;} pane {position:relative;padding:26dp;})";
    UiTabDesc tab = UI_TAB_DESC_INIT;
    tab.title = "Run preview";
    tab.build = build;
    tab.update = [](ModContext*, void*, ModError*) {
        discord_ui::update(false);
        const auto t = now();
        if (close_after_delete && t >= close_after_delete) {
            close();
            return MOD_OK;
        }
        static std::string last_status;
        auto current_status = online::status();
        if (network_status && current_status != last_status) {
            svc_ui->elem_set_text(mod_ctx, network_status, current_status.c_str());
            last_status = current_status;
        }
        if (playing) {
            position = std::min(duration, position + (t - last) * speed);
        }
        last = t;
        if (position == duration) {
            playing = false;
        }
        if (t - painted >= 50) {
            render();
            painted = t;
        }
        return MOD_OK;
    };
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = &tab;
    desc.tab_count = 1;
    static const std::string style = std::string(kStyle) + css;
    desc.rcss = style.c_str();
    desc.on_closed = [](ModContext*, UiWindowHandle w, void*) {
        if (w == window) {
            window = 0;
            map = cursor = status = 0;
        }
    };
    svc_ui->window_push(mod_ctx, &desc, &window);
}
}  // namespace rush::replay
