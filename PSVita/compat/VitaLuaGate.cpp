#include "util/VitaLuaGate.h"
#include <mutex>
#include <cassert>
#include <new>

namespace {
std::mutex& vmMutex() {
    // Reflection descriptors copy weak Lua references before main. Vita's
    // pthread mutex needs its constructor before those copies take the gate.
    // Keep it alive for late static weak-reference destruction as well.
    alignas(std::mutex) static unsigned char storage[sizeof(std::mutex)];
    static std::mutex* const mutex = new (storage) std::mutex;
    return *mutex;
}
thread_local unsigned vmDepth = 0;
}
namespace RBX {
void lockVitaLua() {
    if (!vmDepth) vmMutex().lock();
    ++vmDepth;
}
void unlockVitaLua() {
    assert(vmDepth);
    if (!--vmDepth) vmMutex().unlock();
}
}
