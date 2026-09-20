#pragma once
#include <mods/service.hpp>
#include <filesystem>
#include <cstdint>
namespace rush::replay {
void open(const std::filesystem::path& recording = {});
void close();
uint64_t revision();
}  // namespace rush::replay
