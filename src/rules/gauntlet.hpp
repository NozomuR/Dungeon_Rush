#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace rush::run {
enum class Mode { Dungeon, Boss, DungeonGauntlet, BossGauntlet };
inline bool bosses(Mode m) {
    return m == Mode::Boss || m == Mode::BossGauntlet;
}
inline bool gauntlet(Mode m) {
    return m == Mode::DungeonGauntlet || m == Mode::BossGauntlet;
}
struct Gauntlet {
    std::array<int64_t, 9> times{};
    std::size_t completed = 0;
    bool finish(std::size_t index, int64_t time) {
        if (index != completed || completed == times.size() || time <= 0) {
            return false;
        }
        times[completed++] = time;
        return true;
    }
    int64_t total() const {
        int64_t t = 0;
        for (auto ms : times) {
            t += ms;
        }
        return t;
    }
    bool has_next() const { return completed > 0 && completed < times.size(); }
};
}  // namespace rush::run
