#pragma once
#include "JSystem/J3DGraphBase/J3DMaterial.h"
namespace rush::ghost {
inline unsigned texture_slots(J3DMaterial& material) {
    switch (material.getTevBlock()->getType()) {
        case 'TVB1':
            return 1;
        case 'TVB2':
            return 2;
        case 'TVB4':
            return 4;
        case 'TVPT':
        case 'TV16':
            return 8;
        default:
            return 0;
    }
}
}  // namespace rush::ghost
