#pragma once
#include <new>

namespace VitaLoading {
enum class ExitFailure { Memory, Other };

// A failed exit must not unwind the live world/GPU owners in the caller.
// Recovery either waits for an explicit retry or requests the no-heap handoff.
template<class Work,class Recovery>
void guardPlaceExit(Work&& work,Recovery&& recovery) {
    static_assert(noexcept(recovery(ExitFailure::Other)),"Exit recovery must retain the live process");
    for(;;) {
        ExitFailure failure=ExitFailure::Other;
        try { work();return; }
        catch(const std::bad_alloc&) { failure=ExitFailure::Memory; }
        catch(...) {}
        recovery(failure);
    }
}
}
