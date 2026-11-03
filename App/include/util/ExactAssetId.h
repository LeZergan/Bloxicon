#pragma once

#include <cmath>
#include <cstdint>

namespace RBX {
// Lua and the existing reflected/network number type use doubles. Preserve
// every integer they can represent exactly, rather than narrowing to int32.
inline bool exactAssetId(double number, int64_t& id)
{
    if (!(number > 0.0 && number <= 9007199254740991.0) ||
        std::floor(number) != number)
        return false;
    id = static_cast<int64_t>(number);
    return true;
}
}
