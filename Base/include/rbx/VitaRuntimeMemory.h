#pragma once
#ifdef RBX_PLATFORM_PSVITA
#include <atomic>
#include <new>
#include <cstddef>
namespace RBX {
// Process-wide latch. Once exhausted, stop scheduling work until the renderer
// hands off to a fresh loader process. This path never allocates.
inline std::atomic<bool>& vitaRuntimeMemoryFailureLatch() {
    static std::atomic<bool> failed(false);
    return failed;
}
inline bool vitaRuntimeMemoryFailed() {
    return vitaRuntimeMemoryFailureLatch().load(std::memory_order_acquire);
}
inline std::atomic<unsigned>& vitaRuntimeMemoryFailureReason() {
    static std::atomic<unsigned> reason(0);
    return reason;
}
inline std::atomic<size_t>& vitaRuntimeMemoryFailureBytes() {
    static std::atomic<size_t> bytes(0);
    return bytes;
}
// Fixed numeric context survives OOM without allocating an error string:
// 1 scheduler, 2 Lua limit, 3 Lua allocation, 4 Lua reference, 5 Lua error.
inline void requestVitaRuntimeMemoryRecovery(unsigned reason = 0, size_t bytes = 0) {
    unsigned unset = 0;
    if (vitaRuntimeMemoryFailureReason().compare_exchange_strong(unset, reason))
        vitaRuntimeMemoryFailureBytes().store(bytes, std::memory_order_relaxed);
    vitaRuntimeMemoryFailureLatch().store(true, std::memory_order_release);
}
template<class Report> inline void vitaGuardRuntimeErrorReport(Report report) {
    if (vitaRuntimeMemoryFailed()) return;
    try { report(); }
    catch (const std::bad_alloc&) { requestVitaRuntimeMemoryRecovery(); }
}
}
#endif
