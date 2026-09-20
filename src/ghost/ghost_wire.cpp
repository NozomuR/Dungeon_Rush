#include "ghost/ghost_wire.hpp"
#include "ghost/ghost_wire_geometry.hpp"
#include <aurora/dl.hpp>
#include <algorithm>
#include <set>
#include <vector>
#include <cstring>
#include <mods/svc/hook.hpp>
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DModelData.h"
#include "JSystem/J3DGraphBase/J3DShape.h"
#include "JSystem/J3DGraphBase/J3DShapeMtx.h"
#include "JSystem/J3DGraphBase/J3DDrawBuffer.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "d/d_com_inf_game.h"

namespace rush::ghost {
namespace {
using DlReader = aurora::gx::dl::Reader;
using DlCommand = aurora::gx::dl::Command;
DEFINE_HOOK_SYMBOL(
    "??0Reader@dl@gx@aurora@@QEAA@PEBEIPEBUGXVtxDescList@@PEBV?$array@PEBUGXVtxAttrFmtList@@$07@std@@@Z",
    void(DlReader*, const u8*, u32, const GXVtxDescList*, const aurora::gx::dl::VtxFmtLists*),
    ReaderInit);
DEFINE_HOOK_SYMBOL("?next@Reader@dl@gx@aurora@@QEAA?AV?$optional@UCommand@dl@gx@aurora@@@std@@XZ",
                   std::optional<DlCommand>(DlReader*), ReaderNext);
std::vector<u8> wire_list(J3DShapeDraw& source, J3DShape& shape) {
    using namespace aurora::gx::dl;
    if (!ReaderInit::resolved_target() || !ReaderNext::resolved_target()) {
        return {};
    }
    // Use the host decoder and its matching header, including Aurora's indexed
    // draw extension. These engine-internal methods are resolved by the SDK.
    alignas(Reader) std::byte storage[sizeof(Reader)];
    auto* reader = reinterpret_cast<Reader*>(storage);
    reinterpret_cast<void (*)(Reader*, const u8*, u32, const GXVtxDescList*, const VtxFmtLists*)>(
        ReaderInit::resolved_target())(reader, source.getDisplayList(), source.getDisplayListSize(),
                                       shape.getVtxDesc(), nullptr);
    const auto next =
        reinterpret_cast<std::optional<Command> (*)(Reader*)>(ReaderNext::resolved_target());
    static_assert(std::is_trivially_destructible_v<Reader>);
    std::vector<u8> out;
    while (auto cmd = next(reader)) {
        if (cmd->kind == Command::Kind::Passthrough) {
            out.insert(out.end(), cmd->data, cmd->data + cmd->size);
            continue;
        }
        const auto& d = cmd->draw;
        WireEdges edges;
        auto triangle = [&](u16 a, u16 b, u16 c) {
            edges.triangle(a, b, c);
        };
        if (cmd->kind == Command::Kind::DrawIndexed) {
            if (d.indexCount % 3) {
                return {};
            }
            auto index = [&](u32 i) {
                u16 value;
                std::memcpy(&value, d.indices + i * 2, 2);
                return value;
            };
            for (u32 i = 0; i < d.indexCount; i += 3) {
                triangle(index(i), index(i + 1), index(i + 2));
            }
        } else if (!expand_triangles(d.prim, d.vtxCount, triangle)) {
            return {};
        }
        const auto lines = edges.encode({d.vertices, size_t(d.vtxCount) * d.layout->stride},
                                        d.vtxCount, d.layout->stride, static_cast<u8>(d.fmt));
        if (!lines) {
            return {};
        }
        out.insert(out.end(), lines->begin(), lines->end());
    }
    if (reader->failed()) {
        return {};
    }
    return out;
}
void style(GXColor color) {
    GXSetNumChans(0);
    GXSetNumTexGens(0);
    GXSetNumIndStages(0);
    GXSetNumTevStages(1);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevColor(GX_TEVREG0, color);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    GXSetZCompLoc(GX_ENABLE);
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_DISABLE);
    GXSetFog(GX_FOG_NONE, 0, 0, 0, 0, {0, 0, 0, 0});
    GXSetFogRangeAdj(GX_DISABLE, 0, nullptr);
    GXSetCullMode(GX_CULL_BACK);
    GXSetLineWidth(6, GX_TO_ZERO);
}
}  // namespace
bool WireOverlay::build(J3DModel* source, GXColor hue) {
    model = source;
    color = hue;
    auto* data = model->getModelData();
    size_t total = 0;
    for (u16 i = 0; i < data->getShapeNum(); ++i) {
        total += data->getShapeNodePointer(i)->getMtxGroupNum();
    }
    groups = JKR_NEW_ARRAY(Group, total);
    if (!groups) {
        return false;
    }
    for (u16 i = 0; i < data->getShapeNum(); ++i) {
        auto* shape = data->getShapeNodePointer(i);
        for (u16 g = 0; g < shape->getMtxGroupNum(); ++g) {
            auto* original = shape->getShapeDraw(g);
            if (!original) {
                continue;
            }
            const auto bytes = wire_list(*original, *shape);
            if (bytes.empty()) {
                return false;
            }
            auto* lines = JKR_NEW J3DShapeDraw(bytes.data(), static_cast<u32>(bytes.size()));
            if (!lines) {
                return false;
            }
            groups[count++] = {i, g, lines};
        }
    }
    return count != 0;
}
void WireOverlay::submit() {
    drawClear();
    dComIfGd_setList3Dlast();
    j3dSys.getDrawBuffer(1)->entryImm(this, 0);
    dComIfGd_setList();
}
void WireOverlay::draw() {
    j3dSys.reinitGX();
    auto* data = model->getModelData();
    for (size_t i = 0; i < count; ++i) {
        const auto& group = groups[i];
        auto* shape = data->getShapeNodePointer(group.shape);
        auto* packet = model->getShapePacket(group.shape);
        if (shape->checkFlag(J3DShpFlag_Visible) || packet->checkFlag(J3DShpFlag_Hidden)) {
            continue;
        }
        packet->prepareDraw();
        J3DShape::resetVcdVatCache();
        shape->loadPreDrawSetting();
        shape->setArrayAndBindPipeline();
        J3DDifferedTexMtx::sTexGenBlock = nullptr;
        J3DDifferedTexMtx::sTexMtxObj = nullptr;
        if (shape->checkFlag(J3DShpFlag_NoMtx)) {
            GXLoadPosMtxImm(*packet->getBaseMtxPtr(), GX_PNMTX0);
        } else if (auto* matrix = shape->getShapeMtx(group.group)) {
            matrix->load();
        }
        auto wash = color;
        wash.a = 22;
        style(wash);
        shape->getShapeDraw(group.group)->draw();
        auto lines = color;
        lines.a = 155;
        style(lines);
        group.lines->draw();
    }
    j3dSys.reinitGX();
    J3DShape::resetVcdVatCache();
}
}  // namespace rush::ghost
