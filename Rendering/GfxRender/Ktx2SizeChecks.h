#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace RBX { namespace Graphics {

// Image mip offsets and the decoder's pixel indexing use unsigned int. Check
// before narrowing or allocating, on both 32-bit Vita and desktop builds.
inline uint64_t ktx2SizeLimit()
{
    return std::min(uint64_t(std::numeric_limits<unsigned int>::max()),
        uint64_t(std::numeric_limits<size_t>::max()));
}

inline size_t checkedKtx2PixelBytes(unsigned int width, unsigned int height)
{
    if (!width || !height || uint64_t(width) > ktx2SizeLimit() / 4 / height)
        throw std::runtime_error("KTX2 decoded dimensions exceed image storage");
    return size_t(uint64_t(width) * height * 4);
}

inline size_t checkedKtx2BlockBytes(unsigned int width, unsigned int height, unsigned int blockBytes)
{
    if (!width || !height || (blockBytes != 8 && blockBytes != 16))
        throw std::runtime_error("Invalid KTX2 block layout");
    const uint64_t columns = (uint64_t(width) + 3) / 4;
    const uint64_t rows = (uint64_t(height) + 3) / 4;
    if (columns > ktx2SizeLimit() / blockBytes / rows)
        throw std::runtime_error("KTX2 block dimensions exceed image storage");
    return size_t(columns * rows * blockBytes);
}

inline size_t checkedKtx2MipChainBytes(unsigned int width, unsigned int height, unsigned int levels, unsigned int blockBytes = 0)
{
    if (!levels || levels > 32)
        throw std::runtime_error("Invalid KTX2 mip count");
    uint64_t total = 0;
    for (unsigned int level = 0; level < levels; ++level)
    {
        const size_t bytes = blockBytes ? checkedKtx2BlockBytes(width, height, blockBytes) : checkedKtx2PixelBytes(width, height);
        if (bytes > ktx2SizeLimit() - total)
            throw std::runtime_error("KTX2 mip chain exceeds image storage");
        total += bytes;
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    return size_t(total);
}

} }
