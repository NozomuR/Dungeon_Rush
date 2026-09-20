#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <vector>
namespace rush::ghost {
class WireEdges {
    std::set<std::pair<uint16_t, uint16_t>> edges;

public:
    void triangle(uint16_t a, uint16_t b, uint16_t c) {
        if (a == b || b == c || a == c) {
            return;
        }
        for (auto [x, y] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}}) {
            edges.emplace(std::min(x, y), std::max(x, y));
        }
    }
    std::optional<std::vector<uint8_t>> encode(std::span<const uint8_t> vertices, uint16_t count,
                                               uint8_t stride, uint8_t fmt) const {
        if (!stride || fmt > 7 || vertices.size() != size_t(count) * stride) {
            return std::nullopt;
        }
        std::vector<uint8_t> out;
        auto it = edges.begin();
        while (it != edges.end()) {
            const auto n = std::min<size_t>(32767, std::distance(it, edges.end()));
            const auto lineVertices = n * 2;
            out.push_back(0xa8 | fmt);  // GX_LINES, preserving the vertex format.
            out.push_back(static_cast<uint8_t>(lineVertices >> 8));
            out.push_back(static_cast<uint8_t>(lineVertices));
            for (size_t i = 0; i < n; ++i, ++it) {
                for (uint16_t v : {it->first, it->second}) {
                    if (v >= count) {
                        return std::nullopt;
                    }
                    auto data = vertices.subspan(size_t(v) * stride, stride);
                    out.insert(out.end(), data.begin(), data.end());
                }
            }
        }
        return out;
    }
};
}  // namespace rush::ghost
