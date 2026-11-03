#pragma once
#include <psp2/kernel/threadmgr.h>

namespace RBX {
// Own one instance per worker thread. CPU3 is extra eligibility: essential work
// can always run on the application cores, even with CapUnlocker enabled.
class VitaWorkerAffinity {
    int activeMask_ = -1;
    bool rejected_ = false;
public:
    int activeMask() const { return activeMask_; }
    int configure(SceUID thread, bool allowExtraCore) {
        if (!allowExtraCore) rejected_ = false;
        const int user = SCE_KERNEL_CPU_MASK_USER_ALL;
        const int desired = allowExtraCore && !rejected_
            ? user | SCE_KERNEL_CPU_MASK_SYSTEM : user;
        if (activeMask_ == desired) return activeMask_;
        const int result = sceKernelChangeThreadCpuAffinityMask(thread, desired);
        const int observed = result >= 0 ? sceKernelGetThreadCpuAffinityMask(thread) : result;
        activeMask_ = observed == desired ? desired : -1;
        if (activeMask_ < 0 && desired != user) {
            rejected_ = true;
            const int fallback = sceKernelChangeThreadCpuAffinityMask(thread, user);
            activeMask_ = fallback >= 0 && sceKernelGetThreadCpuAffinityMask(thread) == user
                ? user : -1;
        }
        // An unverified fallback is retried on the next task; never cache it
        // as an applied mask. A rejected extra mask waits for an off/on toggle.
        return activeMask_;
    }
};
}
