#pragma once
#include <atomic>
#include <cstddef>

namespace RBX {
// Request policy has no Http data members: archived engine callers retain
// their original object layout. Each worker owns its nested synchronous scope.
struct VitaHttpRequestPolicy {
    size_t maxBytes = 16u * 1024u * 1024u;
    const std::atomic<bool>* cancelled = nullptr;
};
inline VitaHttpRequestPolicy& vitaHttpRequestPolicy() {
    static thread_local VitaHttpRequestPolicy policy;
    return policy;
}
class VitaHttpRequestScope {
    VitaHttpRequestPolicy previous;
public:
    explicit VitaHttpRequestScope(size_t maxBytes, const std::atomic<bool>* cancelled = nullptr)
        : previous(vitaHttpRequestPolicy()) {
        vitaHttpRequestPolicy().maxBytes = maxBytes;
        if (cancelled) vitaHttpRequestPolicy().cancelled = cancelled;
    }
    ~VitaHttpRequestScope() { vitaHttpRequestPolicy() = previous; }
    VitaHttpRequestScope(const VitaHttpRequestScope&) = delete;
    VitaHttpRequestScope& operator=(const VitaHttpRequestScope&) = delete;
};
}
