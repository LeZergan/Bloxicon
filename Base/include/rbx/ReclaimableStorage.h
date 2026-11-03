#pragma once
#include <new>
#include <utility>

namespace RBX {
// Temporary native physics trees must return their storage to the shared heap
// when collected, rather than pinning the peak in a retaining singleton pool.
// The object, constructor/destructor and desktop allocator stay unchanged.
template<class T, class... Args>
T* createReclaimableStorage(Args&&... args)
{
#ifdef RBX_PLATFORM_PSVITA
    void* storage = ::operator new(sizeof(T));
    try { return ::new (storage) T(std::forward<Args>(args)...); }
    catch (...) { ::operator delete(storage); throw; }
#else
    return new T(std::forward<Args>(args)...);
#endif
}

template<class T>
void destroyReclaimableStorage(T* object)
{
#ifdef RBX_PLATFORM_PSVITA
    if (object)
    {
        object->~T();
        ::operator delete(object);
    }
#else
    delete object;
#endif
}
}
