// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>

namespace RBX { namespace Graphics {
class VitaTextureCachePressure
{
    double nextCheck = 0;
public:
    static constexpr size_t reserve = 16u * 1024u * 1024u;
    static bool canRetain(size_t bytes, size_t headroom)
    {
        return headroom >= reserve && bytes <= headroom - reserve;
    }
    template<class Headroom, class Release>
    void reclaim(double now, bool hasCache, Headroom headroom, Release release)
    {
        if (!hasCache || now < nextCheck) return;
        nextCheck = now + 0.25;
        if (headroom() < reserve) release();
    }
};
} }
