// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstring>
#include <vector>

namespace RBX { namespace Graphics {
// Published GLSL packs inherited an RGB average typo from screenspace.hlsl.
// Correct that expression before shader compilation and cache hashing while
// retaining every other byte and the pack's sampler/uniform declarations.
inline void correctImageProcessGrayscale(std::vector<char>& bytecode)
{
    const char before[] = "(color_1.x + color_1.y) + color_1.y";
    const char after[] = "(color_1.x + color_1.y) + color_1.z";
    const size_t size = sizeof(before) - 1;
    auto position = std::search(bytecode.begin(), bytecode.end(), before, before + size);
    if (position != bytecode.end()) std::copy(after, after + size, position);
}
} }
