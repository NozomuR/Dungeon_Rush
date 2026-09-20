#pragma once
#include "JSystem/J3DGraphBase/J3DPacket.h"
class J3DModel;
class J3DShapeDraw;
namespace rush::ghost {
class WireOverlay final : public J3DPacket {
    struct Group {
        u16 shape, group;
        J3DShapeDraw* lines;
    };
    J3DModel* model{};
    Group* groups{};
    size_t count{};
    GXColor color{};

public:
    bool build(J3DModel*, GXColor);
    void submit();
    void draw() override;
};
}  // namespace rush::ghost
