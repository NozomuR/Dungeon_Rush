#pragma once
#include "vendor/json.hpp"
#include "rules/presets.hpp"
#include "rules/dungeon_starts.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
namespace rush::replay {
struct Point {
    int64_t ms;
    std::string stage;
    int room;
    float x, z;
};
struct Event {
    int64_t ms;
    std::string label;
    int item_id = -1;
};
inline int event_item_id(const Event& event) {
    if (event.item_id >= 0) {
        return event.item_id;
    }
    // Older recordings did not store the item ID.
    if (event.label == "Gale Boomerang") {
        return 0x40;
    }
    if (event.label.starts_with("Small key")) {
        return 0x20;
    }
    if (event.label == "Big Key") {
        return 0x26;
    }
    return -1;
}
struct ReplayData {
    std::vector<Point> route;
    std::vector<Event> events;
    int64_t duration = 0;
    std::string outcome;
};
inline ReplayData parse_record(const nlohmann::json& j, const nlohmann::json& bounds) {
    const auto outcome = j.value("status", "");
    const bool supported = outcome == "Finished" || outcome == "Defeated" ||
                           outcome == "Cancelled" || outcome == "Restarted" ||
                           outcome == "Left dungeon" || outcome == "Save loaded";
    auto dungeon = std::find_if(kDungeons.begin(), kDungeons.end(),
                                [&](const auto& d) { return j.value("dungeon", "") == d.id; });
    if (dungeon == kDungeons.end() || !supported || j.value("route_truncated", false)) {
        throw std::runtime_error("Unsupported dungeon recording");
    }
    const auto dungeon_index = static_cast<size_t>(dungeon - kDungeons.begin());
    const auto& points = j.at("route");
    if (points.empty() || points.size() > 72000) {
        throw std::runtime_error("Invalid route length");
    }
    ReplayData data;
    data.outcome = outcome;
    for (const auto& p : points) {
        Point q{p.at("ms"), p.at("stage"), p.at("room"), p.at("map_x"), p.at("map_z")};
        if (!dungeon_stage(dungeon_index, q.stage) || !bounds.contains(q.stage) ||
            !std::isfinite(q.x) || !std::isfinite(q.z) || q.ms < 0 || q.room < -1 || q.room >= 64 ||
            (!data.route.empty() && q.ms < data.route.back().ms)) {
            throw std::runtime_error("Invalid route point");
        }
        data.route.push_back(q);
    }
    data.duration = j.at("elapsed_ms");
    if (data.duration <= 0 || data.duration > 86400000 || data.route.back().ms > data.duration) {
        throw std::runtime_error("Invalid duration");
    }
    for (const auto& e : j.at("splits")) {
        Event event{e.at("ms"), e.at("label"), e.value("item_id", -1)};
        if (data.events.size() >= 512 || event.ms < 0 || event.ms > data.duration ||
            event.item_id < -1 || event.item_id > 255 || event.label.size() > 256 ||
            (!data.events.empty() && event.ms < data.events.back().ms)) {
            throw std::runtime_error("Invalid split");
        }
        data.events.push_back(event);
    }
    return data;
}
inline Point position_at(const std::vector<Point>& route, int64_t t) {
    if (route.empty()) {
        throw std::runtime_error("Empty replay");
    }
    auto it = std::upper_bound(route.begin(), route.end(), t,
                               [](int64_t t, const Point& p) { return t < p.ms; });
    auto p = it == route.begin() ? route.front() : *std::prev(it);
    if (p.room >= 0 && it != route.end() && it->stage == p.stage && it->room == p.room &&
        it->ms > p.ms && it->ms - p.ms <= 1000) {
        float f = std::clamp(float(t - p.ms) / float(it->ms - p.ms), 0.f, 1.f);
        p.x += (it->x - p.x) * f;
        p.z += (it->z - p.z) * f;
    }
    return p;
}
}  // namespace rush::replay
