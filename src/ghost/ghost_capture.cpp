#include "ghost/ghost.hpp"
#include "ghost/ghost_material.hpp"
#include "d/actor/d_a_alink.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DMaterial.h"
#include "JSystem/J3DGraphBase/J3DShape.h"
#include <cstring>
#include <stdexcept>

namespace rush::ghost {
void capture_visuals(daAlink_c& player, Recording& recording, const FrameInfo& frame) {
    auto& track = recording.visuals;
    const auto matrixStart = track.matrices.size(), materialStart = track.materials.size();
    VisualFrame result{};
    auto capture = [&](Visual which, J3DModel* model, bool visible) {
        if (!model || !visible) {
            return;
        }
        auto* data = model->getModelData();
        if (!data || data->getJointNum() > 96 || data->getMaterialNum() > 64) {
            return;
        }
        auto& part = result[static_cast<size_t>(which)];
        part.materials = static_cast<uint32_t>(track.materials.size());
        part.materialCount = data->getMaterialNum();
        for (u16 i = 0; i < data->getMaterialNum(); ++i) {
            auto* material = data->getMaterialNodePointer(i);
            VisualMaterial state{};
            state.textures.fill(0xffff);
            for (u32 j = 0; j < texture_slots(*material); ++j) {
                state.textures[j] = material->getTexNo(j);
            }
            track.materials.push_back(state);
            if (auto* shape = material->getShape(); shape && shape->checkFlag(J3DShpFlag_Visible)) {
                part.hidden |= uint64_t{1} << i;
            }
        }
        // Body bones already exist in the base recording. This extension holds
        // the independent animations of the remaining models and materials.
        if (which == Visual::Body) {
            return;
        }
        part.matrices = static_cast<uint32_t>(track.matrices.size());
        part.joints = data->getJointNum();
        auto matrix = [&](const f32(*m)[4], u8 scale) {
            VisualMatrix copy;
            std::memcpy(copy.m, m, sizeof copy.m);
            track.matrices.push_back(copy);
            track.scales.push_back(scale);
        };
        matrix(model->getBaseTRMtx(), 1);
        for (u16 j = 0; j < part.joints; ++j) {
            matrix(model->getAnmMtx(j), model->getMtxBuffer()->getScaleFlag(j));
        }
    };
    try {
        const bool human = frame.form == kFormHuman;
        capture(Visual::Body, player.mpLinkModel, true);
        capture(Visual::Hat, player.mpLinkHatModel, human);
        capture(Visual::Face, player.mpLinkFaceModel, human);
        capture(Visual::Hands, player.mpLinkHandModel, human);
        auto equipment = [&](Visual visual, J3DModel* model, EquipSlot slot) {
            capture(visual, model, (frame.visMask & (1 << slot)) != 0);
        };
        equipment(Visual::Sword, player.mSwordModel, SlotSword);
        equipment(Visual::Sheath, player.mSheathModel, SlotSheath);
        equipment(Visual::Shield, player.mShieldModel, SlotShield);
        equipment(Visual::Held, player.mHeldItemModel, SlotHeld);
        equipment(Visual::Hook, player.mpHookTipModel, SlotHookTip);
        equipment(Visual::Lantern, player.mpKanteraModel, SlotKantera);
        for (int i = 0; i < 4; ++i) {
            equipment(static_cast<Visual>(static_cast<int>(Visual::Chain0) + i),
                      player.mpWlChainModels[i], static_cast<EquipSlot>(SlotChain0 + i));
        }
        const bool boots = human && player.checkEquipHeavyBoots();
        capture(Visual::BootL, player.mpLinkBootModels[0], boots);
        capture(Visual::BootR, player.mpLinkBootModels[1], boots);
        if (visual_bytes(track) + sizeof result > visualBudget) {
            throw std::runtime_error("Ghost animation recording reached its memory limit");
        }
        track.frames.push_back(result);
    } catch (...) {
        track.matrices.resize(matrixStart);
        track.scales.resize(matrixStart);
        track.materials.resize(materialStart);
        throw;
    }
}
}  // namespace rush::ghost
