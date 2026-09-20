// Seeding the save before a run: the baseline file, per-dungeon flags, the loadout, and the
// hooks that catch the game as it loads the destination stage.
#include "run/run_internal.hpp"

#include "rules/boss_tests.hpp"
#include "rules/dungeon_starts.hpp"
#include "menu/equipment_ui.hpp"

#include "c/c_damagereaction.h"
#include "d/d_item_data.h"
#include "d/d_map_path_dmap.h"
#include "d/d_stage.h"
#include "f_op/f_op_overlap_mng.h"
#include "f_op/f_op_scene_mng.h"

#include <cstring>

namespace rush::run {

void capture_loadout() {
    auto& p = loadouts[active_dungeon];
    constrain_loadout(p, active_dungeon, active_category);
    active_shield = p.shield;
    active_gear = p.gear;
    active_hearts = p.hearts;
    active_master = p.master;
    active_equipped = p.equipped;
}

const char* entry_stage() {
    return boss_run ? kBossTests[active_dungeon].stage : kStarts[active_dungeon].stage;
}

int entry_spawn() {
    return boss_run ? kBossTests[active_dungeon].spawn : kStarts[active_dungeon].spawn;
}

int entry_layer() {
    return boss_run ? kBossTests[active_dungeon].layer : kStarts[active_dungeon].layer;
}

int entry_save() {
    return boss_run ? kBossTests[active_dungeon].save_table : 16 + static_cast<int>(active_dungeon);
}

int entry_room() {
    return boss_run ? kBossTests[active_dungeon].room : kStarts[active_dungeon].room;
}

void prepare_baseline() {
    // Preserve personal controls and names, not any gameplay progress.
    const auto config = g_dComIfG_gameInfo.info.mSavedata.mPlayer.mConfig;
    const auto identity = g_dComIfG_gameInfo.info.mSavedata.mPlayer.mPlayerInfo;
    auto& info = g_dComIfG_gameInfo.info;
    info.mSavedata = baseline;
    for (int i = 16; i <= 24; ++i) {
        info.mSavedata.mSave[i].getBit().init();
        if (i < entry_save()) {
            info.mSavedata.mSave[i].getBit().onStageBossEnemy();
        }
    }
    if (active_dungeon == 1 && !boss_run) {
        // Mines room 1: R01-start event completion and its TagEvC trigger latch.
        info.mSavedata.mSave[17].getBit().onSwitch(0x55);
        info.mSavedata.mSave[17].getBit().onSwitch(0x56);
    }
    if (active_dungeon == 3) {
        info.mSavedata.mSave[19].getBit().onSwitch(0x55);
    }
    if (active_dungeon == 6) {
        // City arrival cutscene already watched; Ooccoo Sr. already joined.
        info.mSavedata.mEvent.onEventBit(dSv_event_flag_c::F_0510);
        info.mSavedata.mEvent.onEventBit(dSv_event_flag_c::F_0756);
    }
    // Horseback Ganondorf waits for Epona; the Forest seed still marks her as missing.
    if (active_dungeon == 8) {
        info.mSavedata.mEvent.onEventBit(dSv_event_flag_c::M_023);
    }
    // Shadow crystal unlocks native transformation; the rod's restored power
    // is required after Temple of Time, which supplies power locally itself.
    if (active_gear & WolfForm) {
        info.mSavedata.mEvent.onEventBit(0xD04);
    } else {
        info.mSavedata.mEvent.offEventBit(0xD04);
    }
    if (active_gear & DominionRod) {
        info.mSavedata.mEvent.onEventBit(dSv_event_flag_c::F_0302);
    }
    if (active_dungeon >= 7) {
        info.mSavedata.mEvent.onEventBit(dSv_event_flag_c::F_0354);
    }
    auto& player = info.mSavedata.mPlayer;
    if (active_dungeon >= 3 || (active_gear & WolfForm)) {
        player.mPlayerStatusB.onTransformLV(3);
        info.mSavedata.mEvent.onEventBit(0x1E08);  // Midna revived (F_0250).
        info.mSavedata.mEvent.onEventBit(dSv_event_flag_c::M_067);
        info.mSavedata.mEvent.onEventBit(dSv_event_flag_c::F_0550);
        info.mSavedata.mEvent.onEventBit(0x501);  // Midna charge attack.
    }
    for (const auto& shield : kShields) {
        player.mCollect.offCollect(COLLECT_SHIELD, shield.collect);
        player.mGetItem.offFirstBit(shield.item);
    }
    const auto& shield = kShields[active_shield];
    player.mCollect.setCollect(COLLECT_SHIELD, shield.collect);
    player.mGetItem.onFirstBit(shield.item);
    player.mPlayerStatusA.setSelectEquip(COLLECT_SHIELD, shield.item);
    for (const auto& item : kInventory) {
        player.mItem.mItems[item.slot] = (active_gear & item.bit) ? item.item : dItemNo_NONE_e;
        if (active_gear & item.bit) {
            player.mGetItem.onFirstBit(item.item);
        } else {
            player.mGetItem.offFirstBit(item.item);
        }
    }
    player.mItem.setLineUpItem();
    player.mItemRecord.setArrowNum((active_gear & Bow) ? 30 : 0);
    player.mItemRecord.setBombNum(0, (active_gear & Bombs) ? 30 : 0);
    player.mItemRecord.setBombNum(1, (active_gear & WaterBombs) ? 15 : 0);
    if (active_gear & ZoraArmor) {
        player.mCollect.setCollect(COLLECT_CLOTHING, 2 /* Zora clothing collection index */);
        player.mGetItem.onFirstBit(dItemNo_WEAR_ZORA_e);
    }
    auto& equipment = player.mPlayerStatusA;
    equipment.setMaxLife(active_hearts * 5);
    equipment.setLife(active_hearts * 4);
    if (active_master) {
        player.mCollect.setCollect(COLLECT_SWORD, COLLECT_MASTER_SWORD);
        player.mGetItem.onFirstBit(dItemNo_MASTER_SWORD_e);
        equipment.setSelectEquip(COLLECT_SWORD, dItemNo_MASTER_SWORD_e);
        equipment.setSelectEquip(B_BUTTON_ITEM, dItemNo_MASTER_SWORD_e);
    }
    for (int i = 0; i < MAX_SELECT_ITEM; ++i) {
        equipment.setSelectItemIndex(i, 0xff);
    }
    if (active_gear & Slingshot) {
        equipment.setSelectItemIndex(0, SLOT_23);
    }
    if (active_gear & Lantern) {
        equipment.setSelectItemIndex(1, SLOT_1);
    }
    info.mSavedata.mPlayer.mConfig = config;
    info.mSavedata.mPlayer.mPlayerInfo.setPlayerName(identity.getPlayerName());
    info.mSavedata.mPlayer.mPlayerReturnPlace.set(entry_stage(), entry_room(), entry_spawn());
    if (boss_run) {
        cDmr_SkipInfo = 0;  // Always play the boss entrance, including after Retry.
        // Boss runs include the dungeon reward needed for the fight.
        const auto reward =
            (kDungeons[active_dungeon].reward == Clawshot && (active_gear & DoubleClawshots))
                ? DoubleClawshots
                : kDungeons[active_dungeon].reward;
        for (const auto& item : kInventory) {
            if (item.bit == reward) {
                player.mPlayerStatusA.setSelectItemIndex(0, item.slot);
            }
        }
        if (active_dungeon == 1) {
            player.mPlayerStatusA.setSelectItemIndex(1, 3);  // Iron Boots
        }
        if (active_dungeon == 2 && (active_gear & ZoraArmor)) {
            player.mPlayerStatusA.setSelectEquip(COLLECT_CLOTHING, dItemNo_WEAR_ZORA_e);
        }
    }
    std::array<int, 2> defaults{equipment.getSelectItemIndex(0), equipment.getSelectItemIndex(1)};
    const auto equipped = resolve_equipment(active_gear, active_equipped, defaults);
    for (int i = 0; i < 2; ++i) {
        equipment.setSelectItemIndex(i, equipped[i]);
    }
    info.mMemory = info.mSavedata.mSave[entry_save()];
    info.mDan.reset();
    info.mTmp.init();
    info.initZone();
    info.mRestart = {};
    info.mRestart.setStartPoint(entry_spawn());
    info.mTurnRestart = {};
    cM_initRnd(0x1357, 0x2468, 0x369c);
    cM_initRnd2(0x1357, 0x2468, 0x369c);
}

HookAction game_start(ModContext*, void*, void*, void*) {
    if (restore_pending && mode_active()) {
        // The native new-file path can still replace the destination here.
        // Leave save state untouched until the gameplay scene consumes it.
        phase = Phase::Loading;
        phase_at = now_ms();
    }
    return HOOK_CONTINUE;
}

HookAction play_entry(ModContext*, void*, void*, void*) {
    if (!restore_pending || !mode_active()) {
        return HOOK_CONTINUE;
    }
    // phase_1 is the first consumer of NextStage, after resetGame and file
    // select have finished. nextStage::set silently ignores an enabled request,
    // so explicitly clear the old request before replacing it.
    dComIfGp_offEnableNextStage();
    dComIfGp_setNextStage(entry_stage(), entry_spawn(), entry_room(), entry_layer(), 0.f, 0, 1, 0,
                          0, 0, 0);
    if (std::string_view(dComIfGp_getNextStageName()) != entry_stage() ||
        dComIfGp_getNextStageRoomNo() != entry_room() ||
        dComIfGp_getNextStagePoint() != entry_spawn()) {
        svc_log->warn(mod_ctx, "Rush: destination verification failed; baseline not applied");
        cancel();
        return HOOK_CONTINUE;
    }
    prepare_baseline();
    phase = Phase::Loading;
    phase_at = now_ms();
    svc_log->info(mod_ctx,
                  (std::string("Rush: verified gameplay entry ") + dComIfGp_getNextStageName() +
                   " room " + std::to_string(dComIfGp_getNextStageRoomNo()))
                      .c_str());
    return HOOK_CONTINUE;
}

HookAction stage_init(ModContext*, void*, void*, void*) {
    if (savewarp_pending && savewarp_loaded) {
        savewarp_scene_ready = true;
    }
    if (restore_pending && mode_active() &&
        std::string_view(dComIfGp_getStartStageName()) == entry_stage()) {
        // Old scene deletion has already written its flags. Restore here, before
        // getSave/initDan and before the new scene creates any actors.
        prepare_baseline();
        restore_pending = false;
        world_held = true;
        session_ready = true;
        phase = Phase::Loading;
        phase_at = now_ms();
        svc_log->info(mod_ctx, "Rush: dungeon stage initialized; waiting for countdown");
    }
    return HOOK_CONTINUE;
}

}  // namespace rush::run
