#pragma once

#include <array>
#include <cstddef>

namespace VitaBrowser { namespace Typography {

// Rasterize the shipped Source Sans faces at the actual display size. The
// minimum applies to captions too: shrinking a 32px atlas made these illegible.
static const std::array<unsigned, 8> sizes = {{16, 18, 20, 22, 24, 26, 28, 32}};
static const unsigned atlasWidth = 1024;
static const unsigned atlasHeight = 512;

inline size_t sizeIndex(float requested)
{
    // Captions use a native 18px face; retain 16px in the atlas for compatibility.
    for (size_t i = 1; i + 1 < sizes.size(); ++i)
        if (requested <= float(sizes[i])) return i;
    return sizes.size() - 1;
}

inline unsigned pixels(float requested)
{
    return sizes[sizeIndex(requested)];
}

} }
