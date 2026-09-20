#pragma once
#include <mods/service.hpp>
#include <mods/svc/ui.h>
#include <filesystem>
#include <string>

namespace rush::video {
bool initialize();
void start(const std::filesystem::path& path);
void update(UiElementHandle element);
void stop();
void shutdown();
}  // namespace rush::video
