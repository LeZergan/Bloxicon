#pragma once
#include <psp2/kernel/threadmgr.h>

namespace VitaCorePolicy {
inline int dormantProbeEntry(SceSize,void*){return 0;}
// Probe a disposable, never-started thread. The render thread is never moved
// onto the system core just to discover whether CapUnlocker allows the mask.
inline bool extraCoreAvailable(){
    const SceUID probe=sceKernelCreateThread("bloxicon_core_probe",dormantProbeEntry,
        0x10000100,0x1000,0,SCE_KERNEL_CPU_MASK_USER_ALL,nullptr);
    if(probe<0)return false;
    const int desired=SCE_KERNEL_CPU_MASK_USER_ALL|SCE_KERNEL_CPU_MASK_SYSTEM;
    const int changed=sceKernelChangeThreadCpuAffinityMask(probe,desired);
    const int actual=changed>=0?sceKernelGetThreadCpuAffinityMask(probe):changed;
    const bool allowed=actual>=0&&(actual&desired)==desired;
    const int removed=sceKernelDeleteThread(probe);
    return allowed&&removed>=0;
}
}
