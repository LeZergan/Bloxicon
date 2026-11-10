// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <new>
#include <cstddef>
#include <vector>
#include <atomic>

namespace RBX { namespace Graphics {

// Allocation recovery may run on script/asset threads. Only an idle scratch
// cache can be reclaimed; mapped GPU uploads never retain these CPU pointers.
template<class Vertex> class VitaGeometryScratchCache
{
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
public:
    std::vector<Vertex> vertices;
    std::vector<unsigned short> indices;
    struct Use
    {
        VitaGeometryScratchCache& cache;
        explicit Use(VitaGeometryScratchCache& cache): cache(cache)
        {
            while (cache.busy.test_and_set(std::memory_order_acquire)) {}
        }
        ~Use() { cache.busy.clear(std::memory_order_release); }
        Use(const Use&) = delete;
        Use& operator=(const Use&) = delete;
    };
    bool releaseIfIdle()
    {
        if (busy.test_and_set(std::memory_order_acquire)) return false;
        const bool released = vertices.capacity() || indices.capacity();
        std::vector<Vertex>().swap(vertices);
        std::vector<unsigned short>().swap(indices);
        busy.clear(std::memory_order_release);
        return released;
    }
};

// Rebuild scratch has no live mesh bytes to preserve between calls. Discard
// undersized storage before growing so vector growth cannot temporarily retain
// two full world meshes or double the persistent high-water allocation.
template<class Vertex>
bool prepareVitaGeometryScratch(std::vector<Vertex>& vertices,
    std::vector<unsigned short>& indices, std::size_t vertexCount, std::size_t indexCount)
{
    if (vertexCount > vertices.capacity()) std::vector<Vertex>().swap(vertices);
    if (indexCount > indices.capacity()) std::vector<unsigned short>().swap(indices);
    try
    {
        vertices.resize(vertexCount);
        indices.resize(indexCount);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        // The caller can generate the same mesh through synchronized mappings
        // of its existing GPU buffers. Free both scratch blocks for entities,
        // materials and scripts; an optional staging copy must not end play.
        std::vector<Vertex>().swap(vertices);
        std::vector<unsigned short>().swap(indices);
        return false;
    }
}

} }
