#pragma once
#include <atomic>
namespace RBX { namespace VitaBundledContentPolicy {
inline std::atomic<bool>& state() { static std::atomic<bool> value(false);return value; }
inline void set(bool enabled) { state().store(enabled,std::memory_order_release); }
inline bool enabled() { return state().load(std::memory_order_acquire); }
} }
