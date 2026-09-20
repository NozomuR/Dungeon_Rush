#include "ghost/ghost.hpp"
#include "ghost/ghost_material.hpp"
#include "ghost/ghost_wire.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <mods/svc/actor.h>
#include <mods/svc/log.h>
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_resorce.h"
#include "f_op/f_op_actor_mng.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DShape.h"
#include "JSystem/J3DGraphBase/J3DTexture.h"
#include "JSystem/JKernel/JKRMemArchive.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_dvd_thread.h"
#include "res/Object/AlAnm.h"
#include "res/Object/Alink.h"
#include "res/Object/Kmdl.h"
namespace Bmdl {
#include "res/Object/Bmdl.h"
}
namespace Zmdl {
#include "res/Object/Zmdl.h"
}
namespace Mmdl {
#include "res/Object/Mmdl.h"
}
#include "res/Object/Wmdl.h"

extern const ActorService* svc_actor;
namespace rush::ghost {
namespace {
void report(const std::string& text) {
    svc_log->info(mod_ctx, text.c_str());
}

// Resource identifiers come from the game's generated asset headers. Selection
// and lifetime are owned here, independently of the playback manager.
struct Asset {
    const char* archive;
    uint16_t index;
};
struct CostumeAssets {
    const char* archive;
    uint16_t body, hat, face, hands, lantern, wood, boots;
};
constexpr CostumeAssets costumes[] = {
    {"Kmdl", dRes_INDEX_KMDL_BMD_AL_e, dRes_INDEX_KMDL_BMD_AL_HEAD_e, dRes_INDEX_KMDL_BMD_AL_FACE_e,
     dRes_INDEX_KMDL_BMD_AL_HANDS_e, dRes_INDEX_KMDL_BMD_AL_KANTERA_e, dRes_INDEX_KMDL_BMD_AL_SWB_e,
     dRes_INDEX_KMDL_BMD_AL_BOOTSH_e},
    {"Bmdl", Bmdl::dRes_INDEX_BMDL_BMD_BL_e, Bmdl::dRes_INDEX_BMDL_BMD_BL_HEAD_e,
     Bmdl::dRes_INDEX_BMDL_BMD_AL_FACE_e, Bmdl::dRes_INDEX_BMDL_BMD_BL_HANDS_e,
     Bmdl::dRes_INDEX_BMDL_BMD_AL_KANTERA_e, Bmdl::dRes_INDEX_BMDL_BMD_AL_SWB_e,
     Bmdl::dRes_INDEX_BMDL_BMD_AL_BOOTSH_e},
    {"Zmdl", Zmdl::dRes_INDEX_ZMDL_BMD_ZL_e, Zmdl::dRes_INDEX_ZMDL_BMD_ZL_HEAD_e,
     Zmdl::dRes_INDEX_ZMDL_BMD_ZL_FACE_e, Zmdl::dRes_INDEX_ZMDL_BMD_AL_HANDS_e,
     Zmdl::dRes_INDEX_ZMDL_BMD_AL_KANTERA_e, Zmdl::dRes_INDEX_ZMDL_BMD_AL_SWB_e,
     Zmdl::dRes_INDEX_ZMDL_BMD_AL_BOOTSH_e},
    {"Mmdl", Mmdl::dRes_INDEX_MMDL_BMD_ML_e, Mmdl::dRes_INDEX_MMDL_BMD_ML_HEAD_e,
     Mmdl::dRes_INDEX_MMDL_BMD_AL_FACE_e, Mmdl::dRes_INDEX_MMDL_BMD_AL_HANDS_e,
     Mmdl::dRes_INDEX_MMDL_BMD_AL_KANTERA_e, Mmdl::dRes_INDEX_MMDL_BMD_AL_SWB_e,
     Mmdl::dRes_INDEX_MMDL_BMD_AL_BOOTSH_e},
};

class Archives {
    struct Mount {
        const char* name{};
        mDoDvdThd_mountArchive_c* job{};
        JKRArchive* archive{};
    };
    std::array<Mount, 12> mounts{};
    bool error = false;

public:
    bool request(const char* name) {
        if (std::strcmp(name, "AlAnm") == 0) {
            return true;  // Read-only source, copied before use.
        }
        for (const auto& m : mounts) {
            if (m.name && std::strcmp(m.name, name) == 0) {
                return true;
            }
        }
        for (auto& m : mounts) {
            if (!m.name) {
                m.name = name;
                const auto path = std::format("/res/Object/{}.arc", name);
                m.job = mDoDvdThd_mountArchive_c::create(path.c_str(), mDoDvd_MOUNT_DIRECTION_HEAD,
                                                         nullptr);
                if (!m.job) {
                    error = true;
                }
                return m.job != nullptr;
            }
        }
        error = true;
        return false;
    }
    // Finish all outstanding jobs even after a failure. Never free an active
    // DVD command while its worker can still access it.
    int poll() {
        bool pending = false;
        for (auto& m : mounts) {
            if (m.job) {
                if (!m.job->sync()) {
                    pending = true;
                    continue;
                }
                m.archive = m.job->getArchive();
                m.job->destroy();
                m.job = nullptr;
                if (!m.archive) {
                    error = true;
                }
            }
        }
        return pending ? 0 : error ? -1 : 1;
    }
    J3DModelData* instantiate(Asset asset) {
        JKRArchive* archive = nullptr;
        if (std::strcmp(asset.archive, "AlAnm") == 0) {
            archive = dComIfGp_getAnmArchive();
        } else {
            for (const auto& m : mounts) {
                if (m.name && std::strcmp(m.name, asset.archive) == 0) {
                    archive = m.archive;
                }
            }
        }
        if (!archive) {
            return nullptr;
        }
        auto* raw = archive->getIdxResource(asset.index);
        if (!raw) {
            return nullptr;
        }
        const auto size = archive->getExpandedResSize(raw);
        if (size < 32 || size > 16 * 1024 * 1024) {
            return nullptr;
        }
        auto* copy = JKR_NEW_ARRAY_ARGS(u8, size, 32);
        if (!copy) {
            return nullptr;
        }
        std::memcpy(copy, raw, size);
        u32 type = 0;
        for (int i = 0; i < archive->countDirectory(); ++i) {
            const auto& dir = archive->mNodes[i];
            if (asset.index >= dir.first_file_index &&
                asset.index < dir.first_file_index + dir.num_entries) {
                type = dir.type;
            }
        }
        if (type != 'BMDR' && type != 'BMWR' && type != 'BMDE' && type != 'BMWE' &&
            type != 'BMDV') {
            return nullptr;
        }
        // Every piece gets separate model data, including repeated chain/boot
        // resources. Joint callbacks and material visibility cannot collide.
        auto* data = dRes_info_c::loaderBasicBmd(type, copy);
        if (data && (type == 'BMWR' || type == 'BMWE')) {
            // loaderBasicBmd appends and enables Link's teleport mask. Native
            // Link disables it after model initialization; a replay must do
            // the same or the mask erases pixels as the camera moves.
            dRes_info_c::offWarpMaterial(data);
            data->simpleCalcMaterial(const_cast<MtxP>(j3dDefaultMtx));
            data->makeSharedDL();
        }
        return data;
    }
    ~Archives() {
        for (auto& m : mounts) {
            if (m.archive) {
                m.archive->unmount();
            }
        }
    }
};

struct Piece {
    Asset asset{};
    Visual visual{};
    J3DModel* model{};
    WireOverlay* overlay{};
    const GhostMtx* legacyJoints{};
    const u8* legacyScales{};
    const VisualMatrix* joints{};
    const u8* scales{};
    uint16_t jointCount{};
    const PuppetFrameView* frame{};

    static int animate(J3DJoint* joint, int pass) {
        if (pass != 0) {
            return 1;
        }
        auto& piece = *reinterpret_cast<Piece*>(j3dSys.getModel()->getUserArea());
        const auto index = joint->getJntNo();
        Mtx resolved;
        bool override = false;
        if (index < piece.jointCount && (piece.joints || piece.legacyJoints)) {
            if (piece.joints) {
                std::memcpy(resolved, piece.joints[index].m, sizeof resolved);
            } else {
                std::memcpy(resolved, piece.legacyJoints[index].m, sizeof resolved);
            }
            piece.model->setScaleFlag(
                index, piece.scales ? piece.scales[index] : piece.legacyScales[index]);
            override = true;
        } else if (piece.visual == Visual::Hands && piece.frame &&
                   piece.frame->info->jointCount > AL_JNT_HANDR_e) {
            const auto source = index == AL_HANDS_JNT_AL_HANDSL_e   ? AL_JNT_HANDL_e
                                : index == AL_HANDS_JNT_AL_HANDSR_e ? AL_JNT_HANDR_e
                                                                    : -1;
            if (source >= 0) {
                std::memcpy(resolved, piece.frame->bodyMtx[source].m, sizeof resolved);
                piece.model->setScaleFlag(index, piece.frame->scaleFlags[source]);
                override = true;
            }
        }
        if (override) {
            piece.model->setAnmMtx(index, resolved);
            MTXCopy(resolved, J3DSys::mCurrentMtx);
        }
        return 1;
    }
    bool build(Archives& archives) {
        auto* data = archives.instantiate(asset);
        if (!data || !data->getJointNum() || !data->getMaterialNum() || !data->getShapeNum()) {
            return false;
        }
        unsigned textures = 0;
        for (u16 i = 0; i < data->getMaterialNum(); ++i) {
            textures = std::max(textures, texture_slots(*data->getMaterialNodePointer(i)));
        }
        const u32 changes = J3DDiffFlag_MatColor | J3DDiffFlag_AmbColor | J3DDiffFlag_TevReg |
                            J3DDiffFlag_KonstColor | J3DDiffFlag_Fog | J3D_DIFF_LIGHTOBJNUM(8) |
                            J3D_DIFF_TEXNONUM(textures);
        model = mDoExt_J3DModel__create(data, J3DMdlFlag_DifferedDLBuffer, changes);
        if (!model) {
            return false;
        }
        model->setUserArea(reinterpret_cast<uintptr_t>(this));
        for (u16 i = 0; i < data->getJointNum(); ++i) {
            auto* joint = data->getJointNodePointer(i);
            joint->setCallBack(animate);
        }
        const auto rgb = ghost_color();
        overlay = JKR_NEW WireOverlay();
        if (!overlay ||
            !overlay->build(model, {static_cast<u8>(rgb >> 16), static_cast<u8>(rgb >> 8),
                                    static_cast<u8>(rgb), 155})) {
            report("Ghost wire overlay unavailable for a model part; keeping its full model");
            overlay = nullptr;
        }
        return true;
    }
    const VisualPart* recorded(const PuppetFrameView& f) const {
        return f.visualFrame ? &(*f.visualFrame)[static_cast<size_t>(visual)] : nullptr;
    }
    bool calculate(const PuppetFrameView& f, const f32 (*fallback)[4]) {
        joints = nullptr;
        scales = nullptr;
        legacyJoints = nullptr;
        legacyScales = nullptr;
        jointCount = 0;
        frame = &f;
        Mtx base;
        std::memcpy(base, fallback, sizeof base);
        const auto* state = recorded(f);
        if (visual == Visual::Body) {
            if (model->getModelData()->getJointNum() != f.info->jointCount) {
                frame = nullptr;
                return false;
            }
            jointCount = f.info->jointCount;
            legacyJoints = f.bodyMtx;
            legacyScales = f.scaleFlags;
        } else if (state && state->joints == model->getModelData()->getJointNum() &&
                   state->joints) {
            std::memcpy(base, f.visuals->matrices[state->matrices].m, sizeof base);
            joints = f.visuals->matrices.data() + state->matrices + 1;
            scales = f.visuals->scales.data() + state->matrices + 1;
            jointCount = state->joints;
        }
        model->setBaseTRMtx(base);
        model->calc();  // Callbacks run inside hierarchy traversal, before skinning/capture.
        auto* data = model->getModelData();
        for (u16 i = 0; i < data->getMaterialNum(); ++i) {
            auto* mat = data->getMaterialNodePointer(i);
            auto* shape = mat->getShape();
            bool hidden = false;
            if (state && i < state->materialCount) {
                hidden = (state->hidden & (uint64_t{1} << i)) != 0;
                const auto& material = f.visuals->materials[state->materials + i];
                for (unsigned t = 0; t < texture_slots(*mat); ++t) {
                    const auto texture = material.textures[t];
                    if (texture != 0xffff && texture < data->getTexture()->getNum()) {
                        mat->getTevBlock()->setTexNo(t, texture);
                    }
                }
            } else if (visual == Visual::Hands) {
                hidden = i != f.info->leftHandShape && i != f.info->rightHandShape;
            }
            if (shape) {
                if (hidden) {
                    shape->hide();
                } else {
                    shape->show();
                }
            }
        }
        frame = nullptr;
        joints = nullptr;
        scales = nullptr;
        legacyJoints = nullptr;
        legacyScales = nullptr;
        return true;
    }
    void detach() {
        if (!model) {
            return;
        }
        model->forgetMtx();
        auto* data = model->getModelData();
        for (u16 i = 0; i < data->getJointNum(); ++i) {
            auto* joint = data->getJointNodePointer(i);
            joint->setCallBack(nullptr);
        }
        model->setUserArea(0);
        model = nullptr;
    }
};

class ReplayPuppet final : public fopAc_ac_c {
    Archives archives;
    std::array<Piece, 64> pieces{};
    size_t used = 0;
    std::array<std::array<Piece*, visualCount>, kNumClothes> outfits{};
    std::array<Piece*, visualCount> wolf{};
    std::array<Piece*, kNumSwords> swords{}, sheaths{};
    std::array<Piece*, kNumShields> shields{};
    std::array<Piece*, kNumHeld> tools{};
    Piece* hook{};
    std::array<Piece*, visualCount> visible{};
    bool planned = false, complete = false, planFailed = false, wolfFrame = false;
    static size_t slot(Visual v) { return static_cast<size_t>(v); }
    Piece* reserve(Asset asset, Visual visual) {
        if (used == pieces.size()) {
            planFailed = true;
            return nullptr;
        }
        auto& piece = pieces[used++];
        piece.asset = asset;
        piece.visual = visual;
        if (!archives.request(asset.archive)) {
            planFailed = true;
        }
        return &piece;
    }
    void plan(const Recording& recording) {
        for (u8 c = 0; c < kNumClothes; ++c) {
            if (recording.clothesMask & (1 << c)) {
                const auto& a = costumes[c];
                auto& set = outfits[c];
                auto add = [&](Visual v, u16 id) {
                    set[slot(v)] = reserve({a.archive, id}, v);
                };
                add(Visual::Body, a.body);
                add(Visual::Hat, a.hat);
                add(Visual::Face, a.face);
                add(Visual::Hands, a.hands);
                add(Visual::Lantern, a.lantern);
                add(Visual::BootL, a.boots);
                add(Visual::BootR, a.boots);
                if (recording.swordMask & (1 << SwordWood)) {
                    add(Visual::Sword, a.wood);
                }
            }
        }
        if (recording.anyWolf) {
            wolf[slot(Visual::Body)] = reserve({"Wmdl", dRes_INDEX_WMDL_BMD_WL_e}, Visual::Body);
            for (int i = 0; i < 4; ++i) {
                const auto v = static_cast<Visual>(slot(Visual::Chain0) + i);
                wolf[slot(v)] = reserve({"Wmdl", dRes_INDEX_WMDL_BMD_WL_KUSARI_e}, v);
            }
        }
        if (recording.swordMask & (1 << SwordOrdon)) {
            swords[SwordOrdon] = reserve({"Alink", dRes_INDEX_ALINK_BMD_AL_SWA_e}, Visual::Sword);
            sheaths[SwordOrdon] =
                reserve({"Alink", dRes_INDEX_ALINK_BMD_AL_PODA_e}, Visual::Sheath);
        }
        if (recording.swordMask & ((1 << SwordMaster) | (1 << SwordWood))) {
            swords[SwordMaster] = reserve({"Alink", dRes_INDEX_ALINK_BMD_AL_SWM_e}, Visual::Sword);
            sheaths[SwordMaster] =
                reserve({"Alink", dRes_INDEX_ALINK_BMD_AL_PODM_e}, Visual::Sheath);
            sheaths[SwordWood] = sheaths[SwordMaster];
        }
        constexpr const char* shieldArchives[] = {nullptr, "CWShd", "SWShd", "HyShd"};
        for (u8 s = 1; s < kNumShields; ++s) {
            if (recording.shieldMask & (1 << s)) {
                shields[s] = reserve({shieldArchives[s], 3}, Visual::Shield);
            }
        }
        constexpr Asset heldAssets[] = {{nullptr, 0},
                                        {"Alink", dRes_INDEX_ALINK_BMD_AL_BOOM_e},
                                        {"AlAnm", dRes_INDEX_ALANM_BMD_AL_BOW_e},
                                        {"AlAnm", dRes_INDEX_ALANM_BMD_AL_PACHI_e},
                                        {"AlAnm", dRes_INDEX_ALANM_BMD_AL_HS_e},
                                        {"AlAnm", dRes_INDEX_ALANM_BMD_AL_IB_e},
                                        {"AlAnm", dRes_INDEX_ALANM_BMD_AL_CROD_e}};
        for (u8 t = 1; t < kNumHeld; ++t) {
            if (recording.heldMask & (1 << t)) {
                tools[t] = reserve(heldAssets[t], Visual::Held);
            }
        }
        if (recording.heldMask & (1 << HeldClawshot)) {
            hook = reserve({"AlAnm", dRes_INDEX_ALANM_BMD_AL_HS_TIP_e}, Visual::Hook);
        }
    }
    int allocate() {
        // entrySolidHeap may retry its callback with a new heap. Discard all
        // pointers from a failed allocation attempt before building again.
        for (size_t i = 0; i < used; ++i) {
            pieces[i].model = nullptr;
        }
        for (size_t i = 0; i < used; ++i) {
            if (!pieces[i].build(archives)) {
                report(std::format("Puppet resource failed: {} index {}", pieces[i].asset.archive,
                                   pieces[i].asset.index));
                return 0;
            }
        }
        return 1;
    }
    bool show(Piece* piece, const PuppetFrameView& frame, const f32 (*base)[4]) {
        if (!piece || !piece->calculate(frame, base)) {
            return false;
        }
        visible[slot(piece->visual)] = piece;
        return true;
    }

public:
    int create() {
        if (!planned) {
            const auto* recording = playback_recording();
            if (!recording) {
                return cPhs_ERROR_e;
            }
            plan(*recording);
            planned = true;
        }
        const int state = archives.poll();
        if (!state) {
            return cPhs_INIT_e;
        }
        if (state < 0 || planFailed || !playback_recording()) {
            return cPhs_ERROR_e;
        }
        if (!fopAcM_entrySolidHeap(
                this,
                [](fopAc_ac_c* actor) { return static_cast<ReplayPuppet*>(actor)->allocate(); },
                0x800000)) {
            return cPhs_ERROR_e;
        }
        complete = true;
        dKy_tevstr_init(&tevStr, fopAcM_GetRoomNo(this), 0xff);
        report(std::format("New ghost rig: {} independent model instances created", used));
        execute();
        return cPhs_COMPLEATE_e;
    }
    int execute() {
        // Replay visibility is controlled by the recorded stage/room and pose,
        // not the native actor's preset culling box.
        fopAcM_OffStatus(this, fopAcStts_CULL_e);
        visible.fill(nullptr);
        if (!complete) {
            return 1;
        }
        const auto frame = puppet_frame();
        if (!frame.valid()) {
            return 1;
        }
        const auto& f = *frame.info;
        if (f.clothes >= kNumClothes || f.sword >= kNumSwords || f.shield >= kNumShields ||
            f.held >= kNumHeld) {
            return 1;
        }
        current.pos = old.pos = f.pos;
        current.angle = shape_angle = f.angle;
        wolfFrame = f.form == kFormWolf;
        auto& outfit = wolfFrame ? wolf : outfits[f.clothes];
        auto* body = outfit[slot(Visual::Body)];
        if (!show(body, frame, frame.baseMtx->m)) {
            return 1;
        }
        fopAcM_SetMtx(this, body->model->getBaseTRMtx());
        if (!wolfFrame && f.jointCount > AL_JNT_HEAD_e) {
            auto* head = body->model->getAnmMtx(AL_JNT_HEAD_e);
            show(outfit[slot(Visual::Hat)], frame, head);
            show(outfit[slot(Visual::Face)], frame, head);
            show(outfit[slot(Visual::Hands)], frame, frame.baseMtx->m);
            for (Visual v : {Visual::BootL, Visual::BootR}) {
                if (frame.visualFrame && (*frame.visualFrame)[slot(v)].joints) {
                    show(outfit[slot(v)], frame, frame.baseMtx->m);
                }
            }
        }
        auto equip = [&](Piece* p, EquipSlot s) {
            if (f.visMask & (1 << s)) {
                show(p, frame, frame.equipMtx[s].m);
            }
        };
        equip(f.sword == SwordWood ? outfit[slot(Visual::Sword)] : swords[f.sword], SlotSword);
        equip(sheaths[f.sword], SlotSheath);
        equip(shields[f.shield], SlotShield);
        equip(tools[f.held], SlotHeld);
        if (f.held == HeldClawshot) {
            equip(hook, SlotHookTip);
        }
        if (!wolfFrame) {
            equip(outfit[slot(Visual::Lantern)], SlotKantera);
        } else {
            for (int i = 0; i < 4; ++i) {
                equip(wolf[slot(Visual::Chain0) + i], static_cast<EquipSlot>(SlotChain0 + i));
            }
        }
        return 1;
    }
    int draw() {
        // Use the pose committed by execute(); don't query a second timestamp
        // and discard geometry that was already accepted for this frame.
        if (!visible[slot(Visual::Body)]) {
            return 1;
        }
        g_env_light.settingTevStruct(wolfFrame ? 9 : 10, &current.pos, &tevStr);
        if (wolfFrame) {
            dComIfGd_setListDark();
        } else {
            dComIfGd_setList();
        }
        for (auto* piece : visible) {
            if (piece) {
                g_env_light.setLightTevColorType_MAJI(piece->model, &tevStr);
                mDoExt_modelEntryDL(piece->model);
            }
        }
        for (auto* piece : visible) {
            if (piece && piece->overlay) {
                piece->overlay->submit();
            }
        }
        dComIfGd_setList();
        return 1;
    }
    int dispose() {
        if (!archives.poll()) {
            return 0;
        }
        if (complete) {
            for (size_t i = 0; i < used; ++i) {
                pieces[i].detach();
            }
        }
        // Release models and their private buffers before archive unmounts.
        fopAcM_DeleteHeap(this);
        this->~ReplayPuppet();
        return 1;
    }
};
ActorHandle registration = 0;
}  // namespace
ModResult register_puppet() {
    ActorProfileDesc descriptor{"RushRig"};
    descriptor.priority_group = 7;
    descriptor.process_size = sizeof(ReplayPuppet);
    descriptor.draw_priority = fpcDwPi_ALINK_e;
    descriptor.status = fopAcStts_UNK_0x40000_e;
    descriptor.group = fopAc_ACTOR_e;
    descriptor.cull_type = fopAc_CULLBOX_0_e;
    descriptor.create_function = [](void* memory) {
        auto* actor = static_cast<ReplayPuppet*>(memory);
        fopAcM_ct(actor, ReplayPuppet);
        return actor->create();
    };
    descriptor.execute_function = [](void* a) {
        return static_cast<ReplayPuppet*>(a)->execute();
    };
    descriptor.draw_function = [](void* a) {
        return static_cast<ReplayPuppet*>(a)->draw();
    };
    descriptor.delete_function = [](void* a) {
        return static_cast<ReplayPuppet*>(a)->dispose();
    };
    descriptor.is_delete_function = [](void*) {
        return 1;
    };
    return svc_actor->register_actor(mod_ctx, &descriptor, &puppet_profile, &registration);
}
ModResult unregister_puppet() {
    if (!registration) {
        return MOD_OK;
    }
    const auto result = svc_actor->unregister_actor(mod_ctx, registration);
    if (result == MOD_OK) {
        registration = 0;
    }
    return result;
}
}  // namespace rush::ghost
