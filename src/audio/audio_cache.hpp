#pragma once

#include <mods/service.hpp>

#include <string>

namespace rush::audio_cache {
// Installs the audio hooks and configures the expanded audio pool.
// Must be called successfully before using the rest of this API.
ModResult initialize();
// Controls whether loaded effect waves are kept resident.
// Disabling also resets per-run reporting state.
void pin(bool active);
enum class Prepare {
    Waiting,  // archives still loading; call again next frame
    Ready,    // every effect archive is resident
    Failed,   // see error()
};
// Loads any missing effect archives, a few at a time.
Prepare prepare();
// The message for Prepare::Failed. Cleared by pin().
const std::string& error();

}  // namespace rush::audio_cache