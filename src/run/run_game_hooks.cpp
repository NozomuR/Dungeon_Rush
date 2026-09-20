// What the run watches in the game: boss defeats, monkeys, chests, items, and the hooks that
// freeze actors and events while the world is held.
#include "run/run_internal.hpp"

#include "rules/dungeon_starts.hpp"
#include "ghost/ghost.hpp"

#include "SSystem/SComponent/c_math.h"
#include "c/c_damagereaction.h"
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
#include "d/d_item_data.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"

namespace rush::run {

void boss_post(ModContext*, void* args, void*, void*) {
    auto* boss = mods::arg<b_bq_class*>(args, 0);
    if (boss_run && phase == Phase::Loading && boss && in_run_stage()) {
        if (boss->mDemoMode >= 10 && boss->mDemoMode <= 14) {
            boss_intro_seen = true;
        }
        if (boss_intro_seen && boss->mDemoMode == 0) {
            boss_intro_done = true;
        }
    }
    if (phase != Phase::Running || !boss || !in_run_stage()) {
        return;
    }
    if (boss->mDemoMode >= 1 && boss->mDemoMode <= 4) {
        phase_two_seen = true;
    }
    if (phase_two_seen && !phase_two_audio_restored && boss->mDemoMode == 0 &&
        !dComIfGp_event_runCheck()) {
        if (auto* sound = Z2GetAudioMgr()) {
            svc_log->info(mod_ctx, ("Rush audio: phase two; effects banks=" +
                                    std::to_string(sound->getSeLoadStatus(1)) + "," +
                                    std::to_string(sound->getSeLoadStatus(4)) +
                                    " demo=" + std::to_string(sound->getDemoStatus()) +
                                    " fanfare=" + std::to_string(sound->isItemGetDemo()))
                                       .c_str());
            // Keep the native effects mix; missing banks are handled by the cache.
            phase_two_audio_restored = true;
        }
    }
    if (boss->mDemoMode != 50 && boss->mDemoMode != 51) {
        return;
    }
    start_ending();
}

void start_ending() {
    if (phase != Phase::Running || savewarp_pending || !in_run_stage()) {
        return;
    }
    ghost::stop_all();
    const auto end = now_ms();
    record.split("boss", kStarts[active_dungeon].boss, end);
    sample_position(record.elapsed(end));
    record.finish(end);
    outcome = "Finished";
    if (gauntlet(active_mode)) {
        series.finish(active_dungeon, record.elapsed(end));
    }
    choose_results_music();
    phase = Phase::Cinematic;
    phase_at = now_ms();
    original_scale = get_scale();
    scale_owned = true;
    if (auto* sound = Z2GetAudioMgr()) {
        sound->seStart(Z2SE_SY_MG_TIMEUP, nullptr, 0, 0, 1.f, 1.f, -1.f, -1.f, 0);
    }
    svc_log->info(mod_ctx, (std::string("Rush: ending ") + kStarts[active_dungeon].boss +
                            "; slowdown in " + std::to_string(finish_lead_in_ms) + " ms")
                               .c_str());
}

void fyrus_post(ModContext*, void* args, void*, void*) {
    auto* boss = mods::arg<e_fm_class*>(args, 0);
    if (active_dungeon == 1 && boss && boss->mDemoCamMode == 51) {
        start_ending();
    }
}

void morpheel_post(ModContext*, void* args, void*, void*) {
    auto* boss = mods::arg<b_ob_class*>(args, 0);
    if (active_dungeon == 2 && boss && boss->mDemoAction >= 41 && boss->mDemoAction <= 49) {
        start_ending();
    }
}

void stallord_death_post(ModContext*, void* args, void*, void*) {
    auto* boss = mods::arg<daB_DS_c*>(args, 0);
    if (active_dungeon == 3 && boss && boss->mMode >= 1) {
        start_ending();
    }
}

void blizzeta_death_post(ModContext*, void* args, void*, void*) {
    auto* boss = mods::arg<daB_YO_c*>(args, 0);
    if (active_dungeon == 4 && boss && boss->mMode >= 1) {
        start_ending();
    }
}

void argorok_post(ModContext*, void* args, void*, void*) {
    auto* boss = mods::arg<daB_DR_c*>(args, 0);
    if (active_dungeon == 6 && boss && boss->mMoveMode >= 1) {
        start_ending();
    }
}

void zant_post(ModContext*, void* args, void*, void*) {
    auto* boss = mods::arg<daB_ZANT_c*>(args, 0);
    if (active_dungeon == 7 && boss && boss->mMode >= 1) {
        start_ending();
    }
}

void gohma_post(ModContext*, void* args, void*, void*) {
    auto* boss = mods::arg<b_gm_class*>(args, 0);
    if (active_dungeon == 5 && boss && boss->mDemoMode == 41) {
        start_ending();
    }
}

void ganondorf_post(ModContext*, void* args, void*, void*) {
    auto* boss = mods::arg<b_gnd_class*>(args, 0);
    if (active_dungeon == 8 && boss && boss->mDemoCamMode >= 61 && boss->mDemoCamMode <= 65) {
        start_ending();
    }
}

void split(std::string id, std::string label) {
    if (record.split(std::move(id), std::move(label), now_ms())) {
        sample_position(record.splits.back().milliseconds);
    }
}

HookAction hold_actor(ModContext*, void*, void* result, void*) {
    if (world_held && in_run_stage()) {
        if (sync_presentation) {
            sync_presentation();
        }
        *static_cast<int*>(result) = 1;
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

HookAction hold_event(ModContext*, void*, void* result, void*) {
    if (world_held && in_run_stage()) {
        *static_cast<int*>(result) = 1;
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

HookAction monkey_pre(ModContext*, void* args, void*, void*) {
    monkey_actor = mods::arg<void*>(args, 0);
    return HOOK_CONTINUE;
}

void monkey_post(ModContext*, void*, void*, void*) {
    monkey_actor = nullptr;
}

void treasure_post(ModContext*, void* args, void*, void*) {
    if (phase != Phase::Running || !in_run_stage() || !monkey_actor) {
        return;
    }
    const int flag = mods::arg<int>(args, 0);
    const std::string id = "monkey:" + std::to_string(flag);
    const auto before = record.splits.size();
    split(id, "Monkey " + std::to_string(monkey_count + 1));
    if (record.splits.size() != before) {
        ++monkey_count;
    }
}

void item_given(ModContext*, const ItemGiveInfo* item, void*) {
    if (phase != Phase::Running || !in_run_stage()) {
        return;
    }
    std::string label;
    if (item->item == dItemNo_BOOMERANG_e) {
        label = "Gale Boomerang";
    } else if (item->item == 0x43) {
        label = "Hero's Bow";
    } else if (item->item == 0x44) {
        label = "Clawshot";
    } else if (item->item == 0x41) {
        label = "Spinner";
    } else if (item->item == 0x42) {
        label = "Ball and Chain";
    } else if (item->item == 0x46) {
        label = "Dominion Rod";
    } else if (item->item == 0x47) {
        label = "Double Clawshots";
    } else if (item->item == dItemNo_SMALL_KEY_e || item->item == dItemNo_SMALL_KEY2_e) {
        label = "Small key " + std::to_string(key_count + 1);
    } else if (item->item == dItemNo_BOSS_KEY_e) {
        label = "Big Key";
    } else if (item->check_name && std::string_view(item->check_name).starts_with("chest:")) {
        label = "Chest";
    } else {
        return;
    }
    const std::string id =
        item->check_name ? item->check_name
                         : ("item:" + std::to_string(item->item) + ":" + std::to_string(key_count));
    const auto before = record.splits.size();
    split(id, label);
    if (record.splits.size() != before) {
        record.splits.back().item_id = item->item;
    }
    if (record.splits.size() != before &&
        (item->item == dItemNo_SMALL_KEY_e || item->item == dItemNo_SMALL_KEY2_e)) {
        ++key_count;
    }
}

}  // namespace rush::run
