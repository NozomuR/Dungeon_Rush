#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include "rules/dungeon_starts.hpp"
namespace rush::ghost {
inline constexpr uint32_t kMaxFrames = 30 * 60 * 30;
inline constexpr uint64_t kMatrixBudget = 192ull * 1024 * 1024;
struct FileHeader {
    uint32_t magic, version, frameCount, mtxCount, flagCount, stageCount, reserved[4];
};
inline bool valid_header(const FileHeader& h, uint64_t bytes, uint32_t frameSize,
                         uint32_t matrixSize) {
    if (h.magic != 0x54504748 || h.version != 2 || !h.frameCount || h.frameCount > kMaxFrames ||
        !h.stageCount || h.stageCount > 255 || h.mtxCount > uint64_t(h.frameCount) * 107 ||
        h.flagCount > uint64_t(h.frameCount) * 96 ||
        uint64_t(h.mtxCount) * matrixSize > kMatrixBudget || h.reserved[0] != frameSize ||
        h.reserved[1] != matrixSize) {
        return false;
    }
    return bytes == sizeof(FileHeader) + uint64_t(h.stageCount) * 12 +
                        uint64_t(h.frameCount) * frameSize + uint64_t(h.mtxCount) * matrixSize +
                        h.flagCount;
}
template <class Recording>
bool valid_recording(const Recording& rec, std::size_t expected_dungeon) {
    uint64_t previous = 0;
    for (const auto& f : rec.frames) {
        if (f.milliseconds < previous || f.milliseconds > 86400000 || f.form > 1 ||
            f.clothes >= 4 || f.sword >= 4 || f.shield >= 4 || f.held >= 7 || f.roomNo < 0 ||
            f.roomNo >= 64 || !std::isfinite(f.pos.x) || !std::isfinite(f.pos.y) ||
            !std::isfinite(f.pos.z) || !f.jointCount || f.jointCount > 96 ||
            f.stageIdx >= rec.stages.size() ||
            uint64_t(f.mtxOffset) + 1 + f.jointCount + 10 > rec.mtxPool.size() ||
            uint64_t(f.flagOffset) + f.jointCount > rec.scaleFlagPool.size()) {
            return false;
        }
        previous = f.milliseconds;
    }
    for (const auto& s : rec.stages) {
        auto end = std::find(std::begin(s.name), std::end(s.name), '\0');
        if (end == std::end(s.name)) {
            return false;
        }
        const std::string_view stage(s.name, end - s.name);
        if (!dungeon_stage(expected_dungeon, stage)) {
            return false;
        }
    }
    for (const auto& matrix : rec.mtxPool) {
        for (const auto& row : matrix.m) {
            for (float v : row) {
                if (!std::isfinite(v)) {
                    return false;
                }
            }
        }
    }
    return !rec.frames.empty();
}
template <class Frames>
std::optional<size_t> frame_at(const Frames& frames, uint64_t time) {
    if (frames.empty() || time < frames.front().milliseconds || time > frames.back().milliseconds) {
        return {};
    }
    const auto it = std::upper_bound(frames.begin(), frames.end(), time,
                                     [](uint64_t t, const auto& f) { return t < f.milliseconds; });
    const auto index = static_cast<size_t>(std::prev(it) - frames.begin());
    // Hide stale poses during loading gaps rather than holding one in a doorway.
    if (time - frames[index].milliseconds > 250) {
        return {};
    }
    return index;
}
}  // namespace rush::ghost
