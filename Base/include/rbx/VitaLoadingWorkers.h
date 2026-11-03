#pragma once
#include <atomic>
namespace RBX {
inline std::atomic<bool>& vitaLoadingCpu3Enabled() { static std::atomic<bool> enabled(false); return enabled; }
}
