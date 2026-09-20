#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <istream>
#include <ostream>
#include <vector>

namespace rush::ghost {
enum class Visual : uint8_t {
    Body,
    Hat,
    Face,
    Hands,
    Sword,
    Sheath,
    Shield,
    Held,
    Hook,
    Lantern,
    Chain0,
    Chain1,
    Chain2,
    Chain3,
    BootL,
    BootR,
    Count
};
constexpr size_t visualCount = static_cast<size_t>(Visual::Count);
struct VisualMatrix {
    float m[3][4];
};
struct VisualMaterial {
    std::array<uint16_t, 8> textures;
};
struct VisualPart {
    uint32_t matrices = 0, materials = 0;
    uint64_t hidden = 0;
    uint16_t joints = 0, materialCount = 0;
    uint32_t padding = 0;
};
using VisualFrame = std::array<VisualPart, visualCount>;
struct VisualTrack {
    std::vector<VisualFrame> frames;
    std::vector<VisualMatrix> matrices;
    std::vector<uint8_t> scales;
    std::vector<VisualMaterial> materials;
};
constexpr uint64_t visualBudget = 128ull * 1024 * 1024;
inline uint64_t visual_bytes(const VisualTrack& t) {
    return t.frames.size() * sizeof(VisualFrame) + t.matrices.size() * (sizeof(VisualMatrix) + 1) +
           t.materials.size() * sizeof(VisualMaterial);
}
inline bool valid_visuals(const VisualTrack& t, size_t frames) {
    if (t.frames.size() != frames || t.scales.size() != t.matrices.size() ||
        visual_bytes(t) > visualBudget) {
        return false;
    }
    for (const auto& f : t.frames) {
        for (const auto& p : f) {
            if (p.joints > 96 || p.materialCount > 64) {
                return false;
            }
            if (p.joints && uint64_t(p.matrices) + 1 + p.joints > t.matrices.size()) {
                return false;
            }
            if (uint64_t(p.materials) + p.materialCount > t.materials.size()) {
                return false;
            }
        }
    }
    for (const auto& m : t.matrices) {
        for (const auto& row : m.m) {
            for (float v : row) {
                if (!std::isfinite(v)) {
                    return false;
                }
            }
        }
    }
    for (auto flag : t.scales) {
        if (flag > 1) {
            return false;
        }
    }
    return true;
}
struct VisualHeader {
    uint32_t magic, version, frames, matrices, materials, frameSize;
};
inline bool write_visuals(std::ostream& out, const VisualTrack& t) {
    if (!valid_visuals(t, t.frames.size())) {
        return false;
    }
    VisualHeader h{0x41504754,
                   1,
                   static_cast<uint32_t>(t.frames.size()),
                   static_cast<uint32_t>(t.matrices.size()),
                   static_cast<uint32_t>(t.materials.size()),
                   sizeof(VisualFrame)};
    out.write(reinterpret_cast<const char*>(&h), sizeof h);
    out.write(reinterpret_cast<const char*>(t.frames.data()),
              t.frames.size() * sizeof(VisualFrame));
    out.write(reinterpret_cast<const char*>(t.matrices.data()),
              t.matrices.size() * sizeof(VisualMatrix));
    out.write(reinterpret_cast<const char*>(t.scales.data()), t.scales.size());
    out.write(reinterpret_cast<const char*>(t.materials.data()),
              t.materials.size() * sizeof(VisualMaterial));
    return out.good();
}
inline bool read_visuals(std::istream& in, uint64_t bytes, size_t frames,
                         VisualTrack& destination) {
    VisualHeader h{};
    if (!in.read(reinterpret_cast<char*>(&h), sizeof h)) {
        return false;
    }
    const uint64_t payload = uint64_t(h.frames) * sizeof(VisualFrame) +
                             uint64_t(h.matrices) * (sizeof(VisualMatrix) + 1) +
                             uint64_t(h.materials) * sizeof(VisualMaterial);
    if (h.magic != 0x41504754 || h.version != 1 || h.frames != frames ||
        h.frameSize != sizeof(VisualFrame) || payload > visualBudget ||
        bytes != sizeof h + payload) {
        return false;
    }
    VisualTrack t;
    t.frames.resize(h.frames);
    t.matrices.resize(h.matrices);
    t.scales.resize(h.matrices);
    t.materials.resize(h.materials);
    in.read(reinterpret_cast<char*>(t.frames.data()), t.frames.size() * sizeof(VisualFrame));
    in.read(reinterpret_cast<char*>(t.matrices.data()), t.matrices.size() * sizeof(VisualMatrix));
    in.read(reinterpret_cast<char*>(t.scales.data()), t.scales.size());
    in.read(reinterpret_cast<char*>(t.materials.data()),
            t.materials.size() * sizeof(VisualMaterial));
    if (!in.good() || !valid_visuals(t, frames)) {
        return false;
    }
    destination = std::move(t);
    return true;
}
}  // namespace rush::ghost
