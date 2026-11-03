#pragma once
#include <cstddef>

namespace RBX {
// Lua accounts for its wrappers, not the native instance trees they own.
// Advance its existing collector at safe API boundaries when the shared heap
// is tight. Live Lua references and authored objects keep normal GC ownership.
class VitaLuaMemoryPressure {
    double nextSample = 0;
    double nextCollection = 0;
    bool collecting = false;
public:
    template<class Headroom, class Collect, class Clock>
    void step(double now, Headroom headroom, Collect collect, Clock clock)
    {
        if (collecting || now < nextSample) return;
        nextSample = now + 0.05;
        if (headroom() >= 16u * 1024u * 1024u || now < nextCollection) return;
        nextCollection = now + 0.05;
        struct Guard {
            bool& value;
            explicit Guard(bool& value): value(value) { value = true; }
            ~Guard() { value = false; }
        } guard(collecting);
        collect();
        // A long finalizer must not make another step immediately overdue.
        nextSample = nextCollection = clock() + 0.05;
    }
};
}
