#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace VitaBrowser {

// A presentation-only snapshot of the six visible catalogue badges. Actual
// Play/Install/Update actions still consult the original filesystem resolver.
// Navigation/mutations invalidate it; idle external-file changes are sampled
// as one badge per frame, once per second, rather than six disk reads per frame.
template<class State, std::size_t Capacity = 6>
class CatalogBadgeSnapshot
{
    std::array<State, Capacity> states{};
    std::size_t first = 0, count = 0, totalSeen = 0, refreshCursor = 0;
    std::size_t refreshRemaining = 0;
    std::uint64_t lastSweep = 0;
    bool dirty = true;

public:
    void invalidate() noexcept { dirty = true; }

    template<class Resolver>
    void refresh(std::size_t total, std::size_t pageFirst, std::uint64_t now, Resolver resolve)
    {
        if (dirty || first != pageFirst || totalSeen != total)
        {
            first = pageFirst;
            totalSeen = total;
            count = first < total ? std::min(Capacity, total - first) : 0;
            for (std::size_t i = 0; i < count; ++i) states[i] = resolve(first + i);
            refreshRemaining = 0;
            refreshCursor = 0;
            lastSweep = now;
            dirty = false;
            return;
        }
        if (now < lastSweep) lastSweep = now; // tolerate a reset host/device clock
        if (!refreshRemaining && now - lastSweep >= 1000000)
        {
            refreshRemaining = count;
            refreshCursor = 0;
            lastSweep = now;
        }
        if (refreshRemaining)
        {
            states[refreshCursor] = resolve(first + refreshCursor);
            ++refreshCursor;
            --refreshRemaining;
        }
    }

    bool lookup(std::size_t index, State& result) const noexcept
    {
        if (dirty || index < first || index - first >= count) return false;
        result = states[index - first];
        return true;
    }
};

} // namespace VitaBrowser
