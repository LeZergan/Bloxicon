#pragma once
#include <cstdint>
namespace VitaLoading {
constexpr uint64_t nativePreparationLimit = 300000000ULL;
constexpr uint64_t authoredLoadingLimit = 180000000ULL;
inline bool elapsed(uint64_t now, uint64_t start, uint64_t limit) {
    return start != 0 && now >= start && now - start >= limit;
}
struct ScreenWatch {
    uint64_t visibleSince = 0;
    bool update(uint64_t now, bool visible) {
        if (!visible) { visibleSince = 0; return false; }
        if (!visibleSince) visibleSince = now;
        return elapsed(now, visibleSince, authoredLoadingLimit);
    }
    void keepWaiting(uint64_t now) { visibleSince = now; }
};
}
