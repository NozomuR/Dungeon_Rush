// The run state machine: how a run starts, what it waits for, how it ends. The other
// run_*.cpp files are the pieces it drives; run_internal.hpp is the state they share.
#include "run/run_internal.hpp"

#include "rules/boss_tests.hpp"
#include "run/combos.hpp"
#include "rules/dungeon_starts.hpp"
#include "menu/equipment_ui.hpp"
#include "ghost/ghost.hpp"
#include "replay/replay.hpp"
#include "replay/replay_files.hpp"
#include "run/timer_ui.hpp"
#include "vendor/json.hpp"

#include "SSystem/SComponent/c_math.h"
#include "d/actor/d_a_b_bq.h"
#include "d/actor/d_a_b_dr.h"
#include "d/actor/d_a_b_ds.h"
#include "d/actor/d_a_b_gm.h"
#include "d/actor/d_a_b_gnd.h"
#include "d/actor/d_a_b_ob.h"
#include "d/actor/d_a_b_yo.h"
#include "d/actor/d_a_b_zant.h"
#include "d/actor/d_a_e_fm.h"
#include "d/actor/d_a_player.h"
#include "d/d_event.h"
#include "d/d_meter2.h"
#include "d/d_meter2_info.h"
#include "d/d_stage.h"
#include "f_op/f_op_overlap_mng.h"
#include "f_op/f_op_scene_mng.h"
#include "f_pc/f_pc_name.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_graphic.h"

#include <cstring>
#include <fstream>

DEFINE_HOOK(&dStage_infoCreate, RushStageInit);
DEFINE_HOOK(&dComIfGs_gameStart, RushGameStart);
DEFINE_HOOK_SYMBOL("src/d/d_s_play.cpp#phase_1", int(void*), RushPlayEntry);
DEFINE_HOOK_SYMBOL("fopAc_Execute", int(void*), RushActorExecute);
DEFINE_HOOK(&dEvt_control_c::Step, RushEventStep);
DEFINE_HOOK(&dMeter2_c::_draw, RushHud);
DEFINE_HOOK_SYMBOL("daNpc_Ks_Execute", int(void*), RushMonkey);
DEFINE_HOOK(&dComIfGs_onTbox, RushTreasure);
DEFINE_HOOK_SYMBOL("daB_BQ_Execute", int(b_bq_class*), RushBoss);
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_e_fm.cpp#daE_FM_Execute", int(e_fm_class*), RushFyrus);
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_b_ob.cpp#daB_OB_Execute", int(b_ob_class*), RushMorpheel);
DEFINE_HOOK(&daB_DS_c::executeBattle2Dead, RushStallordDeath);
DEFINE_HOOK(&daB_YO_c::executeDeath, RushBlizzetaDeath);
DEFINE_HOOK(&daB_DR_c::executeDead, RushArgorokDeath);
DEFINE_HOOK(&daB_ZANT_c::executeLastEndDemo, RushZantDeath);
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_b_gm.cpp#daB_GM_Execute", int(b_gm_class*), RushGohma);
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_b_gnd.cpp#daB_GND_Execute", int(b_gnd_class*), RushGanondorf);
DEFINE_HOOK(&Z2SeMgr::seMoveVolumeAll, RushEffectsVolume);
DEFINE_HOOK(&mDoAud_seStartMenu, RushMenuSound);
DEFINE_HOOK_SYMBOL("dusk::ui::update", void(), RushPresentationTick);

namespace rush::run {

const char* ruleset() {
    return category_ruleset(active_category, bosses(active_mode), active_dungeon);
}

bool matching_ruleset(const std::string& rule) {
    return rule == ruleset() ||
           (active_category == Category::Unrestricted &&
            rule == category_ruleset(Category::Legacy, bosses(active_mode), active_dungeon));
}

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool mode_active() {
    bool active = false;
    return svc_game_mode->is_active(mod_ctx, "local.dungeon_rush", &active) == MOD_OK && active;
}

bool in_run_stage() {
    const std::string_view stage = dComIfGp_getStartStageName();
    if (boss_run) {
        return dungeon_stage(active_dungeon, stage);
    }
    return dungeon_stage(active_dungeon, stage);
}

bool ensure_scene_audio() {
    auto* sound = Z2GetAudioMgr();
    if (!sound || !in_run_stage()) {
        return false;
    }
    // Normally tied to the native BGM-start flag. Rush starts must not depend
    // on that flag surviving menu previews / direct scene entry.
    sound->load2ndDynamicWave();
    const std::string_view stage = dComIfGp_getStartStageName();
    const u8 effects = stage == "D_MN05A" ? 4 : stage == "D_MN05B" ? 3 : 2;
    bool ready = true;
    const auto ensure = [&](u8 bank, bool bgm) {
        const int status = bgm ? sound->getBgmLoadStatus(bank) : sound->getSeLoadStatus(bank);
        if (status == 2) {
            return;
        }
        ready = false;
        if (status == 0) {
            const bool requested = bgm ? sound->loadBgmWave(bank) : sound->loadSeWave(bank);
            if (!requested && !bank_warning) {
                bank_warning = true;
                svc_log->warn(mod_ctx, ("Rush audio: could not load " +
                                        std::string(bgm ? "BGM " : "SE ") + std::to_string(bank))
                                           .c_str());
            }
        }
    };
    for (u8 bank : {u8(0), u8(0x58), u8(1), effects}) {
        ensure(bank, false);
    }
    for (u8 bank : {sound->requestSeWave_1, sound->requestSeWave_2, sound->requestDemoWave}) {
        if (bank && bank != 0x7f) {
            ensure(bank, false);
        }
    }
    for (u8 bank : {sound->requestBgmWave_1, sound->requestBgmWave_2}) {
        if (bank) {
            ensure(bank, true);
        }
    }
    return ready;
}

void toast(const char* title) {
    UiToastDesc desc = UI_TOAST_DESC_INIT;
    desc.title_rml = title;
    desc.duration_ms = 4500;
    svc_ui->push_toast(mod_ctx, &desc);
}

// Attempts the player threw away, or that never began, are not worth a recording.
bool worth_saving(std::string_view status) {
    return status != "Restarted" && status != "Start failed" && status != "Countdown failed" &&
           status != "Audio loading failed";
}

void finish(const char* status) {
    ghost::stop_all();
    const bool was_running = record.running();
    if (was_running) {
        sample_position();
        record.finish(now_ms());
    }
    world_held = false;
    restore_pending = false;
    phase = Phase::Finished;
    outcome = status;
    if (was_running && worth_saving(status)) {
        save_record();
    }
}

ModResult initialize() {
    static_assert(sizeof(dSv_save_c) == 0x958, "Forest seed requires the matching game save ABI");
    ResourceBuffer resource = RESOURCE_BUFFER_INIT;
    if (svc_resource->load(mod_ctx, "runs/forest_entry.bin", &resource) != MOD_OK) {
        return MOD_ERROR;
    }
    if (resource.size != sizeof(baseline)) {
        svc_resource->free(mod_ctx, &resource);
        return MOD_ERROR;
    }
    std::memcpy(&baseline, resource.data, sizeof(baseline));
    svc_resource->free(mod_ctx, &resource);
    auto& p = baseline.mPlayer;
    baseline.mSave[forest_save].getBit().init();
    for (auto& rooms : baseline.mSave2) {
        rooms.init();
    }
    p.mItem.init();
    p.mGetItem.init();
    p.mCollect.init();
    p.mItemRecord.init();
    p.mItem.mItems[SLOT_1] = dItemNo_KANTERA_e;
    p.mItem.mItems[SLOT_20] = dItemNo_FISHING_ROD_1_e;
    p.mItem.mItems[SLOT_23] = dItemNo_PACHINKO_e;
    p.mItem.mItems[SLOT_11] = dItemNo_EMPTY_BOTTLE_e;
    p.mItem.setLineUpItem();
    for (u8 item : {u8(dItemNo_SWORD_e), u8(dItemNo_SHIELD_e), u8(dItemNo_WEAR_KOKIRI_e),
                    u8(dItemNo_KANTERA_e), u8(dItemNo_FISHING_ROD_1_e), u8(dItemNo_PACHINKO_e),
                    u8(dItemNo_EMPTY_BOTTLE_e)}) {
        p.mGetItem.onFirstBit(item);
    }
    p.mCollect.setCollect(COLLECT_SWORD, COLLECT_ORDON_SWORD);
    p.mCollect.setCollect(COLLECT_SHIELD, COLLECT_ORDON_SHIELD);
    p.mCollect.setCollect(COLLECT_CLOTHING, KOKIRI_CLOTHES_FLAG);
    auto& status = p.mPlayerStatusA;
    status.setMaxLife(15);
    status.setLife(12);
    status.setRupee(0);
    status.setMaxOil(21600);
    status.setOil(21600);
    status.setTransformStatus(TF_STATUS_HUMAN);
    status.setSelectEquip(COLLECT_SWORD, dItemNo_SWORD_e);
    status.setSelectEquip(COLLECT_SHIELD, dItemNo_SHIELD_e);
    status.setSelectEquip(COLLECT_CLOTHING, dItemNo_WEAR_KOKIRI_e);
    status.setSelectEquip(B_BUTTON_ITEM, dItemNo_SWORD_e);
    for (int i = 0; i < MAX_SELECT_ITEM; ++i) {
        status.setSelectItemIndex(i, 0xff);
        status.setMixItemIndex(i, 0xff);
    }
    status.setSelectItemIndex(0, SLOT_23);
    status.setSelectItemIndex(1, SLOT_1);
    p.mItemRecord.setPachinkoNum(50);
#define REQUIRE(call)         \
    do {                      \
        auto result = (call); \
        if (result != MOD_OK) \
            return result;    \
    } while (false)
    REQUIRE(mods::hook::add_pre<RushStageInit>(stage_init));
    REQUIRE(mods::hook::add_pre<RushGameStart>(game_start));
    REQUIRE(mods::hook::add_pre<RushPlayEntry>(play_entry));
    REQUIRE(mods::hook::add_pre<RushActorExecute>(hold_actor));
    REQUIRE(mods::hook::add_pre<RushEventStep>(hold_event));
    REQUIRE(audio_cache::initialize());
    REQUIRE(timer_ui::initialize());
    REQUIRE(ghost::initialize());
    REQUIRE(mods::hook::add_pre<RushHud>(countdown_draw));
    REQUIRE(mods::hook::add_pre<RushMonkey>(monkey_pre));
    REQUIRE(mods::hook::add_post<RushMonkey>(monkey_post));
    REQUIRE(mods::hook::add_post<RushTreasure>(treasure_post));
    REQUIRE(mods::hook::add_post<RushBoss>(boss_post));
    REQUIRE(mods::hook::add_post<RushFyrus>(fyrus_post));
    REQUIRE(mods::hook::add_post<RushMorpheel>(morpheel_post));
    REQUIRE(mods::hook::add_post<RushStallordDeath>(stallord_death_post));
    REQUIRE(mods::hook::add_post<RushBlizzetaDeath>(blizzeta_death_post));
    REQUIRE(mods::hook::add_post<RushArgorokDeath>(argorok_post));
    REQUIRE(mods::hook::add_post<RushZantDeath>(zant_post));
    REQUIRE(mods::hook::add_post<RushGohma>(gohma_post));
    REQUIRE(mods::hook::add_post<RushGanondorf>(ganondorf_post));
    REQUIRE(mods::hook::add_pre<RushEffectsVolume>(effects_volume));
    REQUIRE(mods::hook::add_pre<RushMenuSound>(menu_sound));
    REQUIRE(mods::hook::add_post<RushPresentationTick>(presentation_tick));
    // Register the native countdown samples independently of scene bank selection.
    AudioRawWave cue_raw{AUDIO_WAVE_FORMAT_ADPCM4, 16000.f, 0, 0};
    AudioWaveInfo cue_wave{60, false, 0, 33251, &cue_raw};
    REQUIRE(svc_audio_res->replace_wave(mod_ctx, AUDIO_WAVE_BANK_SOUND_EFFECTS, 0x123a,
                                        "res/sounds/ready.adpcm", &cue_wave, nullptr));
    cue_wave.loop_end_sample = 8313;
    REQUIRE(svc_audio_res->replace_wave(mod_ctx, AUDIO_WAVE_BANK_SOUND_EFFECTS, 0x123b,
                                        "res/sounds/start.adpcm", &cue_wave, nullptr));
    REQUIRE(svc_item->observe_gives(mod_ctx, item_given, nullptr, nullptr));
    REQUIRE(svc_hook->resolve(mod_ctx, "aurora::time::set_scale",
                              reinterpret_cast<void**>(&set_scale), nullptr));
    svc_hook->resolve(mod_ctx, "dusk::interp::request_presentation_sync",
                      reinterpret_cast<void**>(&sync_presentation), nullptr);
    REQUIRE(svc_hook->resolve(mod_ctx, "aurora::time::scale", reinterpret_cast<void**>(&get_scale),
                              nullptr));
    auto stream = *svc_audio_res->default_stream_info;
    stream.volume = .65f;
    stream.stop_on_scene_change = false;
    REQUIRE(svc_audio_res->add_sound_table_stream(mod_ctx, "/Audiores/Stream/spirit.ast", &stream,
                                                  nullptr, &results_music_id));
    REQUIRE(svc_audio_res->add_sound_table_stream(mod_ctx, "/Audiores/Stream/fairy.ast", &stream,
                                                  nullptr, &fairy_music_id));
#undef REQUIRE
    initialized = true;
    expire_incomplete_recordings();
    return MOD_OK;
}

bool attempt_active() {
    return phase == Phase::Ready || phase == Phase::Running;
}

void restart() {
    if (!attempt_active()) {
        return;
    }
    request(false, boss_run, active_dungeon);
}

bool available() {
    return initialized && mode_active();
}

bool busy() {
    return phase == Phase::AwaitFile || phase == Phase::Loading || phase == Phase::Ready ||
           phase == Phase::Cinematic;
}

bool can_begin(bool new_file) {
    return available() && !busy() && (new_file || (session_ready && dComIfGp_getPlayer(0)));
}

bool start(bool new_file, Mode mode, std::size_t dungeon) {
    if (!can_begin(new_file)) {
        return false;
    }
    if (dungeon >= kStarts.size()) {
        return false;
    }
    if (record.running()) {
        finish("Restarted");
    }
    active_mode = mode;
    active_category = selected_category;
    series = {};
    series_id.clear();
    if (gauntlet(mode)) {
        dungeon = 0;
        series_id = std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());
    }
    return request(new_file, bosses(mode), dungeon);
}

bool request(bool new_file, bool boss_test, std::size_t dungeon) {
    if (dungeon >= kStarts.size() || (boss_test && dungeon >= kBossTests.size())) {
        return false;
    }
    if (!can_begin(new_file)) {
        return false;
    }
    close_results();
    savewarp_pending = savewarp_loaded = savewarp_scene_ready = false;
    ghost::suspend_scene(false);
    if (record.running()) {
        finish("Restarted");
    }
    audio_cache::pin(true);
    ghost::stop_all();
    ghost_prepared = false;
    boss_run = boss_test;
    active_dungeon = dungeon;
    finish_lead_in_ms = 1000LL * boss_finish_delay[dungeon];
    if (!gauntlet(active_mode)) {
        active_category = selected_category;
    }
    capture_loadout();
    if (boss_run) {
        active_gear |= kDungeons[dungeon].reward;
    }
    if (!gauntlet(active_mode) && ghost::race_mode == 3 && !selected_recording.empty()) {
        try {
            std::ifstream in(selected_recording);
            const auto j = nlohmann::json::parse(in);
            if (j.at("dungeon") == kDungeons[dungeon].id &&
                matching_ruleset(j.value("ruleset", "")) && j.value("gauntlet_id", "").empty()) {
                active_gear = j.at("starting_gear");
                active_hearts = j.at("starting_hearts");
                active_master = j.at("master_sword");
                for (size_t i = 0; i < kShields.size(); ++i) {
                    if (j.at("starting_shield") == kShields[i].name) {
                        active_shield = i;
                    }
                }
            }
        } catch (const std::exception& e) {
            toast(e.what());
            return false;
        }
    }
    active_gear = normalize_gear(active_gear);
    if (active_category != Category::Unrestricted) {
        const auto allowed = allowed_gear(dungeon, active_category, boss_run);
        if ((active_gear & ~allowed) || (active_master && dungeon < 3) ||
            active_hearts > standard_hearts(dungeon) || active_hearts < 3 ||
            (dungeon == 0 && active_shield > 1)) {
            toast("Selected replay loadout is outside this category");
            return false;
        }
    }
    active_gear = normalize_gear(active_gear);
    last_recording.clear();
    boss_intro_seen = false;
    boss_intro_done = false;
    boss_settle_at = 0;
    entrance_settling = false;
    phase_two_seen = false;
    phase_two_audio_restored = false;
    next_audio_check = 0;
    bank_warning = false;
    record = {};
    route.clear();
    route_truncated = false;
    outcome.clear();
    monkey_count = key_count = 0;
    restore_pending = true;
    phase = new_file ? Phase::AwaitFile : Phase::Loading;
    phase_at = now_ms();
    if (!new_file) {
        // Native scene transition fades out and destroys the old dungeon actors.
        world_held = true;
        dComIfGp_setNextStage(entry_stage(), entry_spawn(), entry_room(), entry_layer(), 0.f, 0, 1,
                              0, 0, 1, 3);
    }
    return true;
}

ModResult save_loaded(void*, ModError*) {
    if (!available()) {
        return MOD_OK;
    }
    if (record.running()) {
        // Let the native save restore its own inventory, switches and return
        // entrance. Never apply the fresh-run seed during an active attempt.
        savewarp_pending = true;
        savewarp_loaded = true;
        savewarp_scene_ready = false;
        restore_pending = false;
        world_held = false;
        session_ready = false;
        ghost::suspend_scene(true);
        return MOD_OK;
    }
    audio_cache::pin(true);
    close_results();
    if (phase != Phase::AwaitFile) {
        if (record.running()) {
            finish("Save loaded");
        }
        boss_run = false;
        active_dungeon = 0;
        capture_loadout();
        record = {};
        route.clear();
        monkey_count = key_count = 0;
        route_truncated = false;
        outcome.clear();
    }
    ghost::stop_all();
    ghost_prepared = false;
    restore_pending = true;
    world_held = false;
    session_ready = false;
    phase = Phase::AwaitFile;
    phase_at = now_ms();
    return MOD_OK;
}

void update() {
    if (return_pending) {
        if (fopOvlpM_IsPeek()) {
            return;
        }
        auto* scene = fopScnM_SearchByID(dStage_roomControl_c::getProcID());
        if (!scene) {
            return_pending = false;
            toast("File select is unavailable. Try again.");
            return;
        }
        if (!fopScnM_ChangeReq(scene, fpcNm_NAME_SCENE_e, 0, 5)) {
            return_pending = false;
            toast("Could not return to file select. Try again.");
            return;
        }
        return_pending = false;
        cancel();
        session_ready = false;
        phase = Phase::Idle;
        dComIfGp_offEnableNextStage();
        return;
    }
    if (results_window || (phase == Phase::Cinematic && now_ms() - phase_at >= finish_lead_in_ms)) {
        if (auto* sound = Z2GetAudioMgr()) {
            // Fade the SE master independently of the spirit stream. Use wall
            // time and immediate gains so the cinematic slowdown cannot stall it.
            // JASGlobalInstance<T> is local to the mod DLL; its singleton is not
            // the engine instance. Use the exported audio manager and its owned mixer.
            auto* effects = sound->mSoundMgr.getSeMgr()->getParams();
            if (!effects_volume_owned) {
                original_effects_volume = effects->params_.mVolume;
                effects_volume_owned = true;
            }
            const float fade = results_window
                                   ? 1.f
                                   : std::clamp(float(now_ms() - phase_at - finish_lead_in_ms) /
                                                    float(finish_fade_ms),
                                                0.f, 1.f);
            const float eased = fade * fade * (3.f - 2.f * fade);
            effects->moveVolume(original_effects_volume * (1.f - eased), 0);
            sound->bgmStop(0, 0);
            // Result sequences load their bank on demand; streams need no wave bank.
            const u8 wave = results_track == 2 ? 0x2b : results_track == 3 ? 0x33 : 0;
            bool music_ready = true;
            if (wave && sound->getBgmLoadStatus(wave) != 2) {
                music_ready = false;
                if (sound->getBgmLoadStatus(wave) == 0 && sound->loadBgmWave(wave)) {
                    owned_results_wave = wave;
                }
            }
            if (!results_music && music_ready) {
                results_music_started = false;
                sound->Z2AudioMgr::startSound(JAISoundID(results_sound_id), &results_music,
                                              nullptr);
            }
            if (results_music) {
                if (!results_music_started) {
                    results_music->getAuxiliary().moveVolume(0.f, 0);
                }
                if (results_music->isPrepared()) {
                    if (!results_music_started) {
                        results_music_at = now_ms();
                        results_music_started = true;
                        svc_log->info(mod_ctx, (std::string("Rush: results music prepared: ") +
                                                results_track_names[results_track])
                                                   .c_str());
                    }
                    const float gain =
                        std::clamp(float(now_ms() - results_music_at) / 1400.f, 0.f, 1.f);
                    results_music->getAuxiliary().moveVolume(
                        gain * (results_track >= 2 ? .65f : 1.f), 0);
                }
            }
        }
    }
    if (phase == Phase::Idle || phase == Phase::AwaitFile || phase == Phase::Finished) {
        return;
    }
    if (!mode_active()) {
        reset();
        return;
    }
    const auto now = now_ms();
    if (savewarp_pending) {
        if (!savewarp_loaded || !savewarp_scene_ready || !dComIfGp_getPlayer(0) ||
            !in_run_stage() || dComIfGp_isEnableNextStage() || fopOvlpM_IsPeek()) {
            return;
        }
        savewarp_pending = savewarp_loaded = savewarp_scene_ready = false;
        session_ready = true;
        ghost::suspend_scene(false);
        next_sample = now;
        next_audio_check = 0;
        svc_log->info(mod_ctx, "Rush: savewarp resumed; original clock and recording retained");
    }
    if (phase == Phase::Cinematic) {
        if (now - phase_at >= finish_lead_in_ms && !slowdown_blur_requested) {
            slowdown_blur_requested = true;
            // Passive overlay: the death animation keeps running underneath.
            // Match the two-second slowdown, retaining a light blur at results.
            if (!timer_ui::begin_finish_blur()) {
                svc_log->warn(mod_ctx, "Rush: slowdown blur document could not load");
            }
        }
        // Leave the first moment of the death animation at full speed, then
        // smoothly slow the entire simulation while rendering keeps interpolating.
        const float t =
            std::clamp(float(now - phase_at - finish_lead_in_ms) / float(finish_fade_ms), 0.f, 1.f);
        const float eased = t * t * (3.f - 2.f * t);
        // Ease all the way to a perceptual stop before holding the final frame.
        // A tiny positive scale keeps engine/audio updates alive until the UI opens.
        const float remaining = 1.f - eased;
        set_scale(original_scale * std::max(.002f, remaining * remaining));
        if (now - phase_at >= finish_lead_in_ms + finish_fade_ms) {
            world_held = true;
            phase = Phase::Finished;
            mDoGph_gInf_c::offBlure();
            save_record();
            show_results();
        }
    } else if (phase == Phase::Loading) {
        if (now - phase_at > (boss_run ? 180000 : 45000)) {
            finish("Start failed");
            toast("Run could not start. Try again.");
            return;
        }
        if (restore_pending || !in_run_stage() || dComIfGp_isEnableNextStage() ||
            fopOvlpM_IsPeek() || !dComIfGp_getPlayer(0) || !dComIfGp_getCamera(0) ||
            !dMeter2Info_getMeterClass() || dComIfGp_isPauseFlag()) {
            return;
        }
        const bool scene_ready = ensure_scene_audio();
        const auto cache = audio_cache::prepare();
        if (cache == audio_cache::Prepare::Failed) {
            const auto error = audio_cache::error();
            finish("Audio loading failed");
            audio_cache::pin(false);
            toast(error.c_str());
            return;
        }
        if (cache != audio_cache::Prepare::Ready || !scene_ready || ghost::resources_busy()) {
            return;
        }
        if (!ghost_prepared) {
            prepare_ghost();
            ghost_prepared = true;
        }
        if (!ghost::prepare_loaded()) {
            return;
        }
        if (boss_run) {
            world_held = false;
            // Only the Diababa test enters through the boss door. Ordinary
            // Forest runs start at the dungeon entrance and cannot see this intro.
            if (active_dungeon == 0 && (!boss_intro_done || dComIfGp_event_runCheck())) {
                return;
            }
            if (!boss_settle_at) {
                boss_settle_at = now;
                return;
            }
            if (dComIfGp_event_runCheck() || now - boss_settle_at < 1000) {
                return;
            }
            record.begin(now);
            start_ghost(now);
            phase = Phase::Running;
            world_held = false;
            next_sample = now;
            sample_position();
            return;
        }
        // Native entrance demos advance through actor/event updates. Let them
        // release Link's controls before freezing the scene for Ready/Go.
        if (!entrance_settling) {
            entrance_settling = true;
            world_held = false;
            return;
        }
        auto* player = dComIfGp_getLinkPlayer();
        if (dComIfGp_event_runCheck()) {
            phase_at = now;  // a native cutscene is not a hang
        }
        if (!player || dComIfGp_event_runCheck() || player->mDemo.getDemoType() != 0) {
            return;
        }
        world_held = true;
        if (g_meter2_info.setMeterString(0x515)) {
            world_held = true;
            if (auto* sound = Z2GetAudioMgr()) {
                sound->seStart(Z2SE_SY_SUMO_READY, nullptr, 0, 0, 1.f, 1.f, -1.f, -1.f, 0);
            }
            svc_log->info(mod_ctx, "Rush: scene audio ready; Ready cue requested");
            phase = Phase::Ready;
            phase_at = now;
        }
    } else if (phase == Phase::Ready) {
        if (now - phase_at > 15000) {
            finish("Countdown failed");
            toast("Countdown could not finish. Try again.");
            return;
        }
        if (dComIfGp_isPauseFlag()) {
            phase_at = now;
            return;
        }
        if (now - phase_at < 2000) {
            return;
        }
        dMeter2Info_resetMeterString();
        if (!g_meter2_info.setMeterString(0x516)) {
            return;
        }
        if (auto* sound = Z2GetAudioMgr()) {
            sound->seStart(Z2SE_SY_SUMO_START, nullptr, 0, 0, 1.f, 1.f, -1.f, -1.f, 0);
        }
        svc_log->info(mod_ctx, "Rush: Start cue requested");
        cM_initRnd(0x1357, 0x2468, 0x369c);
        cM_initRnd2(0x1357, 0x2468, 0x369c);
        record.begin(now);
        start_ghost(now);
        phase = Phase::Running;
        world_held = false;
        next_sample = now;
        sample_position();
    } else if (phase == Phase::Running) {
        if (now >= next_audio_check && in_run_stage() && !dComIfGp_isEnableNextStage() &&
            !fopOvlpM_IsPeek()) {
            ensure_scene_audio();
            audio_cache::prepare();
            next_audio_check = now + 250;
        }
        if (!dComIfGp_isEnableNextStage() && !in_run_stage()) {
            finish("Left dungeon");
            return;
        }
        if (dComIfGs_getLife() == 0) {
            finish("Defeated");
            show_results();
            return;
        }
        if (in_run_stage() && dComIfGs_isStageBossEnemy()) {
            if (active_dungeon < 8) {
                start_ending();
                return;
            }
        }
        if (now >= next_sample) {
            sample_position();
            next_sample = now + 100;
        }
    }
}

void cancel() {
    equipment_ui::close(mod_ctx);
    savewarp_pending = savewarp_loaded = savewarp_scene_ready = false;
    ghost::suspend_scene(false);
    ghost::stop_all();
    if (phase == Phase::Cinematic) {
        save_record();
        phase = Phase::Finished;
    } else if (phase != Phase::Idle && phase != Phase::Finished) {
        finish("Cancelled");
    }
    close_results();
    world_held = false;
    restore_pending = false;
    audio_cache::pin(false);
    timer_ui::close();
}

void reset() {
    cancel();
    session_ready = false;
    boss_run = false;
    phase = Phase::Idle;
    monkey_actor = nullptr;
}

void game_reset() {
    if (phase != Phase::Running) {
        reset();
        return;
    }
    // Real time continues through file select. Pose capture leaves a gap until
    // the loaded scene is ready; playback uses the same uninterrupted epoch.
    savewarp_pending = true;
    savewarp_loaded = false;
    savewarp_scene_ready = false;
    restore_pending = false;
    world_held = false;
    session_ready = false;
    monkey_actor = nullptr;
    ghost::suspend_scene(true);
    timer_ui::close();
    svc_log->info(mod_ctx, "Rush: save-and-quit; attempt retained while loading");
}

std::string summary_rml() {
    if (phase != Phase::Finished || outcome.empty()) {
        return {};
    }
    return "<p>" + outcome + " · " + time_text(record.elapsed(now_ms())) + "</p>";
}

}  // namespace rush::run
