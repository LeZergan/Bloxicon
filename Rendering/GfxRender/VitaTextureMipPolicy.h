#pragma once
#include <cstddef>

namespace RBX { namespace Graphics {
struct VitaTextureMipRange { unsigned first; unsigned count; bool fits; };

template<class SizeOfRange>
inline VitaTextureMipRange selectVitaTextureMips(unsigned levels, size_t available, bool preserveResolution, SizeOfRange size)
{
    if (!levels) return {0, 0, false};
    if (size(0, levels) <= available) return {0, levels, true};
    if (size(0, 1) <= available) return {0, 1, true};
    if (!preserveResolution)
        for (unsigned first = 1; first < levels; ++first) {
            if (size(first, levels - first) <= available) return {first, levels - first, true};
            if (size(first, 1) <= available) return {first, 1, true};
        }
    return {0, 0, false};
}
} }
