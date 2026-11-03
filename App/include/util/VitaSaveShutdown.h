#pragma once
#include <atomic>
namespace RBX {
inline std::atomic<unsigned>& vitaDeferredSaveDrains() { static std::atomic<unsigned> count(0); return count; }
inline bool vitaSavesStillDraining() { return vitaDeferredSaveDrains().load(std::memory_order_acquire) != 0; }
inline std::atomic<bool>& vitaCallbackFailureLatch() { static std::atomic<bool> failed(false); return failed; }
inline void requestVitaCallbackRecovery() { vitaCallbackFailureLatch().store(true, std::memory_order_release); }
}
