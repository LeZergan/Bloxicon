#include <stdint.h>

#include "rbx/SystemUtil.h"

#include <psp2/kernel/sysmem.h>

namespace RBX
{
namespace SystemUtil
{

std::string getCPUMake()
{
    return "ARM Cortex-A9";
}

uint64_t getCPUSpeed()
{
    // CPU frequency may be changed at runtime by user tools; no stable public
    // VitaSDK query is exposed here, so do not report a stale fixed clock.
    return 0;
}

uint64_t getCPULogicalCount()
{
    return getCPUCoreCount();
}

uint64_t getCPUCoreCount()
{
    // VitaSDK exposes USER_0, USER_1 and USER_2 as the application CPU set;
    // the fourth Cortex-A9 core is the system CPU.
    return 3;
}

uint64_t getCPUPhysicalCount()
{
    return getCPUCoreCount();
}

bool isCPU64Bit()
{
    return false;
}

uint64_t getMBSysRAM()
{
    return 512;
}

uint64_t getMBSysAvailableRAM()
{
    SceKernelFreeMemorySizeInfo info = {};
    info.size = sizeof(info);

    if (sceKernelGetFreeMemorySize(&info) < 0)
        return 0;

    const uint64_t freeBytes =
        static_cast<uint64_t>(info.size_user) +
        static_cast<uint64_t>(info.size_phycont);

    return freeBytes / (1024 * 1024);
}

uint64_t getVideoMemory()
{
    return 128ull * 1024ull * 1024ull;
}

std::string osPlatform()
{
    return "PS Vita";
}

int osPlatformId()
{
    return 0;
}

std::string osVer()
{
    return "";
}

std::string deviceName()
{
    return sceKernelIsPSVitaTV() ? "PlayStation TV" : "PlayStation Vita";
}

std::string getGPUMake()
{
    return "PowerVR SGX543MP4+";
}

std::string getMaxRes()
{
    return "960x544";
}

} // namespace SystemUtil
} // namespace RBX
