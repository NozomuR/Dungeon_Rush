#include "menu/menu_music.hpp"

#include <mods/svc/audio_res.h>
#include <mods/svc/log.h>
#include <mods/svc/overlay.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <random>
#include <string>

#include "Z2AudioLib/Z2AudioMgr.h"
#include "Z2AudioLib/Z2SeqMgr.h"
#include "menu/menu_ui.hpp"
#include "menu/ui_common.hpp"

namespace rush::menu_music {
namespace {
using menu::detail;
using menu::gallery;
using menu::menu_section;
using menu::selected;

uint16_t menu_spirit_id = 0, menu_fairy_id = 0;
std::array<JAISoundHandle, 23> music;
std::array<float, 23> gains{}, from_gains{};
constexpr int menu_slots[] = {0, 10, 11, 12, 13};
constexpr const char* menu_credits[] = {
    "Twilight / Twilight Princess", "Spirit's Lament / Twilight Princess",
    "Great Fairy Fountain / Twilight Princess", "Midna's Lament / Twilight Princess",
    "Zora's Domain / Twilight Princess"};
constexpr u32 boss_themes[] = {Z2BGM_BOSSBABA_2,       Z2BGM_BOSSFIREMAN_1,    Z2BGM_BOSS_OCTAEEL_1,
                               Z2BGM_HARAGIGANT_BTL02, Z2BGM_BOSS_SNOWWOMAN_1, Z2BGM_GOMA_BTL02,
                               Z2BGM_DRAGON_BTL02,     Z2BGM_BOSS_ZANT,        Z2BGM_VS_GANON_02};
int menu_track = -1;
UiElementHandle music_credit = 0;
constexpr u32 themes[] = {Z2BGM_DUNGEON_FOREST, Z2BGM_DUNGEON_LV2, Z2BGM_DUNGEON_LV3,
                          Z2BGM_DUNGEON_LV4,    Z2BGM_DUNGEON_LV5, Z2BGM_DUNGEON_LV6,
                          Z2BGM_DUNGEON_LV7,    Z2BGM_DUNGEON_LV8, Z2BGM_DUNGEON_LV9_02};
// Primary BGM wave archives from Z2SceneMgr::setSceneName, in slot order: twilight,
// the nine dungeons, Midna, Zora, the nine bosses. Slots 10 and 11 are streams.
constexpr u32 theme_waves[] = {0x0e, 0x0a, 0x13, 0x15, 0x1a, 0x1d, 0x26, 0x27, 0x28, 0x29, 0x36,
                               0x20, 0x0c, 0x16, 0x1e, 0x4c, 0x2e, 0x4e, 0x1e, 0x39, 0x4a};
int wave_index(int slot) {
    return slot <= 9 ? slot : slot >= 12 ? slot - 2 : -1;
}
std::array<bool, 23> theme_initialized{};
bool wave_warning = false;
std::array<bool, 21> owned_theme_waves{};
void release_theme_wave(std::size_t i) {
    auto* sound = Z2GetAudioMgr();
    if (!sound || !owned_theme_waves[i]) {
        return;
    }
    const auto wave = theme_waves[i];
    if (wave != sound->requestBgmWave_1 && wave != sound->requestBgmWave_2) {
        sound->eraseBgmWave(wave);
    }
    owned_theme_waves[i] = false;
}
using MusicClock = std::chrono::steady_clock;
MusicClock::time_point fade_start;
MusicClock::time_point menu_track_at;
void choose_menu_track() {
    static std::mt19937 rng(static_cast<unsigned>(MusicClock::now().time_since_epoch().count()));
    int next = std::uniform_int_distribution<int>(
        0, static_cast<int>(std::size(menu_slots)) - (menu_track < 0 ? 1 : 2))(rng);
    if (menu_track >= 0 && next >= menu_track) {
        ++next;
    }
    menu_track = next;
    menu_track_at = MusicClock::now();
    if (music_credit) {
        svc_ui->elem_set_text(mod_ctx, music_credit, menu_credits[menu_track]);
    }
}
int target_music = -1;
bool fade_running = false;
bool music_started = false;
u32 previous_main = -1, previous_stream = -1, previous_sub = -1;
void stop_music(bool immediate) {
    for (auto& track : music) {
        if (track) {
            track->stop(0);
        }
        track.releaseSound();
    }
    for (std::size_t i = 0; i < owned_theme_waves.size(); ++i) {
        release_theme_wave(i);
    }
    auto* seq = static_cast<Z2SeqMgr*>(Z2GetAudioMgr());
    if (music_started && seq && !immediate) {
        if (previous_main != u32(-1)) {
            seq->bgmStart(previous_main, 0, 0);
        } else if (previous_stream != u32(-1)) {
            seq->bgmStart(previous_stream, 0, 0);
        }
        if (previous_sub != u32(-1)) {
            seq->subBgmStart(previous_sub);
        }
    }
    music_started = false;
    gains.fill(0);
    from_gains.fill(0);
    target_music = -1;
    fade_running = false;
    theme_initialized.fill(false);
}
void update_music() {
    if (!gallery) {
        return;
    }
    auto* sound = Z2GetAudioMgr();
    auto* seq = static_cast<Z2SeqMgr*>(Z2GetAudioMgr());
    if (!sound || !seq) {
        return;
    }
    if (!music_started) {
        previous_main = seq->getMainBgmID();
        previous_stream = seq->getStreamBgmID();
        previous_sub = seq->getSubBgmID();
        music_started = true;
    }
    // Owned theme handles are separate from the game's background-music handles.
    seq->bgmStop(0, 0);
    if (menu_track < 0) {
        choose_menu_track();
    }
    const int menu_slot = menu_slots[menu_track];
    // Rotate looping tracks too. Finished streams advance instead of leaving silence.
    if (!detail &&
        (MusicClock::now() - menu_track_at >= std::chrono::seconds(150) ||
         (target_music == menu_slot && theme_initialized[menu_slot] && !music[menu_slot]))) {
        choose_menu_track();
    }
    const int wanted =
        detail ? static_cast<int>(selected) + (menu_section == 1 ? 14 : 1) : menu_slots[menu_track];
    const bool dungeon_track = wanted >= 1 && wanted <= 9;
    const int wave_slot = wave_index(wanted);
    if (wanted != target_music) {
        // Different banks can share sample IDs, so previews must not overlap.
        for (auto& track : music) {
            if (track) {
                track->stop(0);
            }
            track.releaseSound();
        }
        for (size_t i = 0; i < owned_theme_waves.size(); ++i) {
            release_theme_wave(i);
        }
        gains.fill(0);
        from_gains.fill(0);
        theme_initialized.fill(false);
        target_music = wanted;
        fade_running = false;
        wave_warning = false;
    }
    if (wave_slot >= 0) {
        const auto wave = theme_waves[wave_slot];
        const auto status = sound->getBgmLoadStatus(wave);
        const bool loaded = status != 0 || sound->loadBgmWave(wave);
        if (status == 0 && loaded) {
            owned_theme_waves[wave_slot] = true;
        }
        if (!loaded && !wave_warning) {
            svc_log->warn(
                mod_ctx, ("Could not load dungeon music samples: " + std::to_string(wave)).c_str());
            wave_warning = true;
        }
        // 0 = unloaded, 1 = asynchronous read pending, 2 = samples available.
        if (sound->getBgmLoadStatus(wave) != 2) {
            return;
        }
    }
    auto& incoming = music[wanted];
    if (!incoming) {
        const u32 id = dungeon_track  ? themes[wanted - 1]
                       : wanted >= 14 ? boss_themes[wanted - 14]
                       : wanted == 12 ? Z2BGM_MIDNA_SOS
                       : wanted == 13 ? Z2BGM_ZORA_VILLAGE
                       : wanted == 0
                           ? Z2BGM_TWILIGHT
                           : (0x02000000u | (wanted == 10 ? menu_spirit_id : menu_fairy_id));
        sound->Z2AudioMgr::startSound(JAISoundID(id), &incoming, nullptr);
        if (incoming) {
            incoming->getAuxiliary().moveVolume(gains[wanted], 0);
            theme_initialized[wanted] = false;
        } else {
            return;
        }
    }
    // Keep the outgoing track audible until the incoming track is ready.
    if (!incoming->isPrepared()) {
        return;
    }
    if (!theme_initialized[wanted]) {
        // Supply the normal entrance-room status to sequences that use port 9.
        if (dungeon_track) {
            sound->mSoundStarter.setPortData(&incoming, 9, 0, -1);
        }
        if (wanted == 21) {
            sound->mSoundStarter.setPortData(&incoming, 9, 3, -1);  // Zant arena phase 2.
        }
        theme_initialized[wanted] = true;
        svc_log->info(mod_ctx, ("Dungeon Rush music ready: " + std::to_string(wanted)).c_str());
    }
    if (wanted == 8) {
        // Z2SeqMgr::changeBgmStatus(0): Palace entrance mix. Port 9 alone
        // does not silence the interior/approaching-hand sequence layers.
        // Reapply as child tracks become available, using only our handle.
        for (int track = 2; track <= 14; ++track) {
            seq->setChildTrackVolume(&incoming, track, track <= 5 ? 1.f : 0.f, 0, -1.f, -1.f);
        }
    }
    if (!fade_running) {
        from_gains = gains;
        fade_start = MusicClock::now();
        fade_running = true;
    }
    const float t = std::clamp(
        std::chrono::duration<float>(MusicClock::now() - fade_start).count() / 1.4f, 0.f, 1.f);
    const float blend = t * t * (3.f - 2.f * t);
    for (std::size_t i = 0; i < music.size(); ++i) {
        const float target = i == static_cast<std::size_t>(wanted) ? 1.f : 0.f;
        gains[i] = std::sqrt((1.f - blend) * from_gains[i] * from_gains[i] + blend * target);
        if (music[i]) {
            music[i]->getAuxiliary().moveVolume(gains[i], 0);
            if (t >= 1.f && target == 0.f) {
                music[i]->stop(0);
                music[i].releaseSound();
                theme_initialized[i] = false;
                const int old_wave = wave_index(static_cast<int>(i));
                // Morpheel and Argorok share an archive; don't free the incoming samples.
                if (old_wave >= 0 &&
                    (wave_slot < 0 || theme_waves[old_wave] != theme_waves[wave_slot])) {
                    release_theme_wave(old_wave);
                }
            }
        }
    }
}
}  // namespace

ModResult initialize() {
    auto info = *svc_audio_res->default_stream_info;
    info.volume = .7f;
    info.stop_on_scene_change = false;
    CHECK(svc_audio_res->add_sound_table_stream(mod_ctx, "/Audiores/Stream/spirit.ast", &info,
                                                nullptr, &menu_spirit_id));
    return svc_audio_res->add_sound_table_stream(mod_ctx, "/Audiores/Stream/fairy.ast", &info,
                                                 nullptr, &menu_fairy_id);
}

void choose_track() {
    choose_menu_track();
}

void stop(bool immediate) {
    stop_music(immediate);
}

void update() {
    update_music();
}

void attach_credit(UiElementHandle element) {
    music_credit = element;
}

const char* credit() {
    return menu_track >= 0 ? menu_credits[menu_track] : "";
}

}  // namespace rush::menu_music
