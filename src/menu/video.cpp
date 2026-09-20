#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "menu/video.hpp"
#include <mods/svc/hook.h>
#include <mods/svc/log.h>
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include "video_texture_abi.hpp"
#include "menu/video_reader.hpp"

namespace rush::video {
namespace {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
using Register = void (*)(std::string, aurora::rmlui::TextureProvider);
using Unregister = void (*)(std::string_view) noexcept;
using Release = bool (*)(const std::string&, void*);
Register register_provider = nullptr;
Unregister unregister_provider = nullptr;
Release release_texture = nullptr;
constexpr char scheme[] = "dungeon-rush-video";
constexpr const char* urls[] = {"dungeon-rush-video://0", "dungeon-rush-video://1"};
std::thread decoder;
std::atomic<bool> cancelled{false};
std::mutex mutex;
std::condition_variable wake;
Frame pending, shown;
bool ready = false, registered = false;
unsigned slot = 0;
constexpr char preview_url[] = "dungeon-rush-video://preview";
std::string failure;

void decode(std::filesystem::path path) {
    try {
        const detail::MediaSession media;
        detail::VideoReader reader(path);
        Frame working;
        auto epoch = Clock::now();
        LONGLONG first = -1, last_time = 0, last_duration = reader.frame_duration();
        while (!cancelled) {
            LONGLONG timestamp = 0;
            bool end = false;
            auto sample = reader.read(timestamp, end);
            if (end) {
                if (first < 0) {
                    throw std::runtime_error("Preview contains no video frames");
                }
                {
                    std::unique_lock lock(mutex);
                    wake.wait_until(
                        lock,
                        epoch + std::chrono::nanoseconds((last_time + last_duration - first) * 100),
                        [] { return cancelled.load(); });
                }
                if (cancelled) {
                    break;
                }
                reader.rewind();
                epoch = Clock::now();
                first = -1;
                continue;
            }
            if (!sample) {
                continue;
            }
            if (first < 0) {
                first = timestamp;
            }
            last_time = timestamp;
            last_duration = reader.frame_duration();
            sample->GetSampleDuration(&last_duration);
            reader.convert(sample.Get(), working);
            std::unique_lock lock(mutex);
            wake.wait_until(lock, epoch + std::chrono::nanoseconds((timestamp - first) * 100),
                            [] { return cancelled.load(); });
            if (!cancelled) {
                std::swap(pending, working);
                ready = true;
            }
        }
    } catch (const std::exception& error) {
        if (!cancelled) {
            std::lock_guard lock(mutex);
            failure = error.what();
        }
    }
}
}  // namespace

bool initialize() {
    if (svc_hook->resolve(mod_ctx, "aurora::rmlui::register_texture_provider",
                          reinterpret_cast<void**>(&register_provider), nullptr) != MOD_OK ||
        svc_hook->resolve(mod_ctx, "aurora::rmlui::unregister_texture_provider",
                          reinterpret_cast<void**>(&unregister_provider), nullptr) != MOD_OK ||
        svc_hook->resolve(mod_ctx, "Rml::ReleaseTexture",
                          reinterpret_cast<void**>(&release_texture), nullptr) != MOD_OK) {
        svc_log->warn(mod_ctx, "Runtime video texture functions unavailable");
        return false;
    }
    // Provider consumption copies pixels synchronously on the UI thread, which
    // also owns shown. The decoder only accesses pending under mutex.
    register_provider(scheme, [](std::string_view) -> std::optional<aurora::rmlui::RuntimeTexture> {
        // Menu background colour until the first frame arrives; nullopt would log an error.
        static constexpr std::byte placeholder[] = {std::byte{0x07}, std::byte{0x11},
                                                    std::byte{0x0e}, std::byte{0xff}};
        if (shown.pixels.empty()) {
            return aurora::rmlui::RuntimeTexture{1, 1, placeholder, true, false};
        }
        return aurora::rmlui::RuntimeTexture{shown.width, shown.height, shown.pixels, true, false};
    });
    registered = true;
    return true;
}
void start(const std::filesystem::path& path) {
    stop();
    if (!registered) {
        return;
    }
    cancelled = false;
    decoder = std::thread(decode, path);
}
void update(UiElementHandle element) {
    if (!registered) {
        return;
    }
    {
        std::lock_guard lock(mutex);
        if (!failure.empty()) {
            svc_log->warn(mod_ctx, failure.c_str());
            failure.clear();
        }
        if (!ready) {
            return;
        }
        std::swap(pending, shown);
        ready = false;
    }
    release_texture(preview_url, nullptr);
    if (!element) {
        return;
    }
    slot ^= 1;
    release_texture(urls[slot], nullptr);
    const std::string html =
        std::string("<div class='preview-frame video-frame'><img class='preview-video' src='") +
        urls[slot] + "'/></div>";
    svc_ui->elem_set_rml(mod_ctx, element, html.c_str());
    release_texture(urls[slot ^ 1], nullptr);
}
void stop() {
    cancelled = true;
    wake.notify_all();
    if (decoder.joinable()) {
        decoder.join();
    }
    std::lock_guard lock(mutex);
    pending = {};
    ready = false;
    failure.clear();
}
void shutdown() {
    stop();
    if (registered) {
        release_texture(preview_url, nullptr);
        release_texture(urls[0], nullptr);
        release_texture(urls[1], nullptr);
        unregister_provider(scheme);
        registered = false;
    }
    shown = {};
}
}  // namespace rush::video
