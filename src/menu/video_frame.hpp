#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>
#include <emmintrin.h>

namespace rush::video {
struct Frame {
    uint32_t width = 0, height = 0;
    std::vector<std::byte> pixels;
};

class FrameMailbox {
public:
    void publish(Frame& working) {
        std::lock_guard lock(mutex_);
        std::swap(pending_, working);
        ready_ = true;
    }
    bool consume(Frame& displayed) {
        std::lock_guard lock(mutex_);
        if (!ready_) {
            return false;
        }
        std::swap(pending_, displayed);
        ready_ = false;
        return true;
    }
    void clear() {
        std::lock_guard lock(mutex_);
        pending_ = {};
        ready_ = false;
    }

private:
    std::mutex mutex_;
    Frame pending_;
    bool ready_ = false;
};

inline void convert_rgb32(const uint8_t* bytes, size_t length, uint32_t width, uint32_t height,
                          int32_t stride, Frame& output) {
    const size_t pitch = static_cast<size_t>(stride < 0 ? -int64_t(stride) : stride);
    // Bound allocations and arithmetic even if a malformed file reports huge dimensions.
    if (!bytes || !width || !height || width > 8192 || height > 8192 || pitch < size_t(width) * 4 ||
        pitch > std::numeric_limits<size_t>::max() / height ||
        length < pitch * (height - 1) + size_t(width) * 4) {
        throw std::runtime_error("Invalid RGB32 frame dimensions, stride or buffer length");
    }
    output.width = width;
    output.height = height;
    output.pixels.resize(size_t(width) * height * 4);
    const auto green = _mm_set1_epi32(0x0000ff00);
    const auto red_blue = _mm_set1_epi32(0x00ff00ff);
    const auto alpha = _mm_set1_epi32(-16777216);
    for (uint32_t y = 0; y < height; ++y) {
        const auto* row = bytes + size_t(stride < 0 ? height - 1 - y : y) * pitch;
        auto* out = output.pixels.data() + size_t(y) * width * 4;
        uint32_t x = 0;
        // SSE2 is baseline on the supported Windows x64 runtime: four pixels at a time.
        for (; x + 4 <= width; x += 4) {
            const auto bgra = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + x * 4));
            const auto rb = _mm_and_si128(bgra, red_blue);
            const auto swapped = _mm_or_si128(_mm_slli_epi32(rb, 16), _mm_srli_epi32(rb, 16));
            const auto rgba =
                _mm_or_si128(alpha, _mm_or_si128(swapped, _mm_and_si128(bgra, green)));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + x * 4), rgba);
        }
        for (; x < width; ++x) {
            out[x * 4] = std::byte(row[x * 4 + 2]);
            out[x * 4 + 1] = std::byte(row[x * 4 + 1]);
            out[x * 4 + 2] = std::byte(row[x * 4]);
            out[x * 4 + 3] = std::byte{255};
        }
    }
}
}  // namespace rush::video
