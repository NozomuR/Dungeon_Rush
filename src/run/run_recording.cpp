// Saving runs: records, ghost files, routes, expiry and picking a recording to race.
#include "run/run_internal.hpp"

#include "ghost/ghost.hpp"
#include "replay/replay.hpp"
#include "replay/replay_files.hpp"
#include "vendor/json.hpp"

#include "d/d_map_path_dmap.h"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace rush::run {

// Finished runs are kept forever. Failed or abandoned attempts (and their ghost
constexpr auto kIncompleteRetention = std::chrono::hours(24 * 7);

std::string time_text(int64_t ms) {
    std::ostringstream out;
    out << ms / 60000 << ':' << std::setfill('0') << std::setw(2) << (ms / 1000) % 60 << '.'
        << std::setw(3) << ms % 1000;
    return out.str();
}

std::string json_string(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            out << '\\' << char(c);
        } else if (c < 32) {
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c)
                << std::dec;
        } else {
            out << char(c);
        }
    }
    out << '"';
    return out.str();
}

void sample_position(int64_t timestamp) {
    auto* link = dComIfGp_getPlayer(0);
    if (!link || !in_run_stage() || dComIfGp_isEnableNextStage()) {
        return;
    }
    if (!std::isfinite(link->current.pos.x) || !std::isfinite(link->current.pos.y) ||
        !std::isfinite(link->current.pos.z)) {
        return;
    }
    if (route.size() >= 72000) {
        route_truncated = true;
        return;
    }
    if (fopAcM_GetRoomNo(link) < 0) {
        return;  // Room assignment is transient during door loads.
    }
    BE(Vec) map_pos = link->current.pos;
    dMapInfo_n::correctionOriginPos(fopAcM_GetRoomNo(link), &map_pos);
    route.push_back({timestamp >= 0 ? timestamp : record.elapsed(now_ms()),
                     dComIfGp_getStartStageName(), fopAcM_GetRoomNo(link), link->current.pos.x,
                     link->current.pos.y, link->current.pos.z, link->shape_angle.y, map_pos.x,
                     map_pos.z});
}

void save_record() {
    try {
        const char* persistent = nullptr;
        if (svc_host->data_dir(mod_ctx, &persistent) != MOD_OK || !persistent) {
            throw std::runtime_error("Persistent run storage is unavailable");
        }
        const auto folder = std::filesystem::u8path(persistent) / "runs";
        std::filesystem::create_directories(folder);
        static unsigned serial = 0;
        const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        const auto name = std::string(kDungeons[active_dungeon].id) + "-" + std::to_string(stamp) +
                          "-" + std::to_string(++serial) + ".json";
        std::string ghost_file;
        if (ghost::has_take()) {
            const auto candidate = std::filesystem::path(name).stem().string() + ".tpg";
            std::string error;
            try {
                if (ghost::save_take(candidate, &error)) {
                    ghost_file = candidate;
                } else {
                    svc_log->warn(mod_ctx, ("Ghost save: " + error).c_str());
                }
            } catch (const std::exception& e) {
                svc_log->warn(mod_ctx, e.what());
            }
        }
        const auto temporary = folder / (name + ".tmp");
        std::ofstream out(temporary, std::ios::binary);
        out.imbue(std::locale::classic());
        out << "{\"schema\":2,\"dungeon\":" << json_string(kDungeons[active_dungeon].id)
            << ",\"ruleset\":\"" << ruleset()
            << "\",\"ranked\":false,\"status\":" << json_string(outcome) << ",\"mode\":"
            << json_string(gauntlet(active_mode) ? (boss_run ? "boss-gauntlet" : "dungeon-gauntlet")
                                                 : (boss_run ? "boss" : "dungeon"))
            << ",\"gauntlet_id\":" << json_string(series_id) << ",\"starting_gear\":" << active_gear
            << ",\"starting_hearts\":" << active_hearts
            << ",\"master_sword\":" << (active_master ? "true" : "false")
            << ",\"starting_shield\":" << json_string(kShields[active_shield].name)
            << ",\"elapsed_ms\":" << record.elapsed(now_ms())
            << ",\"ghost_file\":" << json_string(ghost_file)
            << ",\"ghost_truncated\":" << (ghost::recording_truncated() ? "true" : "false")
            << ",\"route_truncated\":" << (route_truncated ? "true" : "false") << ",\"splits\":[";
        bool first = true;
        for (const auto& split : record.splits) {
            if (!first) {
                out << ',';
            }
            first = false;
            out << "{\"id\":" << json_string(split.id) << ",\"label\":" << json_string(split.label)
                << ",\"ms\":" << split.milliseconds << ",\"item_id\":" << split.item_id << '}';
        }
        out << "],\"route\":[";
        first = true;
        for (const auto& p : route) {
            if (!first) {
                out << ',';
            }
            first = false;
            out << "{\"ms\":" << p.ms << ",\"stage\":" << json_string(p.stage)
                << ",\"room\":" << p.room << ",\"x\":" << p.x << ",\"y\":" << p.y
                << ",\"z\":" << p.z << ",\"yaw\":" << p.angle << ",\"map_x\":" << p.map_x
                << ",\"map_z\":" << p.map_z << '}';
        }
        out << "]}\n";
        out.close();
        if (!out) {
            throw std::runtime_error("Could not write run record");
        }
        std::filesystem::rename(temporary, folder / name);
        last_recording = folder / name;
        if (gauntlet(active_mode)) {
            const auto series_folder = std::filesystem::u8path(persistent) / "gauntlets";
            std::filesystem::create_directories(series_folder);
            nlohmann::json summary = {
                {"schema", 1},
                {"mode", boss_run ? "boss-gauntlet" : "dungeon-gauntlet"},
                {"ruleset", ruleset()},
                {"completed", series.completed},
                {"total_ms", series.total()},
                {"status", series.completed == 9 ? "Finished" : "In progress"},
                {"times", series.times}};
            const auto path = series_folder / (series_id + ".json");
            if (std::filesystem::exists(path)) {
                try {
                    std::ifstream previous(path);
                    auto old = nlohmann::json::parse(previous);
                    summary["replays"] = old.value("replays", nlohmann::json::array());
                } catch (...) {
                }
            }
            if (!summary.contains("replays")) {
                summary["replays"] = nlohmann::json::array();
            }
            summary["replays"].push_back(name);
            std::ofstream summary_out(path);
            summary_out << summary.dump(2);
            summary_out.close();
            if (!summary_out) {
                throw std::runtime_error("Could not save gauntlet record");
            }
        }
        svc_log->info(mod_ctx, ("Dungeon Rush saved " + (folder / name).string()).c_str());
    } catch (const std::exception& error) {
        svc_log->warn(mod_ctx, error.what());
        toast("Could not save run");
    }
}

void prepare_ghost() {
    ghost::clear_loaded();
    if (gauntlet(active_mode) || ghost::race_mode == 0) {
        return;
    }
    if (ghost::race_mode == 3 && !selected_recording.empty()) {
        try {
            std::ifstream in(selected_recording);
            auto j = nlohmann::json::parse(in);
            if (j.at("dungeon") != kDungeons[active_dungeon].id) {
                toast("Selected ghost belongs to a different dungeon");
                return;
            }
            if (!matching_ruleset(j.value("ruleset", "")) || !j.value("gauntlet_id", "").empty()) {
                toast("Selected ghost belongs to a different run mode");
                return;
            }
            const auto name = j.at("ghost_file").get<std::string>();
            auto path = j.contains("online_id") ? selected_recording.parent_path() / name
                                                : ghost::ghosts_dir() / name;
            std::string error;
            if (!ghost::load_ghost_file(path, active_dungeon, &error, true)) {
                toast(("Ghost unavailable: " + error).c_str());
            } else {
                toast("Racing the selected run");
            }
        } catch (const std::exception& e) {
            toast(e.what());
        }
        return;
    }
    const char* dir = nullptr;
    if (svc_host->data_dir(mod_ctx, &dir) != MOD_OK || !dir) {
        return;
    }
    try {
        const auto folder = std::filesystem::u8path(dir) / "runs";
        if (!std::filesystem::exists(folder)) {
            return;
        }
        std::vector<std::pair<int64_t, std::filesystem::path>> candidates;
        for (const auto& file : std::filesystem::directory_iterator(folder)) {
            if (file.path().extension() != ".json" || file.file_size() > 32 * 1024 * 1024) {
                continue;
            }
            try {
                std::ifstream stream(file.path());
                auto j = nlohmann::json::parse(stream);
                if (!j.value("gauntlet_id", "").empty()) {
                    continue;
                }
                if (j.value("dungeon", "") != kDungeons[active_dungeon].id ||
                    !matching_ruleset(j.value("ruleset", "")) ||
                    j.value("starting_gear", uint32_t(0)) != active_gear ||
                    j.value("starting_hearts", 0) != active_hearts ||
                    j.value("master_sword", false) != active_master ||
                    j.value("starting_shield", "") != kShields[active_shield].name ||
                    j.value("ghost_truncated", false)) {
                    continue;
                }
                if (ghost::race_mode == 2 && j.value("status", "") != "Finished") {
                    continue;
                }
                const auto name = j.value("ghost_file", "");
                if (name.empty() ||
                    std::filesystem::path(name).filename() != std::filesystem::path(name) ||
                    std::filesystem::path(name).extension() != ".tpg") {
                    continue;
                }
                candidates.emplace_back(j.at("elapsed_ms").get<int64_t>(), file.path());
            } catch (const std::exception&) {
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return ghost::race_mode == 2 ? a.first < b.first : a.second > b.second;
        });
        for (const auto& candidate : candidates) {
            std::ifstream stream(candidate.second);
            const auto j = nlohmann::json::parse(stream);
            const auto path = ghost::ghosts_dir() / j.at("ghost_file").get<std::string>();
            std::string error;
            try {
                if (ghost::load_ghost_file(path, active_dungeon, &error)) {
                    svc_log->info(mod_ctx,
                                  ("Rush ghost selected: " + path.filename().string()).c_str());
                    toast(ghost::race_mode == 2 ? "Racing your best matching run"
                                                : "Racing your latest matching attempt");
                    return;
                }
            } catch (const std::exception& e) {
                error = e.what();
            }
            svc_log->warn(mod_ctx, ("Ghost skipped: " + error).c_str());
        }
    } catch (const std::exception& e) {
        svc_log->warn(mod_ctx, e.what());
    }
}

void start_ghost(int64_t epoch) {
    ghost::start_recording(epoch);
    if (ghost::has_ghost()) {
        std::string error;
        if (!ghost::start_playback(epoch, &error)) {
            svc_log->warn(mod_ctx, error.c_str());
        }
    }
}

void expire_incomplete_recordings() {
    const char* persistent = nullptr;
    if (svc_host->data_dir(mod_ctx, &persistent) != MOD_OK || !persistent) {
        return;
    }
    const auto data = std::filesystem::u8path(persistent);
    const auto folder = data / "runs";
    if (!std::filesystem::is_directory(folder)) {
        return;
    }
    const auto now = std::filesystem::file_time_type::clock::now();
    unsigned removed = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() != ".json" || entry.path() == selected_recording) {
            continue;
        }
        try {
            if (replay_files::read(entry.path()).value("status", "") == "Finished") {
                continue;
            }
            if (now - entry.last_write_time() < kIncompleteRetention) {
                continue;
            }
            replay_files::erase(replay_files::plan(data, entry.path()));
            ++removed;
        } catch (const std::exception& e) {
            svc_log->warn(mod_ctx, ("Rush: could not expire " + entry.path().filename().string() +
                                    ": " + e.what())
                                       .c_str());
        }
    }
    if (removed) {
        svc_log->info(mod_ctx,
                      ("Rush: removed " + std::to_string(removed) + " expired attempt(s)").c_str());
    }
}

void forget_recording(const std::filesystem::path& path) {
    if (selected_recording == path) {
        selected_recording.clear();
        if (ghost::race_mode == 3) {
            ghost::race_mode = 0;
        }
    }
    if (last_recording == path) {
        last_recording.clear();
    }
}

bool select_recording(const std::filesystem::path& path) {
    try {
        if (std::filesystem::file_size(path) > 16 * 1024 * 1024) {
            return false;
        }
        std::ifstream in(path);
        const auto j = nlohmann::json::parse(in);
        if (!j.value("gauntlet_id", "").empty()) {
            return false;
        }
        const auto file = std::filesystem::path(j.at("ghost_file").get<std::string>());
        if (file.empty() || file != file.filename() || file.extension() != ".tpg") {
            return false;
        }
        const auto gear = j.at("starting_gear").get<uint32_t>();
        const auto hearts = j.at("starting_hearts").get<int>();
        if (gear > 65535 || hearts < 1 || hearts > 20 || j.value("ghost_truncated", false)) {
            return false;
        }
        if (std::none_of(kDungeons.begin(), kDungeons.end(),
                         [&](const auto& d) { return j.at("dungeon") == d.id; })) {
            return false;
        }
        if (std::none_of(kShields.begin(), kShields.end(),
                         [&](const auto& d) { return j.at("starting_shield") == d.name; })) {
            return false;
        }
        const bool master = j.at("master_sword").get<bool>();
        for (size_t i = 0; i < kDungeons.size(); ++i) {
            if (j.at("dungeon") == kDungeons[i].id) {
                auto& loadout = loadouts[i];
                loadout.gear = gear;
                loadout.hearts = hearts;
                loadout.master = master;
                for (size_t shield = 0; shield < kShields.size(); ++shield) {
                    if (j.at("starting_shield") == kShields[shield].name) {
                        loadout.shield = shield;
                    }
                }
            }
        }
        const auto category = record_category(j.value("ruleset", ""));
        selected_category = category == Category::Legacy ? Category::Unrestricted : category;
        selected_recording = path;
        ghost::race_mode = 3;
        return true;
    } catch (...) {
        return false;
    }
}

std::string gauntlet_history() {
    std::string html = "<h2>Gauntlet records</h2>";
    try {
        const char* data = nullptr;
        if (svc_host->data_dir(mod_ctx, &data) != MOD_OK || !data) {
            return html;
        }
        const auto folder = std::filesystem::u8path(data) / "gauntlets";
        std::vector<std::filesystem::path> files;
        if (std::filesystem::exists(folder)) {
            for (const auto& f : std::filesystem::directory_iterator(folder)) {
                if (f.path().extension() == ".json") {
                    files.push_back(f.path());
                }
            }
        }
        std::sort(files.rbegin(), files.rend());
        if (files.empty()) {
            return html + "<p>No gauntlet runs yet</p>";
        }
        for (size_t i = 0; i < std::min<size_t>(20, files.size()); ++i) {
            try {
                std::ifstream in(files[i]);
                const auto j = nlohmann::json::parse(in);
                html +=
                    "<div class='board-header'><span>" +
                    std::string(j.value("mode", "") == "boss-gauntlet" ? "Boss gauntlet"
                                                                       : "Dungeon gauntlet") +
                    " / " +
                    category_names[int(record_category(j.value("ruleset", "")) == Category::Legacy
                                           ? Category::Unrestricted
                                           : record_category(j.value("ruleset", "")))] +
                    " / " + std::to_string(j.value("completed", 0)) + " of 9</span><span>" +
                    time_text(j.value("total_ms", int64_t(0))) + "</span></div>";
            } catch (...) {
            }
        }
    } catch (const std::exception& e) {
        svc_log->warn(mod_ctx, e.what());
    }
    return html;
}

}  // namespace rush::run
