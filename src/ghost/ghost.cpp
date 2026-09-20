// Run recording and timestamp playback. The puppet owns all rendering resources.
#include "ghost/ghost.hpp"
#include "rules/dungeon_starts.hpp"
#include "ghost/ghost_validation.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_resorce.h"
#include "f_op/f_op_actor_mng.h"
#include "f_op/f_op_overlap_mng.h"
#include "f_pc/f_pc_manager.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DMaterial.h"
#include "m_Do/m_Do_mtx.h"

#include <mods/svc/host.h>
#include <mods/svc/log.h>
#include <mods/svc/hook.hpp>
#include "f_ap/f_ap_game.h"
#include <chrono>
#include <algorithm>
#include <cmath>
DEFINE_HOOK(&fapGm_Execute, RushGhostTick);

namespace rush::ghost {
void capture_visuals(daAlink_c&, Recording&, const FrameInfo&);

// ------------------------------------------------------------------
// Recording helpers
// ------------------------------------------------------------------

void Recording::clear() {
    visuals = VisualTrack{};
    frames.clear();
    mtxPool.clear();
    scaleFlagPool.clear();
    stages.clear();
    clothesMask = 0;
    swordMask = 0;
    shieldMask = 0;
    heldMask = 0;
    anyWolf = false;
    anyHuman = false;
}

void Recording::computeUsage() {
    clothesMask = 0;
    swordMask = 0;
    shieldMask = 0;
    heldMask = 0;
    anyWolf = false;
    anyHuman = false;

    for (const FrameInfo& f : frames) {
        if (f.form == kFormWolf) {
            anyWolf = true;
        } else {
            anyHuman = true;
            if (f.clothes < kNumClothes) {
                clothesMask |= 1 << f.clothes;
            }
        }
        if (f.sword < kNumSwords) {
            swordMask |= 1 << f.sword;
        }
        if (f.shield < kNumShields) {
            shieldMask |= 1 << f.shield;
        }
        if (f.held < kNumHeld) {
            heldMask |= 1 << f.held;
        }
    }
    swordMask &= ~u8(1 << SwordNone);
    shieldMask &= ~u8(1 << ShieldNone);
    heldMask &= ~u8(1 << HeldNone);
}

// ------------------------------------------------------------------
// Manager state
// ------------------------------------------------------------------

namespace {

constexpr u32 kFileMagic = 0x54504748;  // 'TPGH'
constexpr u32 kFileVersion = 2;
constexpr size_t kMaxFileBytes = 256u * 1024 * 1024;
constexpr size_t kMaxMatrixBytes = 192u * 1024 * 1024;
int64_t s_epoch = 0;
bool s_truncated = false;
uint64_t s_renderTick = 0;
bool s_testPending = false;
bool s_testLoading = false;
bool s_testMode = false;
int64_t clock_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
// Hard cap so a forgotten recording can't eat memory forever.
// 30 minutes at 30 ticks/sec.
constexpr u32 kMaxRecordFrames = 30 * 60 * 30;

bool s_recording = false;
bool s_playing = false;
bool s_playFinished = false;
u32 s_playPos = 0;

Recording s_take;   // actively recording / last finished take
Recording s_ghost;  // loaded ghost being (or ready to be) played
bool s_takeFinished = false;
bool s_hasGhost = false;
uint32_t s_color = 0x65d7ca;

// Puppet lifecycle
fpc_ProcID s_puppetId = fpcM_ERROR_PROCESS_ID_e;
bool s_puppetSeen = false;
int s_puppetWaitTicks = 0;
int s_spawnCooldown = 0;

bool s_removingPuppet = false;

void copy_mtx(GhostMtx& dst, const Mtx src) {
    memcpy(dst.m, src, sizeof(Mtx));
}

u8 capture_stage_index(Recording& rec, const char* stageName) {
    for (size_t i = 0; i < rec.stages.size(); i++) {
        if (strncmp(rec.stages[i].name, stageName, sizeof(StageName::name) - 1) == 0) {
            return (u8)i;
        }
    }
    if (rec.stages.size() >= 0xFF) {
        return 0;
    }
    StageName s = {};
    strncpy(s.name, stageName, sizeof(s.name) - 1);
    rec.stages.push_back(s);
    return (u8)(rec.stages.size() - 1);
}

u8 classify_clothes(daAlink_c* link) {
    const char* arc = link->mArcName;
    if (arc != nullptr) {
        if (strcmp(arc, "Bmdl") == 0) {
            return ClothesOrdon;
        }
        if (strcmp(arc, "Zmdl") == 0) {
            return ClothesZora;
        }
        if (strcmp(arc, "Mmdl") == 0) {
            return ClothesMagic;
        }
    }
    return ClothesHero;
}

u8 classify_sword(daAlink_c* link) {
    J3DModel* sword = link->mSwordModel;
    if (sword == nullptr) {
        return SwordNone;
    }
    if (sword == link->mWoodSwordModel) {
        return SwordWood;
    }
    if (sword == link->mpSwMModel) {
        return SwordMaster;
    }
    return SwordOrdon;
}

u8 classify_shield(daAlink_c* link) {
    if (link->mShieldModel == nullptr || link->mShieldArcName == nullptr) {
        return ShieldNone;
    }
    if (strcmp(link->mShieldArcName, "CWShd") == 0) {
        return ShieldWoodCarved;
    }
    if (strcmp(link->mShieldArcName, "SWShd") == 0) {
        return ShieldWoodShop;
    }
    if (strcmp(link->mShieldArcName, "HyShd") == 0) {
        return ShieldHylian;
    }
    return ShieldNone;
}

u8 classify_held(daAlink_c* link) {
    if (link->mHeldItemModel == nullptr) {
        return HeldNone;
    }
    switch (link->mEquipItem) {
        case dItemNo_BOOMERANG_e:
            return HeldBoomerang;
        case dItemNo_BOW_e:
            return HeldBow;
        case dItemNo_PACHINKO_e:
            return HeldSlingshot;
        case dItemNo_HOOKSHOT_e:
        case dItemNo_W_HOOKSHOT_e:
            return HeldClawshot;
        case dItemNo_IRONBALL_e:
            return HeldBallChain;
        case dItemNo_COPY_ROD_e:
        case dItemNo_COPY_ROD_2_e:
            return HeldDominionRod;
        default:
            return HeldNone;
    }
}

// Find the material index whose shape is currently shown for a hand.
u8 find_hand_shape_index(J3DModel* handModel, void* shownShape, u8 fallback) {
    if (handModel == nullptr || shownShape == nullptr) {
        return fallback;
    }
    J3DModelData* data = handModel->getModelData();
    for (u16 i = 0; i < data->getMaterialNum(); i++) {
        if ((void*)data->getMaterialNodePointer(i)->getShape() == shownShape) {
            return (u8)i;
        }
    }
    return fallback;
}

void push_equip(Recording& rec, FrameInfo& f, EquipSlot slot, J3DModel* model, bool visible) {
    GhostMtx gm = {};
    if (model != nullptr && visible) {
        f.visMask |= u16(1) << slot;
        copy_mtx(gm, model->getBaseTRMtx());
    }
    rec.mtxPool.push_back(gm);
}

void capture_frame(Recording& rec, bool dungeonOnly = true) {
    daAlink_c* link = (daAlink_c*)dComIfGp_getPlayer(0);
    const std::string_view stage = dComIfGp_getStartStageName();
    if (dungeonOnly && !std::any_of(kStarts.begin(), kStarts.end(), [&](const auto& d) {
            return dungeon_stage(static_cast<size_t>(&d - kStarts.data()), stage);
        })) {
        return;
    }
    if (link == nullptr || fopAcM_GetRoomNo(link) < 0 || dComIfGp_isEnableNextStage() ||
        fopOvlpM_IsPeek()) {
        return;
    }
    J3DModel* body = link->mpLinkModel;
    if (body == nullptr || body->getModelData() == nullptr) {
        return;
    }
    // Skip transition frames where models are being rebuilt.
    if (link->mClothesChangeWaitTimer != 0) {
        return;
    }

    u32 jointCount = body->getModelData()->getJointNum();
    if (jointCount == 0 || jointCount > kMaxJointCount) {
        return;
    }

    const bool wolf = link->checkWolf();

    FrameInfo f = {};
    f.milliseconds = static_cast<uint64_t>(std::max<int64_t>(0, clock_ms() - s_epoch));
    f.pos = link->current.pos;
    f.angle = link->shape_angle;
    f.stageIdx = capture_stage_index(rec, dComIfGp_getStartStageName());
    f.roomNo = (s8)fopAcM_GetRoomNo(link);
    f.form = wolf ? kFormWolf : kFormHuman;
    f.clothes = classify_clothes(link);
    f.sword = classify_sword(link);
    f.shield = classify_shield(link);
    f.held = classify_held(link);
    f.jointCount = (u8)jointCount;
    f.leftHandShape = 0xFF;
    f.rightHandShape = 0xFF;
    f.mtxOffset = (u32)rec.mtxPool.size();
    f.flagOffset = (u32)rec.scaleFlagPool.size();

    if (!wolf) {
        f.leftHandShape =
            find_hand_shape_index(link->mpLinkHandModel, link->field_0x06d0, link->mLeftHandIndex);
        f.rightHandShape =
            find_hand_shape_index(link->mpLinkHandModel, link->field_0x06d4, link->mRightHandIndex);
    }

    // [0] body base matrix
    GhostMtx baseM;
    copy_mtx(baseM, body->getBaseTRMtx());
    rec.mtxPool.push_back(baseM);

    // [1..jointCount] world-space joint matrices + scale flags
    for (u32 j = 0; j < jointCount; j++) {
        GhostMtx gm;
        copy_mtx(gm, body->getAnmMtx(j));
        rec.mtxPool.push_back(gm);
        rec.scaleFlagPool.push_back(body->getMtxBuffer()->getScaleFlag(j));
    }

    // Equipment world base matrices (kNumEquipSlots entries, identity when hidden)
    const bool swordDraw = link->checkSwordDraw();
    const bool shieldDraw = link->checkShieldDraw();
    const bool itemDraw = wolf ? (link->mHeldItemModel != nullptr) : (bool)link->checkItemDraw();
    const bool kanteraDraw = !wolf && (link->checkNoResetFlg2(daPy_py_c::FLG2_UNK_1) ||
                                       link->checkNoResetFlg2(daPy_py_c::FLG2_UNK_20000));
    const bool chainsDraw = wolf && dComIfGs_isEventBit(dSv_event_flag_c::M_011);

    push_equip(rec, f, SlotSword, link->mSwordModel, swordDraw && f.sword != SwordNone);
    push_equip(rec, f, SlotSheath, link->mSheathModel,
               swordDraw && (wolf || !link->checkWoodSwordEquip()));
    push_equip(rec, f, SlotShield, link->mShieldModel, shieldDraw && f.shield != ShieldNone);
    push_equip(rec, f, SlotHeld, link->mHeldItemModel, itemDraw && f.held != HeldNone);
    push_equip(rec, f, SlotHookTip, link->mpHookTipModel,
               itemDraw && f.held == HeldClawshot && link->mpHookTipModel != nullptr);
    push_equip(rec, f, SlotKantera, link->mpKanteraModel, kanteraDraw);
    for (int i = 0; i < 4; i++) {
        push_equip(rec, f, (EquipSlot)(SlotChain0 + i), wolf ? link->mpWlChainModels[i] : nullptr,
                   chainsDraw);
    }

    capture_visuals(*link, rec, f);
    rec.frames.push_back(f);
}

// ------------------------------------------------------------------
bool puppet_alive() {
    if (s_puppetId == (fpc_ProcID)fpcM_ERROR_PROCESS_ID_e) {
        return false;
    }
    fopAc_ac_c* actor = nullptr;
    fopAcM_SearchByID(s_puppetId, &actor);
    return (actor && fopAcM_GetName(actor) == puppet_profile) || fpcM_IsCreating(s_puppetId);
}

void delete_puppet() {
    if (s_puppetId != (fpc_ProcID)fpcM_ERROR_PROCESS_ID_e) {
        fopAc_ac_c* actor = nullptr;
        fopAcM_SearchByID(s_puppetId, &actor);
        if (actor && fopAcM_GetName(actor) == puppet_profile) {
            fopAcM_delete(actor);
        }
    }
}

const FrameInfo* current_play_frame() {
    if (!s_playing || s_playFinished || s_playPos >= s_ghost.frames.size() ||
        (!s_testMode &&
         clock_ms() - s_epoch < static_cast<int64_t>(s_ghost.frames.front().milliseconds))) {
        return nullptr;
    }
    return &s_ghost.frames[s_playPos];
}

// Should the puppet exist & be fed right now?
bool frame_visible_here(const FrameInfo* f) {
    if (f == nullptr) {
        return false;
    }
    if (f->stageIdx >= s_ghost.stages.size()) {
        return false;
    }
    auto* link = dComIfGp_getPlayer(0);
    if (!link || fopAcM_GetRoomNo(link) != f->roomNo || dComIfGp_isEnableNextStage() ||
        fopOvlpM_IsPeek()) {
        return false;
    }
    if (!s_testMode && clock_ms() - s_epoch > static_cast<int64_t>(f->milliseconds) + 250) {
        return false;
    }
    const char* curStage = dComIfGp_getStartStageName();
    return strncmp(s_ghost.stages[f->stageIdx].name, curStage, sizeof(StageName::name) - 1) == 0;
}

void update_puppet_lifecycle() {
    if (!s_playing) {
        return;
    }

    const FrameInfo* f = current_play_frame();
    const bool wantPuppet =
        frame_visible_here(f) && dComIfGp_getPlayer(0) != nullptr && !fopOvlpM_IsDoingReq();

    if (s_puppetId != (fpc_ProcID)fpcM_ERROR_PROCESS_ID_e) {
        fopAc_ac_c* actor = nullptr;
        fopAcM_SearchByID(s_puppetId, &actor);
        if (actor && fopAcM_GetName(actor) == puppet_profile) {
            s_puppetSeen = true;
            return;  // alive and well
        }
        if (!s_puppetSeen) {
            // Still constructing (or create failed silently). Give it time.
            if (++s_puppetWaitTicks < 300) {
                return;
            }
            svc_log->warn(mod_ctx, "Ghost puppet did not appear; retrying");
        }
        // Deleted (stage change) or timed out: allow respawn.
        s_puppetId = fpcM_ERROR_PROCESS_ID_e;
        s_puppetSeen = false;
        s_puppetWaitTicks = 0;
        s_spawnCooldown = 30;
        return;
    }

    if (!wantPuppet) {
        return;
    }
    if (s_spawnCooldown > 0) {
        s_spawnCooldown--;
        return;
    }

    cXyz pos = f->pos;
    csXyz ang = f->angle;
    // This hook runs outside actor execution. Explicitly attach the puppet to
    // Link's gameplay layer instead of inheriting the manager's current layer.
    auto* savedLayer = fpcLy_CurrentLayer();
    auto* link = dComIfGp_getPlayer(0);
    fpcLy_SetCurrentLayer(link->layer_tag.layer);
    fpc_ProcID id = fopAcM_create(puppet_profile, 0, &pos, f->roomNo, &ang, NULL, -1);
    fpcLy_SetCurrentLayer(savedLayer);
    if (id != (fpc_ProcID)fpcM_ERROR_PROCESS_ID_e) {
        s_puppetId = id;
        s_puppetSeen = false;
        s_puppetWaitTicks = 0;
        svc_log->info(mod_ctx, "Ghost: puppet creation queued in gameplay layer");
    } else {
        s_spawnCooldown = 60;
    }
}

void format_time(char* buf, size_t bufSize, u32 frames) {
    const u32 totalCs = (frames * 100) / 30;
    snprintf(buf, bufSize, "%02u:%02u.%02u", totalCs / 6000, (totalCs / 100) % 60, totalCs % 100);
}

}  // namespace

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

void start_recording(int64_t epoch) {
    s_epoch = epoch;
    s_truncated = false;
    s_take.clear();
    s_takeFinished = false;
    s_recording = true;
}

void stop_recording() {
    if (!s_recording) {
        return;
    }
    s_recording = false;
    s_takeFinished = !s_take.frames.empty();
    if (s_takeFinished) {
        s_take.computeUsage();
        char t[16];
        format_time(t, sizeof(t), (u32)s_take.frames.size());
    }
}

bool start_playback(int64_t epoch, std::string* o_err) {
    if (resources_busy()) {
        if (o_err) {
            *o_err = "Previous puppet is still being removed";
        }
        return false;
    }
    s_epoch = epoch;
    if (!s_hasGhost || s_ghost.empty()) {
        if (o_err) {
            *o_err = "No ghost loaded";
        }
        return false;
    }
    s_playing = true;
    s_playFinished = false;
    s_playPos = 0;
    s_removingPuppet = false;
    svc_log->info(mod_ctx, "Ghost: playback clock started");
    return true;
}

void stop_playback() {
    s_testPending = false;
    s_testLoading = false;
    s_testMode = false;
    if (!s_playing && !puppet_alive()) {
        return;
    }
    s_playing = false;
    s_playFinished = false;
    s_playPos = 0;
    delete_puppet();
    // Wait for actor deletion before reusing the playback slot.
    s_removingPuppet = true;
}

void stop_all() {
    stop_recording();
    stop_playback();
}

bool is_recording() {
    return s_recording;
}
uint32_t ghost_color() {
    return s_color;
}
bool is_playing() {
    return s_playing;
}
u32 record_frame_count() {
    return (u32)s_take.frames.size();
}
u32 playback_position() {
    return s_playPos;
}
u32 playback_length() {
    return (u32)s_ghost.frames.size();
}
bool playback_finished() {
    return s_playFinished;
}
bool has_take() {
    return s_takeFinished && !s_take.empty();
}
u32 take_frame_count() {
    return (u32)s_take.frames.size();
}
bool has_ghost() {
    return s_hasGhost && !s_ghost.empty();
}
u32 ghost_frame_count() {
    return (u32)s_ghost.frames.size();
}

const char* ghost_start_stage() {
    if (!s_hasGhost || s_ghost.stages.empty()) {
        return "";
    }
    return s_ghost.stages[0].name;
}

// ------------------------------------------------------------------
// Persistence
// ------------------------------------------------------------------

std::filesystem::path ghosts_dir() {
    const char* root = nullptr;
    if (svc_host->data_dir(mod_ctx, &root) != MOD_OK || !root) {
        throw std::runtime_error("Ghost storage unavailable");
    }
    std::filesystem::path dir = std::filesystem::u8path(root) / "ghosts";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

bool save_take(const std::string& fileName, std::string* o_err) {
    if (!has_take()) {
        if (o_err) {
            *o_err = "No finished recording to save";
        }
        return false;
    }

    std::filesystem::path path = ghosts_dir() / fileName;
    if (path.extension() != ".tpg") {
        path += ".tpg";
    }

    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (o_err) {
            *o_err = "Could not open file for writing";
        }
        return false;
    }

    FileHeader hdr = {};
    hdr.magic = kFileMagic;
    hdr.version = kFileVersion;
    hdr.frameCount = (u32)s_take.frames.size();
    hdr.mtxCount = (u32)s_take.mtxPool.size();
    hdr.flagCount = (u32)s_take.scaleFlagPool.size();
    hdr.stageCount = (u32)s_take.stages.size();
    hdr.reserved[0] = sizeof(FrameInfo);
    hdr.reserved[1] = sizeof(GhostMtx);

    out.write((const char*)&hdr, sizeof(hdr));
    out.write((const char*)s_take.stages.data(), s_take.stages.size() * sizeof(StageName));
    out.write((const char*)s_take.frames.data(), s_take.frames.size() * sizeof(FrameInfo));
    out.write((const char*)s_take.mtxPool.data(), s_take.mtxPool.size() * sizeof(GhostMtx));
    out.write((const char*)s_take.scaleFlagPool.data(), s_take.scaleFlagPool.size());

    out.close();
    if (!out.good()) {
        if (o_err) {
            *o_err = "Write failed";
        }
        return false;
    }

    auto visualPath = path;
    visualPath.replace_extension(".tpa");
    auto visualTemp = visualPath;
    visualTemp += ".tmp";
    std::ofstream animation(visualTemp, std::ios::binary | std::ios::trunc);
    if (!write_visuals(animation, s_take.visuals)) {
        if (o_err) {
            *o_err = "Could not write ghost animation track";
        }
        return false;
    }
    animation.close();
    if (!animation.good()) {
        if (o_err) {
            *o_err = "Animation write failed";
        }
        return false;
    }
    std::filesystem::rename(visualTemp, visualPath);
    std::filesystem::rename(temporary, path);
    return true;
}

bool load_ghost_file(const std::filesystem::path& path, std::size_t expected_dungeon,
                     std::string* o_err, bool require_visuals) {
    if (resources_busy()) {
        if (o_err) {
            *o_err = "Previous puppet is still being removed";
        }
        return false;
    }
    if (std::filesystem::file_size(path) > kMaxFileBytes) {
        if (o_err) {
            *o_err = "Ghost file too large";
        }
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (o_err) {
            *o_err = "Could not open file";
        }
        return false;
    }

    FileHeader hdr = {};
    in.read((char*)&hdr, sizeof(hdr));
    if (!in.good() || hdr.magic != kFileMagic || hdr.version != kFileVersion) {
        if (o_err) {
            *o_err = "Not a valid ghost file";
        }
        return false;
    }
    if (!valid_header(hdr, std::filesystem::file_size(path), sizeof(FrameInfo), sizeof(GhostMtx))) {
        if (o_err) {
            *o_err = "Ghost header or size mismatch";
        }
        return false;
    }

    Recording rec;
    rec.stages.resize(hdr.stageCount);
    rec.frames.resize(hdr.frameCount);
    rec.mtxPool.resize(hdr.mtxCount);
    rec.scaleFlagPool.resize(hdr.flagCount);

    in.read((char*)rec.stages.data(), rec.stages.size() * sizeof(StageName));
    in.read((char*)rec.frames.data(), rec.frames.size() * sizeof(FrameInfo));
    in.read((char*)rec.mtxPool.data(), rec.mtxPool.size() * sizeof(GhostMtx));
    in.read((char*)rec.scaleFlagPool.data(), rec.scaleFlagPool.size());
    if (!in.good()) {
        if (o_err) {
            *o_err = "Ghost file truncated";
        }
        return false;
    }

    if (!valid_recording(rec, expected_dungeon)) {
        if (o_err) {
            *o_err = "Invalid ghost frames";
        }
        return false;
    }
    auto visualPath = path;
    visualPath.replace_extension(".tpa");
    if (std::filesystem::exists(visualPath)) {
        std::ifstream animation(visualPath, std::ios::binary);
        if (!read_visuals(animation, std::filesystem::file_size(visualPath), rec.frames.size(),
                          rec.visuals)) {
            if (require_visuals) {
                if (o_err) {
                    *o_err = "Invalid animation track";
                }
                return false;
            }
            svc_log->warn(mod_ctx, "Invalid auxiliary animation track; using base ghost poses");
        }
    }
    if (require_visuals && rec.visuals.frames.size() != rec.frames.size()) {
        if (o_err) {
            *o_err = "Missing animation track";
        }
        return false;
    }
    rec.computeUsage();
    s_ghost = std::move(rec);
    s_hasGhost = true;
    // Deterministic across reloads, independent of std::hash implementations.
    uint32_t colorHash = 2166136261u;
    for (unsigned char c : (path.parent_path().filename() / path.stem()).string()) {
        colorHash = (colorHash ^ c) * 16777619u;
    }
    const unsigned sector = colorHash % 1536;
    const unsigned rise = sector % 256, fall = 255 - rise;
    const unsigned colors[][3] = {{255, rise, 0}, {fall, 255, 0}, {0, 255, rise},
                                  {0, fall, 255}, {rise, 0, 255}, {255, 0, fall}};
    const auto& rgb = colors[sector / 256];
    s_color = ((64 + rgb[0] * 191 / 255) << 16) | ((64 + rgb[1] * 191 / 255) << 8) |
              (64 + rgb[2] * 191 / 255);

    char t[16];
    format_time(t, sizeof(t), (u32)s_ghost.frames.size());
    return true;
}

// ------------------------------------------------------------------
// Per-tick update
// ------------------------------------------------------------------

namespace {
bool scene_suspended = false;
}
void suspend_scene(bool suspended) {
    if (suspended && !scene_suspended) {
        delete_puppet();
        // A native reset can recycle process IDs. Never carry an actor ID from
        // the old scene into the new process list; retain only replay data.
        s_puppetId = fpcM_ERROR_PROCESS_ID_e;
        s_puppetSeen = false;
        s_puppetWaitTicks = 0;
        s_spawnCooldown = 0;
    }
    scene_suspended = suspended;
}
void on_sim_tick() {
    ++s_renderTick;
    // The actor drains its resource jobs and frees its own rendering heap.
    if (s_removingPuppet && !s_playing) {
        delete_puppet();
        if (!puppet_alive()) {
            s_puppetId = fpcM_ERROR_PROCESS_ID_e;
            s_puppetSeen = false;
            s_puppetWaitTicks = 0;
            s_removingPuppet = false;
        }
    }

    if (s_testPending && !resources_busy()) {
        s_testPending = false;
        if (can_spawn_test()) {
            Recording snapshot;
            capture_frame(snapshot, false);
            if (!snapshot.frames.empty()) {
                auto& pose = snapshot.frames.front();
                // Leave the test at Link's known playable position. A sideways
                // offset can put it behind a wall in narrow dungeon corridors.
                pose.milliseconds = 0;
                snapshot.computeUsage();
                s_ghost = std::move(snapshot);
                s_hasGhost = true;
                s_color = 0x65d7ca;
                s_testLoading = true;
                svc_log->info(mod_ctx, "Ghost test: captured current Link; loading private models");
            } else {
                svc_log->warn(mod_ctx, "Ghost test: current Link pose unavailable");
            }
        }
    }
    if (s_testLoading && prepare_loaded()) {
        s_testLoading = false;
        s_testMode = has_ghost();
        s_playing = s_testMode;
        s_playPos = 0;
        s_playFinished = false;
        if (s_testMode) {
            svc_log->info(mod_ctx,
                          "Ghost test: stationary model ready at Link's captured position");
        }
    }

    if (s_recording && !scene_suspended) {
        try {
            capture_frame(s_take);
        } catch (const std::exception& e) {
            s_truncated = true;
            stop_recording();
            svc_log->warn(mod_ctx, e.what());
        }
        if (s_take.frames.size() >= kMaxRecordFrames ||
            s_take.mtxPool.size() * sizeof(GhostMtx) >= kMaxMatrixBytes) {
            s_truncated = true;
            svc_log->warn(mod_ctx, "Ghost recording limit reached; route and run timing continue");
            stop_recording();
        }
    }

    if (s_playing && !scene_suspended) {
        if (s_testMode) {
            if (!frame_visible_here(&s_ghost.frames.front())) {
                stop_playback();
                return;
            }
            s_playPos = 0;
            s_playFinished = false;
        } else {
            const auto elapsed = static_cast<uint64_t>(std::max<int64_t>(0, clock_ms() - s_epoch));
            const auto index = frame_at(s_ghost.frames, elapsed);
            s_playPos = index ? static_cast<u32>(*index) : static_cast<u32>(s_ghost.frames.size());
            s_playFinished = elapsed > s_ghost.frames.back().milliseconds;
        }
        update_puppet_lifecycle();
    }
}

// ------------------------------------------------------------------
// Puppet interface
// ------------------------------------------------------------------

PuppetFrameView puppet_frame() {
    PuppetFrameView v;
    if (scene_suspended) {
        return v;
    }
    const FrameInfo* f = current_play_frame();
    if (f == nullptr || !frame_visible_here(f)) {
        return v;
    }
    if (s_ghost.visuals.frames.size() == s_ghost.frames.size()) {
        v.visuals = &s_ghost.visuals;
        v.visualFrame = &s_ghost.visuals.frames[s_playPos];
    }
    v.info = f;
    v.baseMtx = &s_ghost.mtxPool[f->mtxOffset];
    v.bodyMtx = &s_ghost.mtxPool[f->mtxOffset + 1];
    v.equipMtx = &s_ghost.mtxPool[f->mtxOffset + 1 + f->jointCount];
    v.scaleFlags = &s_ghost.scaleFlagPool[f->flagOffset];
    return v;
}

const Recording* playback_recording() {
    return s_playing ? &s_ghost : nullptr;
}

int16_t puppet_profile = -1;
uint64_t render_tick() {
    return s_renderTick;
}
bool can_spawn_test() {
    auto* link = dComIfGp_getPlayer(0);
    return link && dComIfGp_getCamera(0) && fopAcM_GetRoomNo(link) >= 0 &&
           !dComIfGp_isEnableNextStage() && !fopOvlpM_IsPeek();
}
void spawn_test() {
    if (!can_spawn_test()) {
        return;
    }
    stop_playback();
    s_testPending = true;
}
void remove_test() {
    if (s_testPending || s_testLoading || s_testMode) {
        stop_playback();
    }
}
bool resources_busy() {
    return s_playing || puppet_alive();
}
bool recording_truncated() {
    return s_truncated;
}
void clear_loaded() {
    if (!resources_busy()) {
        s_ghost = Recording{};
        s_hasGhost = false;
    }
}
bool prepare_loaded() {
    return true;
}
ModResult shutdown() {
    stop_all();
    const auto result = unregister_puppet();
    if (result != MOD_OK) {
        return result;
    }
    s_puppetId = fpcM_ERROR_PROCESS_ID_e;
    s_take = Recording{};
    s_ghost = Recording{};
    return MOD_OK;
}
ModResult initialize() {
    auto result = register_puppet();
    if (result != MOD_OK) {
        return result;
    }
    return mods::hook::add_post<RushGhostTick>(
        [](ModContext*, void*, void*, void*) { on_sim_tick(); });
}
}  // namespace rush::ghost
