#include "audio/audio_cache.hpp"
#include <mods/svc/hook.hpp>
#include <mods/svc/log.h>
#include <aurora/aurora.h>
#include "Z2AudioLib/Z2SceneMgr.h"
#include "Z2AudioLib/Z2AudioMgr.h"
#include "JSystem/JKernel/JKRAram.h"
#include "JSystem/JAudio2/JAUSectionHeap.h"
#include "JSystem/JAudio2/JASBasicWaveBank.h"
#include <atomic>
#include <algorithm>
#include <string>

DEFINE_HOOK(&JKRAram::create, RushAramCreate);
DEFINE_HOOK_SYMBOL("Z2AudioMgr::init", void(Z2AudioMgr*, JKRSolidHeap*, u32, void*, JKRArchive*),
                   RushAudioInit);
DEFINE_HOOK(&Z2SceneMgr::eraseSeWave, RushKeepSe);
DEFINE_HOOK(&JASBasicWaveBank::decWaveTable, RushWaveFallback);

namespace rush::audio_cache {
namespace {

constexpr u32 kMiB = 1024u * 1024;
constexpr u32 kMem2Bytes = 256u * kMiB;       // total ARAM
constexpr u32 kAudioAreaBytes = 192u * kMiB;  // part of ARAM reserved for audio
constexpr u32 kEffectBank = 0;                // wave bank with the sound effects
constexpr unsigned kMaxInFlightLoads = 4;     // archives loading at the same time

// JASWaveArc::mStatus values.
constexpr u32 kWaveUnloaded = 0;
constexpr u32 kWaveLoading = 1;
constexpr u32 kWaveLoaded = 2;

JAUSectionHeap** section = nullptr;

// Everything here runs on the game thread, including the hooks below.
// The audio loader thread only changes archive status and calls incWaveTable,
// which takes the engine's own mutex.
bool expanded = false, pinned = false, reported = false, warned = false;

// Once on, stays on: loaded archives stay in memory between runs, and scene
// changes keep clearing their wave IDs, so aliases need restoring between runs too.
std::atomic<bool> restore_aliases_enabled{false};

std::string failure;

HookAction reserve_audio(ModContext*, void* args, void*, void*) {
    // mDoMch_Create overwrites JFWSystem's setting after mods load. Intercept
    // the allocation boundary, after that overwrite, and never resize live ARAM.
    if (expanded && !JKRAram::getManager()) {
        auto& size = mods::arg_ref<u32>(args, 0);
        size = std::max(size, kAudioAreaBytes);
        svc_log->info(
            mod_ctx,
            ("Rush audio: reserving audio area=" + std::to_string(size) + " bytes").c_str());
    }
    return HOOK_CONTINUE;
}

HookAction initialize_heap(ModContext*, void* args, void*, void*) {
    // mDoAud_Create passes a separate hardcoded 10/11 MiB limit. Give JAS the
    // actual reserved audio area, before it creates its heap or stream buffers.
    auto* aram = JKRAram::getManager();
    if (expanded && aram) {
        const auto reserved = aram->getAudioMemSize();
        mods::arg_ref<u32>(args, 2) = reserved;
        svc_log->info(mod_ctx, ("Rush audio: initializing wave heap from reserved area=" +
                                std::to_string(reserved) + " bytes")
                                   .c_str());
    }
    return HOOK_CONTINUE;
}

void heap_ready(ModContext*, void*, void*, void*) {
    auto* heap = JASWaveArcLoader::getRootHeap();
    const auto size = heap ? heap->getSize() : 0;
    expanded = size >= kAudioAreaBytes;
    svc_log->info(mod_ctx,
                  ("Rush audio: actual wave heap=" + std::to_string(size) + " bytes").c_str());
}

HookAction keep_wave(ModContext*, void*, void* result, void*) {
    if (!pinned || !expanded) {
        return HOOK_CONTINUE;
    }
    *static_cast<bool*>(result) = true;
    return HOOK_SKIP_ORIGINAL;
}

void restore_aliases(ModContext*, void* args, void*, void*) {
    if (!restore_aliases_enabled.load() || !section || !*section) {
        return;
    }
    auto* bank = mods::arg<JASBasicWaveBank*>(args, 0);
    if (bank != (*section)->getWaveBankTable().getWaveBank(kEffectBank)) {
        return;
    }
    // Native unloading clears shared wave IDs without selecting another loaded
    // copy. Rebind only vacant entries through the engine's mutex-protected API.
    for (u32 i = 0; i < bank->getArcCount(); ++i) {
        auto* group = bank->getWaveGroup(i);
        if (group && group->getStatus() == kWaveLoaded) {
            bank->incWaveTable(group);
        }
    }
}

}  // namespace

ModResult initialize() {
    AuroraConfig* config = nullptr;
    int* aram_initialized = nullptr;
#define RESOLVE(name, dest)                                                                       \
    do {                                                                                          \
        const auto r = svc_hook->resolve(mod_ctx, name, reinterpret_cast<void**>(dest), nullptr); \
        if (r != MOD_OK)                                                                          \
            return r;                                                                             \
    } while (false)
    RESOLVE("?g_config@aurora@@3UAuroraConfig@@A", &config);
    RESOLVE("AR_init_flag", &aram_initialized);
    RESOLVE("?sInstance@?$JASGlobalInstance@VJAUSectionHeap@@@@2PEAVJAUSectionHeap@@EA", &section);
#undef RESOLVE

    if (!*aram_initialized) {
        config->mem2Size = std::max(config->mem2Size, kMem2Bytes);
        expanded = true;
        svc_log->info(mod_ctx,
                      "Rush audio: configured 256 MiB ARAM; 192 MiB audio reservation queued");
    } else {
        auto* heap = JASWaveArcLoader::getRootHeap();
        expanded = heap && heap->getSize() >= kAudioAreaBytes;
        if (!expanded) {
            svc_log->warn(mod_ctx,
                          "Rush audio: restart Dusklight to allocate the expanded audio pool");
        }
    }

    auto r = mods::hook::add_pre<RushAramCreate>(reserve_audio);
    if (r != MOD_OK) {
        return r;
    }
    r = mods::hook::add_pre<RushAudioInit>(initialize_heap);
    if (r != MOD_OK) {
        return r;
    }
    r = mods::hook::add_post<RushAudioInit>(heap_ready);
    if (r != MOD_OK) {
        return r;
    }
    r = mods::hook::add_pre<RushKeepSe>(keep_wave);
    if (r != MOD_OK) {
        return r;
    }
    return mods::hook::add_post<RushWaveFallback>(restore_aliases);
}

const std::string& error() {
    return failure;
}

void pin(bool active) {
    failure.clear();
    pinned = active;
    if (!active) {
        reported = false;
        warned = false;
    }
}

Prepare prepare() {
    if (!expanded) {
        failure = "Audio pool is too small, please restart and try again.";
        return Prepare::Failed;
    }
    // The audio system may not exist yet early in a scene load.
    if (!section || !*section) {
        return Prepare::Waiting;
    }
    auto* bank = (*section)->getWaveBankTable().getWaveBank(kEffectBank);
    if (!bank) {
        return Prepare::Waiting;
    }

    restore_aliases_enabled.store(true);

    unsigned pending = 0, missing = 0, total = 0, requested = 0;
    for (u32 i = 0; i < bank->getArcCount(); ++i) {
        auto* arc = bank->getWaveArc(i);
        if (!arc || !arc->getFileSize()) {
            continue;
        }
        ++total;
        if (arc->getStatus() == kWaveLoading) {
            ++pending;
        } else if (arc->getStatus() != kWaveLoaded) {
            ++missing;
        }
    }

    for (u32 i = 0; i < bank->getArcCount() && pending + requested < kMaxInFlightLoads; ++i) {
        auto* arc = bank->getWaveArc(i);
        if (!arc || !arc->getFileSize() || arc->getStatus() != kWaveUnloaded) {
            continue;
        }
        if (arc->load(nullptr)) {
            ++requested;
        } else if (arc->getStatus() == kWaveUnloaded && !warned) {
            warned = true;
            failure = "Audio bank loading failed. Check the audio log or restart Dusklight.";
            svc_log->warn(mod_ctx, ("Rush audio: archive load failed, bank=" +
                                    std::to_string(kEffectBank) + " archive=" + std::to_string(i))
                                       .c_str());
        }
    }

    if (!pending && !missing && !reported) {
        reported = true;
        svc_log->info(
            mod_ctx,
            ("Rush audio: all " + std::to_string(total) + " effect archives resident").c_str());
    }
    if (!failure.empty()) {
        return Prepare::Failed;
    }
    return pending || missing ? Prepare::Waiting : Prepare::Ready;
}

}  // namespace rush::audio_cache
