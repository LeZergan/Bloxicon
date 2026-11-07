#pragma once

#include <array>
#include <cstddef>

namespace VitaBrowser { namespace Typography {

// Rasterize the loader faces at native display sizes. Captions use at least 18px.
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

template<class Measure>
inline float fitPixels(float requested,float width,Measure measure)
{
    size_t index=sizeIndex(requested);
    while(index>1 && measure(float(sizes[index]))>width)--index;
    return float(sizes[index]);
}

} }
